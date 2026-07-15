from __future__ import annotations

import json
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory
import time
import unittest
from unittest.mock import patch

from pocket_journal_partner.transcription import WhisperCppTranscriptionBackend
from pocket_journal_partner.transcription_benchmark import (
    BenchmarkManifestError,
    _run_command,
    benchmark_manifest,
    load_benchmark_manifest,
    word_error_rate,
    write_benchmark_report,
)


def pcm_wav(samples: bytes = b"\x00\x00" * 16_000) -> bytes:
    fmt = struct.pack("<HHIIHH", 1, 1, 16_000, 32_000, 2, 16)
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    chunks += b"data" + struct.pack("<I", len(samples)) + samples
    return b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks


class TranscriptionBenchmarkTests(unittest.TestCase):
    def test_word_error_rate_normalizes_case_and_punctuation(self) -> None:
        exact = word_error_rate("Hello, WORLD!", "hello world")
        changed = word_error_rate("one two three", "one four")
        silence = word_error_rate("", "unexpected words")

        self.assertEqual(exact["word_error_rate"], 0)
        self.assertEqual(changed["word_errors"], 2)
        self.assertAlmostEqual(changed["word_error_rate"], 2 / 3)
        self.assertIsNone(silence["word_error_rate"])
        self.assertEqual(silence["unexpected_words_for_empty_reference"], 2)

    def test_manifest_resolves_relative_audio_and_rejects_unknown_fields(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            audio = root / "audio.wav"
            audio.write_bytes(pcm_wav())
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({
                "schema_version": 1,
                "cases": [{"id": "speech", "audio": "audio.wav"}],
            }), encoding="utf-8")

            cases = load_benchmark_manifest(manifest)

            self.assertEqual(cases[0]["audio_path"], audio.resolve())
            manifest.write_text(json.dumps({
                "schema_version": 1,
                "cases": [{"id": "speech", "audio": "audio.wav", "typo": True}],
            }), encoding="utf-8")
            with self.assertRaisesRegex(BenchmarkManifestError, "unknown fields"):
                load_benchmark_manifest(manifest)

            manifest.write_text(json.dumps({
                "schema_version": 1,
                "cases": [{"id": "speech", "audio": "audio.wav"}],
                "typo": True,
            }), encoding="utf-8")
            with self.assertRaisesRegex(BenchmarkManifestError, "unknown fields"):
                load_benchmark_manifest(manifest)

    def test_manifest_rejects_mismatched_quality_thresholds(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            audio = root / "audio.wav"
            audio.write_bytes(pcm_wav())
            manifest = root / "manifest.json"
            for case in (
                {"id": "speech", "audio": "audio.wav", "max_word_error_rate": 0.2},
                {
                    "id": "speech",
                    "audio": "audio.wav",
                    "reference": "spoken words",
                    "max_unexpected_words": 0,
                },
            ):
                manifest.write_text(json.dumps({
                    "schema_version": 1,
                    "cases": [case],
                }), encoding="utf-8")
                with self.assertRaisesRegex(BenchmarkManifestError, "requires"):
                    load_benchmark_manifest(manifest)

    def test_timeout_terminates_and_reaps_child(self) -> None:
        started = time.monotonic()

        result = _run_command(
            [sys.executable, "-c", "import time; time.sleep(30)"],
            timeout_seconds=0.05,
        )

        self.assertTrue(result["timed_out"])
        self.assertTrue(result["process_reaped"])
        self.assertLess(time.monotonic() - started, 3)

    def test_benchmark_records_cpu_evidence_quality_and_warm_run(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            model = root / "model.bin"
            model.write_bytes(b"model")
            audio = root / "audio.wav"
            audio.write_bytes(pcm_wav())
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({
                "schema_version": 1,
                "cases": [{
                    "id": "speech",
                    "audio": "audio.wav",
                    "reference": "Hello world",
                }],
            }), encoding="utf-8")
            fake_runtime = root / "runtime.py"
            fake_runtime.write_text(
                "from pathlib import Path\n"
                "import json, sys\n"
                "Path(sys.argv[1]).with_suffix('.json').write_text("
                "json.dumps({'transcription': [{'text': 'Hello world'}]}), encoding='utf-8')\n"
                "print('whisper_init_with_params_no_state: use gpu = 0', file=sys.stderr)\n"
                "print('whisper_print_timings: load time = 25.00 ms', file=sys.stderr)\n"
                "print('whisper_print_timings: total time = 250.00 ms', file=sys.stderr)\n",
                encoding="utf-8",
            )
            backend = WhisperCppTranscriptionBackend(model, sys.executable, threads=2)

            def build_command(
                _audio: Path,
                output_prefix: Path,
                *,
                quiet: bool = True,
            ) -> list[str]:
                self.assertFalse(quiet)
                return [sys.executable, str(fake_runtime), str(output_prefix), "--no-gpu"]

            with patch.object(backend, "build_command", side_effect=build_command):
                report = benchmark_manifest(manifest, backend, runs=2, timeout_seconds=5)

        self.assertTrue(report["passed"])
        self.assertEqual(report["summary"]["expectations_met"], 1)
        runs = report["cases"][0]["runs"]
        self.assertEqual(len(runs), 2)
        self.assertEqual(runs[0]["cache_state"], "uncontrolled_first_process")
        self.assertEqual(runs[1]["cache_state"], "warm_process")
        self.assertTrue(runs[0]["cpu_only_requested"])
        self.assertTrue(runs[0]["cpu_only_evidence_met"])
        self.assertFalse(runs[0]["runtime_reported_gpu_enabled"])
        self.assertEqual(runs[0]["model_load_seconds"], 0.025)
        self.assertEqual(runs[0]["runtime_total_seconds"], 0.25)
        self.assertEqual(runs[0]["quality"]["word_error_rate"], 0)
        self.assertTrue(runs[0]["process_reaped"])

    def test_expected_invalid_wav_is_evidence_not_suite_failure(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            model = root / "model.bin"
            model.write_bytes(b"model")
            invalid = root / "truncated.wav"
            invalid.write_bytes(b"RIFF")
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({
                "schema_version": 1,
                "cases": [{
                    "id": "truncated",
                    "audio": "truncated.wav",
                    "expect": "input_error",
                }],
            }), encoding="utf-8")
            backend = WhisperCppTranscriptionBackend(model, sys.executable)

            report = benchmark_manifest(manifest, backend, runs=1)

        self.assertTrue(report["passed"])
        self.assertEqual(report["cases"][0]["actual"], "input_error")
        self.assertEqual(report["cases"][0]["runs"], [])

    def test_silence_hallucination_fails_default_empty_reference_threshold(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            model = root / "model.bin"
            model.write_bytes(b"model")
            audio = root / "silence.wav"
            audio.write_bytes(pcm_wav())
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({
                "schema_version": 1,
                "cases": [{
                    "id": "silence",
                    "audio": "silence.wav",
                    "reference": "",
                    "expect": "success",
                }],
            }), encoding="utf-8")
            fake_runtime = root / "runtime.py"
            fake_runtime.write_text(
                "from pathlib import Path\n"
                "import json, sys\n"
                "Path(sys.argv[1]).with_suffix('.json').write_text("
                "json.dumps({'text': 'Thanks for watching'}), encoding='utf-8')\n"
                "print('whisper_init_with_params_no_state: use gpu = 0', file=sys.stderr)\n",
                encoding="utf-8",
            )
            backend = WhisperCppTranscriptionBackend(model, sys.executable)

            def build_command(
                _audio: Path,
                output_prefix: Path,
                *,
                quiet: bool = True,
            ) -> list[str]:
                return [sys.executable, str(fake_runtime), str(output_prefix), "--no-gpu"]

            with patch.object(backend, "build_command", side_effect=build_command):
                report = benchmark_manifest(manifest, backend, runs=1, timeout_seconds=5)

        self.assertFalse(report["passed"])
        run = report["cases"][0]["runs"][0]
        self.assertEqual(run["actual"], "success")
        self.assertFalse(run["quality_expectation_met"])
        self.assertEqual(run["quality"]["unexpected_words_for_empty_reference"], 3)

    def test_gpu_runtime_evidence_fails_cpu_only_benchmark(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            model = root / "model.bin"
            model.write_bytes(b"model")
            audio = root / "audio.wav"
            audio.write_bytes(pcm_wav())
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({
                "schema_version": 1,
                "cases": [{"id": "speech", "audio": "audio.wav"}],
            }), encoding="utf-8")
            fake_runtime = root / "runtime.py"
            fake_runtime.write_text(
                "from pathlib import Path\n"
                "import json, sys\n"
                "Path(sys.argv[1]).with_suffix('.json').write_text("
                "json.dumps({'text': 'hello'}), encoding='utf-8')\n"
                "print('whisper_init_with_params_no_state: use gpu = 1', file=sys.stderr)\n",
                encoding="utf-8",
            )
            backend = WhisperCppTranscriptionBackend(model, sys.executable)

            def build_command(
                _audio: Path,
                output_prefix: Path,
                *,
                quiet: bool = True,
            ) -> list[str]:
                return [sys.executable, str(fake_runtime), str(output_prefix), "--no-gpu"]

            with patch.object(backend, "build_command", side_effect=build_command):
                report = benchmark_manifest(manifest, backend, runs=1, timeout_seconds=5)

        self.assertFalse(report["passed"])
        run = report["cases"][0]["runs"][0]
        self.assertTrue(run["runtime_reported_gpu_enabled"])
        self.assertFalse(run["cpu_only_evidence_met"])

    def test_report_write_replaces_atomically_without_temp_file(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "report.json"

            write_benchmark_report(path, {"passed": True})

            self.assertEqual(json.loads(path.read_text(encoding="utf-8")), {"passed": True})
            self.assertEqual(list(path.parent.glob(f".{path.name}.*.tmp")), [])


if __name__ == "__main__":
    unittest.main()
