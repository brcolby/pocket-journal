from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import patch

from pocket_journal_partner.transcription import (
    FakeTranscriptionBackend,
    HuggingFaceTranscriptionBackend,
    MODEL_REVISION,
    MODEL_ARTIFACT_DIGEST_UNAVAILABLE_REASON,
    WHISPER_CPP_DEFAULT_MODEL,
    WHISPER_CPP_NO_SPEECH_TEXT,
    WhisperCppTranscriptionBackend,
    _whisper_cpp_text,
    backend_from_name,
    inspect_wav,
)


def pcm_wav(samples: bytes = b"\x00\x00") -> bytes:
    fmt = struct.pack("<HHIIHH", 1, 1, 16_000, 32_000, 2, 16)
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    chunks += b"data" + struct.pack("<I", len(samples)) + samples
    if len(samples) & 1:
        chunks += b"\x00"
    return b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks


class TranscriptionTests(unittest.TestCase):
    def test_inspect_minimal_pcm_wav(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "sample.wav"
            content = pcm_wav()
            path.write_bytes(content)

            inspected = inspect_wav(path)

            self.assertEqual(inspected["sha256"], hashlib.sha256(content).hexdigest())
            self.assertEqual(inspected["bytes"], len(content))
            self.assertEqual(inspected["byte_length"], len(content))
            self.assertEqual(inspected["data_bytes"], 2)
            self.assertEqual(inspected["sample_rate"], 16_000)
            self.assertEqual(inspected["channels"], 1)
            self.assertEqual(inspected["sample_width_bits"], 16)
            self.assertEqual(inspected["sample_width"], 2)
            self.assertEqual(inspected["bits_per_sample"], 16)
            self.assertEqual(inspected["audio_format"], 1)

    def test_inspect_rejects_malformed_or_truncated_wav(self) -> None:
        valid = pcm_wav()
        cases = {
            "short header": b"RIFF",
            "wrong container": b"NOPE" + valid[4:],
            "wrong form": valid[:8] + b"AVI " + valid[12:],
            "truncated riff": valid[:-1],
            "truncated data": valid[:40] + struct.pack("<I", 20) + valid[44:],
        }
        with TemporaryDirectory() as tmp:
            for name, content in cases.items():
                with self.subTest(name=name):
                    path = Path(tmp) / f"{name}.wav"
                    path.write_bytes(content)
                    with self.assertRaises(ValueError):
                        inspect_wav(path)

    def test_inspect_rejects_non_pcm_and_inconsistent_format_metadata(self) -> None:
        cases = {
            "compressed format": (3, 1, 16_000, 32_000, 2, 16),
            "wrong byte rate": (1, 1, 16_000, 31_999, 2, 16),
            "wrong block alignment": (1, 1, 16_000, 32_000, 4, 16),
        }
        with TemporaryDirectory() as tmp:
            for name, fields in cases.items():
                with self.subTest(name=name):
                    fmt = struct.pack("<HHIIHH", *fields)
                    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
                    chunks += b"data" + struct.pack("<I", 2) + b"\x00\x00"
                    path = Path(tmp) / f"{name}.wav"
                    path.write_bytes(
                        b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks
                    )

                    with self.assertRaises(ValueError):
                        inspect_wav(path)

    def test_inspect_rejects_pcm_layout_not_emitted_by_device(self) -> None:
        cases = {
            "stereo": (1, 2, 16_000, 64_000, 4, 16),
            "wrong sample rate": (1, 1, 8_000, 16_000, 2, 16),
            "wrong sample width": (1, 1, 16_000, 16_000, 1, 8),
        }
        with TemporaryDirectory() as tmp:
            for name, fields in cases.items():
                with self.subTest(name=name):
                    fmt = struct.pack("<HHIIHH", *fields)
                    block_align = fields[4]
                    samples = bytes(block_align)
                    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
                    chunks += b"data" + struct.pack("<I", len(samples)) + samples
                    if len(samples) & 1:
                        chunks += b"\x00"
                    path = Path(tmp) / f"{name}.wav"
                    path.write_bytes(
                        b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks
                    )

                    with self.assertRaisesRegex(ValueError, "unsupported device PCM layout"):
                        inspect_wav(path)

    def test_inspect_rejects_incomplete_audio_frame(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "partial-frame.wav"
            path.write_bytes(pcm_wav(b"\x00\x00\x00"))

            with self.assertRaisesRegex(ValueError, "not frame aligned"):
                inspect_wav(path)

    def test_inspect_rejects_bytes_after_declared_riff(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "trailing.wav"
            path.write_bytes(pcm_wav() + b"trailing")

            with self.assertRaisesRegex(ValueError, "trailing bytes"):
                inspect_wav(path)

    def test_digest_changes_with_audio_bytes(self) -> None:
        with TemporaryDirectory() as tmp:
            first = Path(tmp) / "first.wav"
            second = Path(tmp) / "second.wav"
            first.write_bytes(pcm_wav(b"\x00\x00"))
            second.write_bytes(pcm_wav(b"\x01\x00"))

            self.assertNotEqual(inspect_wav(first)["sha256"], inspect_wav(second)["sha256"])

    def test_fake_backend(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "sample.wav"
            path.write_bytes(b"RIFF")
            result = FakeTranscriptionBackend().transcribe(path)
            self.assertEqual(result["model"], "fake")
            self.assertEqual(result["audio_file"], "sample.wav")
            self.assertEqual(result["text"], "Transcript for sample.")

    def test_backend_fingerprints_are_stable_json_without_importing_transformers(self) -> None:
        fake = FakeTranscriptionBackend()
        hf = HuggingFaceTranscriptionBackend(decoding_parameters={"language": "en"})
        before = "transformers" in sys.modules

        self.assertEqual(fake.fingerprint(), fake.fingerprint())
        self.assertEqual(hf.fingerprint(), hf.fingerprint())
        self.assertNotEqual(fake.fingerprint(), hf.fingerprint())
        self.assertEqual(hf.fingerprint()["model_id"], "distil-whisper/distil-large-v3.5")
        self.assertEqual(hf.fingerprint()["model_revision"], MODEL_REVISION)
        self.assertEqual(
            hf.fingerprint()["model_artifact"],
            {
                "algorithm": "sha256",
                "digest": None,
                "status": "unavailable",
                "reason": MODEL_ARTIFACT_DIGEST_UNAVAILABLE_REASON,
            },
        )
        self.assertEqual(hf.fingerprint()["decoding_parameters"], {"language": "en"})
        json.dumps(fake.fingerprint())
        json.dumps(hf.fingerprint())
        self.assertEqual("transformers" in sys.modules, before)

    def test_huggingface_fingerprint_distinguishes_settings(self) -> None:
        first = HuggingFaceTranscriptionBackend(model_id="model-a")
        second = HuggingFaceTranscriptionBackend(model_id="model-b")
        third = HuggingFaceTranscriptionBackend(model_id="model-a", decoding_parameters={"task": "translate"})
        fourth = HuggingFaceTranscriptionBackend(model_id="model-a", model_revision="other-revision")
        fifth = HuggingFaceTranscriptionBackend(
            model_id="model-a", model_artifact_sha256="a" * 64
        )

        self.assertNotEqual(first.fingerprint(), second.fingerprint())
        self.assertNotEqual(first.fingerprint(), third.fingerprint())
        self.assertNotEqual(first.fingerprint(), fourth.fingerprint())
        self.assertNotEqual(first.fingerprint(), fifth.fingerprint())
        self.assertEqual(fifth.fingerprint()["model_artifact"]["status"], "verified")

    def test_huggingface_rejects_unpinned_revision_and_invalid_artifact_digest(self) -> None:
        with self.assertRaisesRegex(ValueError, "model_revision"):
            HuggingFaceTranscriptionBackend(model_revision="")
        with self.assertRaisesRegex(ValueError, "model_artifact_sha256"):
            HuggingFaceTranscriptionBackend(model_artifact_sha256="not-a-digest")

    def test_huggingface_fingerprint_does_not_load_pipeline(self) -> None:
        backend = HuggingFaceTranscriptionBackend(model_artifact_sha256="A" * 64)

        fingerprint = backend.fingerprint()

        self.assertIsNone(backend._pipeline)
        self.assertEqual(fingerprint["model_artifact"]["digest"], "a" * 64)

    def test_huggingface_pipeline_uses_pinned_revision(self) -> None:
        calls: list[tuple[str, dict[str, str]]] = []
        transformers = ModuleType("transformers")

        def fake_pipeline(task: str, **kwargs: str):
            calls.append((task, kwargs))
            return object()

        transformers.pipeline = fake_pipeline  # type: ignore[attr-defined]
        backend = HuggingFaceTranscriptionBackend(
            model_id="model-a",
            model_revision="immutable-revision",
        )

        with patch.dict(sys.modules, {"transformers": transformers}):
            loaded = backend._load()

        self.assertIs(loaded, backend._pipeline)
        self.assertEqual(
            calls,
            [
                (
                    "automatic-speech-recognition",
                    {"model": "model-a", "revision": "immutable-revision"},
                )
            ],
        )

    def test_whisper_cpp_availability_is_explicit_without_downloading(self) -> None:
        backend = WhisperCppTranscriptionBackend(
            model_path="/missing/model.bin",
            executable="definitely-missing-whisper-cli",
        )

        status = backend.availability()

        self.assertFalse(status["available"])
        self.assertTrue(status["cpu_only"])
        self.assertFalse(status["cloud_required"])
        self.assertEqual(status["recommended_model"], WHISPER_CPP_DEFAULT_MODEL)
        self.assertEqual(len(status["issues"]), 2)

    def test_whisper_cpp_fingerprint_pins_local_model_bytes(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            executable = root / "whisper-cli"
            executable.write_text("#!/bin/sh\n", encoding="utf-8")
            executable.chmod(0o755)
            model = root / WHISPER_CPP_DEFAULT_MODEL
            model.write_bytes(b"local-open-weights")
            backend = WhisperCppTranscriptionBackend(model, str(executable), threads=2)

            fingerprint = backend.fingerprint()

        self.assertEqual(fingerprint["backend"], "whisper-cpp")
        self.assertEqual(fingerprint["model_id"], WHISPER_CPP_DEFAULT_MODEL)
        self.assertEqual(
            fingerprint["model_artifact"]["digest"],
            hashlib.sha256(b"local-open-weights").hexdigest(),
        )
        self.assertEqual(
            fingerprint["runtime_artifact_sha256"],
            hashlib.sha256(b"#!/bin/sh\n").hexdigest(),
        )
        self.assertEqual(fingerprint["decoding_parameters"]["threads"], 2)
        self.assertTrue(fingerprint["decoding_parameters"]["cpu_only"])

    def test_whisper_cpp_transcribes_json_segments_without_a_shell(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            executable = root / "whisper-cli"
            executable.write_text("#!/bin/sh\n", encoding="utf-8")
            executable.chmod(0o755)
            model = root / WHISPER_CPP_DEFAULT_MODEL
            model.write_bytes(b"model")
            audio = root / "audio.wav"
            audio.write_bytes(pcm_wav())
            backend = WhisperCppTranscriptionBackend(model, str(executable))

            def run(command, **kwargs):  # type: ignore[no-untyped-def]
                self.assertIsInstance(command, list)
                self.assertNotIn("shell", kwargs)
                self.assertIn("--no-gpu", command)
                self.assertEqual(command[command.index("--beam-size") + 1], "5")
                self.assertEqual(command[command.index("--best-of") + 1], "5")
                self.assertEqual(command[command.index("--temperature") + 1], "0.0")
                self.assertEqual(command[command.index("--no-speech-thold") + 1], "0.6")
                self.assertEqual(command[command.index("-t") + 1], "4")
                output = Path(command[command.index("-of") + 1]).with_suffix(".json")
                output.write_text(json.dumps({
                    "transcription": [
                        {"text": " Hello", "timestamps": {"from": "00:00:00.000", "to": "00:00:01.000"}},
                        {"text": " world"},
                    ]
                }), encoding="utf-8")
                return SimpleNamespace(returncode=0, stdout="", stderr="")

            with patch("pocket_journal_partner.transcription.subprocess.run", side_effect=run):
                result = backend.transcribe(audio)

        self.assertEqual(result["text"], "Hello world")
        self.assertEqual(result["language"], "en")
        self.assertEqual(len(result["segments"]), 2)

    def test_whisper_cpp_normalizes_blank_audio_to_no_speech_result(self) -> None:
        text, segments = _whisper_cpp_text(
            {"transcription": [{"text": " [BLANK_AUDIO] "}]},
            allow_empty=True,
        )

        self.assertEqual(text, "")
        self.assertEqual(segments, [])
        with self.assertRaisesRegex(RuntimeError, "empty transcript"):
            _whisper_cpp_text({"transcription": []})

        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            executable = root / "whisper-cli"
            executable.write_text("#!/bin/sh\n", encoding="utf-8")
            executable.chmod(0o755)
            model = root / WHISPER_CPP_DEFAULT_MODEL
            model.write_bytes(b"model")
            audio = root / "audio.wav"
            audio.write_bytes(pcm_wav())
            backend = WhisperCppTranscriptionBackend(model, str(executable))

            def run(command, **_kwargs):  # type: ignore[no-untyped-def]
                output = Path(command[command.index("-of") + 1]).with_suffix(".json")
                output.write_text(
                    json.dumps({"transcription": [{"text": "[BLANK_AUDIO]"}]}),
                    encoding="utf-8",
                )
                return SimpleNamespace(returncode=0, stdout="", stderr="")

            with patch("pocket_journal_partner.transcription.subprocess.run", side_effect=run):
                result = backend.transcribe(audio)

        self.assertEqual(result["text"], WHISPER_CPP_NO_SPEECH_TEXT)
        self.assertTrue(result["no_speech"])

    def test_backend_factory_selects_whisper_cpp(self) -> None:
        backend = backend_from_name("whisper-cpp", model_path="model.bin")

        self.assertIsInstance(backend, WhisperCppTranscriptionBackend)


if __name__ == "__main__":
    unittest.main()
