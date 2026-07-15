from __future__ import annotations

from datetime import datetime, timezone
import ctypes
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import tempfile
import threading
import time
from typing import Any, Mapping

from .transcription import (
    WhisperCppTranscriptionBackend,
    _whisper_cpp_text,
    inspect_wav,
)


BENCHMARK_SCHEMA_VERSION = 1
MANIFEST_SCHEMA_VERSION = 1
MAX_RUNS_PER_CASE = 20
MAX_TIMEOUT_SECONDS = 24 * 60 * 60
RSS_SAMPLE_SECONDS = 0.05
_WORD_PATTERN = re.compile(r"[\w']+", re.UNICODE)
_LOAD_TIME_PATTERN = re.compile(r"load time\s*=\s*([0-9.]+)\s*ms", re.IGNORECASE)
_TOTAL_TIME_PATTERN = re.compile(r"total time\s*=\s*([0-9.]+)\s*ms", re.IGNORECASE)
_GPU_PATTERN = re.compile(r"use gpu\s*=\s*([01])", re.IGNORECASE)


class BenchmarkManifestError(ValueError):
    pass


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _normalized_words(text: str) -> list[str]:
    return _WORD_PATTERN.findall(text.casefold())


def word_error_rate(reference: str, hypothesis: str) -> dict[str, Any]:
    reference_words = _normalized_words(reference)
    hypothesis_words = _normalized_words(hypothesis)
    previous = list(range(len(hypothesis_words) + 1))
    for reference_index, reference_word in enumerate(reference_words, start=1):
        current = [reference_index]
        for hypothesis_index, hypothesis_word in enumerate(hypothesis_words, start=1):
            current.append(
                min(
                    current[-1] + 1,
                    previous[hypothesis_index] + 1,
                    previous[hypothesis_index - 1]
                    + (reference_word != hypothesis_word),
                )
            )
        previous = current
    edits = previous[-1]
    return {
        "reference_words": len(reference_words),
        "hypothesis_words": len(hypothesis_words),
        "word_errors": edits,
        "word_error_rate": (
            edits / len(reference_words) if reference_words else None
        ),
        "unexpected_words_for_empty_reference": (
            len(hypothesis_words) if not reference_words else None
        ),
    }


def load_benchmark_manifest(path: Path) -> list[dict[str, Any]]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise BenchmarkManifestError(f"cannot read benchmark manifest: {exc}") from exc
    if not isinstance(payload, dict) or payload.get("schema_version") != MANIFEST_SCHEMA_VERSION:
        raise BenchmarkManifestError(
            f"benchmark manifest schema_version must be {MANIFEST_SCHEMA_VERSION}"
        )
    unknown_top_level = sorted(set(payload) - {"schema_version", "cases"})
    if unknown_top_level:
        raise BenchmarkManifestError(
            "benchmark manifest has unknown fields: "
            + ", ".join(unknown_top_level)
        )
    raw_cases = payload.get("cases")
    if not isinstance(raw_cases, list) or not raw_cases:
        raise BenchmarkManifestError("benchmark manifest cases must be a non-empty list")

    supported_keys = {
        "id",
        "audio",
        "reference",
        "tags",
        "notes",
        "expect",
        "runs",
        "timeout_seconds",
        "max_word_error_rate",
        "max_unexpected_words",
    }
    seen_ids: set[str] = set()
    cases: list[dict[str, Any]] = []
    for index, raw_case in enumerate(raw_cases):
        if not isinstance(raw_case, dict):
            raise BenchmarkManifestError(f"case {index} must be an object")
        unknown = sorted(set(raw_case) - supported_keys)
        if unknown:
            raise BenchmarkManifestError(
                f"case {index} has unknown fields: {', '.join(unknown)}"
            )
        case_id = raw_case.get("id")
        if not isinstance(case_id, str) or not case_id.strip() or len(case_id) > 80:
            raise BenchmarkManifestError(f"case {index} id must be 1..80 characters")
        if case_id in seen_ids:
            raise BenchmarkManifestError(f"duplicate benchmark case id: {case_id}")
        seen_ids.add(case_id)
        audio = raw_case.get("audio")
        if not isinstance(audio, str) or not audio:
            raise BenchmarkManifestError(f"case {case_id} audio must be a path")
        reference = raw_case.get("reference")
        if reference is not None and not isinstance(reference, str):
            raise BenchmarkManifestError(f"case {case_id} reference must be text")
        tags = raw_case.get("tags", [])
        if not isinstance(tags, list) or any(not isinstance(tag, str) for tag in tags):
            raise BenchmarkManifestError(f"case {case_id} tags must be strings")
        notes = raw_case.get("notes")
        if notes is not None and not isinstance(notes, str):
            raise BenchmarkManifestError(f"case {case_id} notes must be text")
        expected = raw_case.get("expect", "success")
        if expected not in {"success", "input_error", "runtime_error", "timeout"}:
            raise BenchmarkManifestError(
                f"case {case_id} expect must be success, input_error, runtime_error, or timeout"
            )
        runs = raw_case.get("runs")
        if runs is not None and (
            not isinstance(runs, int)
            or isinstance(runs, bool)
            or not 1 <= runs <= MAX_RUNS_PER_CASE
        ):
            raise BenchmarkManifestError(
                f"case {case_id} runs must be 1..{MAX_RUNS_PER_CASE}"
            )
        timeout_seconds = raw_case.get("timeout_seconds")
        if timeout_seconds is not None and (
            not isinstance(timeout_seconds, (int, float))
            or isinstance(timeout_seconds, bool)
            or not 0 < timeout_seconds <= MAX_TIMEOUT_SECONDS
        ):
            raise BenchmarkManifestError(
                f"case {case_id} timeout_seconds must be positive and bounded"
            )
        max_word_error_rate = raw_case.get("max_word_error_rate")
        if max_word_error_rate is not None and (
            not isinstance(max_word_error_rate, (int, float))
            or isinstance(max_word_error_rate, bool)
            or not 0 <= max_word_error_rate <= 1
        ):
            raise BenchmarkManifestError(
                f"case {case_id} max_word_error_rate must be between 0 and 1"
            )
        max_unexpected_words = raw_case.get("max_unexpected_words")
        if max_unexpected_words is not None and (
            not isinstance(max_unexpected_words, int)
            or isinstance(max_unexpected_words, bool)
            or max_unexpected_words < 0
        ):
            raise BenchmarkManifestError(
                f"case {case_id} max_unexpected_words must be non-negative"
            )
        reference_words = _normalized_words(reference) if reference is not None else []
        if max_word_error_rate is not None and not reference_words:
            raise BenchmarkManifestError(
                f"case {case_id} max_word_error_rate requires a non-empty reference"
            )
        if max_unexpected_words is not None and (
            reference is None or reference_words
        ):
            raise BenchmarkManifestError(
                f"case {case_id} max_unexpected_words requires an empty reference"
            )
        if max_word_error_rate is not None and max_unexpected_words is not None:
            raise BenchmarkManifestError(
                f"case {case_id} cannot combine speech and silence thresholds"
            )
        audio_path = Path(audio).expanduser()
        if not audio_path.is_absolute():
            audio_path = path.parent / audio_path
        cases.append(
            {
                **raw_case,
                "id": case_id,
                "audio_path": audio_path.resolve(),
                "tags": tags,
                "expect": expected,
            }
        )
    return cases


def _darwin_rss_bytes(pid: int) -> int | None:
    try:
        completed = subprocess.run(
            ["ps", "-o", "rss=", "-p", str(pid)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=1,
            check=False,
        )
        value = completed.stdout.strip()
        return int(value) * 1024 if value else None
    except (OSError, ValueError, subprocess.SubprocessError):
        return None


def _linux_rss_bytes(pid: int) -> int | None:
    try:
        for line in Path(f"/proc/{pid}/status").read_text(encoding="ascii").splitlines():
            if line.startswith("VmRSS:"):
                return int(line.split()[1]) * 1024
    except (OSError, UnicodeError, ValueError, IndexError):
        pass
    return None


def _windows_rss_bytes(pid: int) -> int | None:
    if sys.platform != "win32":
        return None

    class ProcessMemoryCounters(ctypes.Structure):
        _fields_ = [
            ("cb", ctypes.c_ulong),
            ("PageFaultCount", ctypes.c_ulong),
            ("PeakWorkingSetSize", ctypes.c_size_t),
            ("WorkingSetSize", ctypes.c_size_t),
            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
            ("PagefileUsage", ctypes.c_size_t),
            ("PeakPagefileUsage", ctypes.c_size_t),
        ]

    from ctypes import wintypes

    process_query_information = 0x0400
    process_vm_read = 0x0010
    kernel32 = ctypes.windll.kernel32  # type: ignore[attr-defined]
    psapi = ctypes.windll.psapi  # type: ignore[attr-defined]
    kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.CloseHandle.restype = wintypes.BOOL
    psapi.GetProcessMemoryInfo.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(ProcessMemoryCounters),
        wintypes.DWORD,
    ]
    psapi.GetProcessMemoryInfo.restype = wintypes.BOOL
    handle = kernel32.OpenProcess(
        process_query_information | process_vm_read,
        False,
        pid,
    )
    if not handle:
        return None
    try:
        counters = ProcessMemoryCounters()
        counters.cb = ctypes.sizeof(counters)
        if not psapi.GetProcessMemoryInfo(
            handle,
            ctypes.byref(counters),
            counters.cb,
        ):
            return None
        return int(counters.WorkingSetSize)
    finally:
        kernel32.CloseHandle(handle)


def _process_rss_bytes(pid: int) -> int | None:
    if sys.platform == "darwin":
        return _darwin_rss_bytes(pid)
    if sys.platform.startswith("linux"):
        return _linux_rss_bytes(pid)
    if sys.platform == "win32":
        return _windows_rss_bytes(pid)
    return None


def _terminate_and_reap(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=2)


def _run_command(command: list[str], timeout_seconds: float) -> dict[str, Any]:
    process = subprocess.Popen(
        command,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    peak_rss = 0
    stop_monitor = threading.Event()

    def monitor() -> None:
        nonlocal peak_rss
        while not stop_monitor.is_set():
            resident = _process_rss_bytes(process.pid)
            if resident is not None:
                peak_rss = max(peak_rss, resident)
            if process.poll() is not None:
                break
            stop_monitor.wait(RSS_SAMPLE_SECONDS)

    monitor_thread = threading.Thread(target=monitor, name="pj-benchmark-rss", daemon=True)
    started = time.perf_counter()
    monitor_thread.start()
    timed_out = False
    stdout = ""
    stderr = ""
    try:
        try:
            stdout, stderr = process.communicate(timeout=timeout_seconds)
        except subprocess.TimeoutExpired:
            timed_out = True
            _terminate_and_reap(process)
            stdout, stderr = process.communicate()
    finally:
        stop_monitor.set()
        monitor_thread.join(timeout=2)
        _terminate_and_reap(process)
    return {
        "exit_code": process.returncode,
        "timed_out": timed_out,
        "process_reaped": process.poll() is not None,
        "wall_seconds": time.perf_counter() - started,
        "peak_rss_bytes": peak_rss or None,
        "stdout": stdout,
        "stderr": stderr,
    }


def _timing_metrics(output: str) -> dict[str, Any]:
    load_match = _LOAD_TIME_PATTERN.search(output)
    total_match = _TOTAL_TIME_PATTERN.search(output)
    gpu_values = _GPU_PATTERN.findall(output)
    return {
        "model_load_seconds": (
            float(load_match.group(1)) / 1000 if load_match else None
        ),
        "runtime_total_seconds": (
            float(total_match.group(1)) / 1000 if total_match else None
        ),
        "runtime_reported_gpu_enabled": (
            any(value == "1" for value in gpu_values) if gpu_values else None
        ),
    }


def _runtime_evidence(output: str) -> list[str]:
    evidence = []
    for line in output.splitlines():
        stripped = line.strip()
        if (
            "use gpu" in stripped.lower()
            or stripped.startswith("system_info:")
            or "CPU total size" in stripped
        ):
            evidence.append(stripped[:1000])
    return evidence


def _tree_size(path: Path) -> int | None:
    try:
        if path.is_file():
            return path.stat().st_size
        if not path.is_dir():
            return None
        return sum(
            entry.stat().st_size
            for entry in path.rglob("*")
            if entry.is_file() and not entry.is_symlink()
        )
    except OSError:
        return None


def _physical_memory_bytes() -> int | None:
    if sys.platform == "win32":
        from ctypes import wintypes

        class MemoryStatus(ctypes.Structure):
            _fields_ = [
                ("length", ctypes.c_ulong),
                ("memory_load", ctypes.c_ulong),
                ("total_physical", ctypes.c_ulonglong),
                ("available_physical", ctypes.c_ulonglong),
                ("total_page_file", ctypes.c_ulonglong),
                ("available_page_file", ctypes.c_ulonglong),
                ("total_virtual", ctypes.c_ulonglong),
                ("available_virtual", ctypes.c_ulonglong),
                ("available_extended_virtual", ctypes.c_ulonglong),
            ]

        status = MemoryStatus()
        status.length = ctypes.sizeof(status)
        global_memory_status = ctypes.windll.kernel32.GlobalMemoryStatusEx  # type: ignore[attr-defined]
        global_memory_status.argtypes = [ctypes.POINTER(MemoryStatus)]
        global_memory_status.restype = wintypes.BOOL
        if global_memory_status(ctypes.byref(status)):
            return int(status.total_physical)
        return None
    try:
        return int(os.sysconf("SC_PHYS_PAGES")) * int(os.sysconf("SC_PAGE_SIZE"))
    except (AttributeError, OSError, TypeError, ValueError):
        return None


def _cpu_name() -> str:
    if sys.platform == "darwin":
        try:
            completed = subprocess.run(
                ["sysctl", "-n", "machdep.cpu.brand_string"],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
                timeout=2,
                check=False,
            )
            if completed.stdout.strip():
                return completed.stdout.strip()
        except (OSError, subprocess.SubprocessError):
            pass
    if sys.platform.startswith("linux"):
        try:
            for line in Path("/proc/cpuinfo").read_text(encoding="ascii").splitlines():
                if line.lower().startswith("model name"):
                    return line.split(":", 1)[1].strip()
        except (OSError, UnicodeError, IndexError):
            pass
    return platform.processor() or platform.machine() or "unknown"


def _platform_profile() -> dict[str, Any]:
    return {
        "system": platform.system(),
        "release": platform.release(),
        "version": platform.version(),
        "machine": platform.machine(),
        "cpu": _cpu_name(),
        "logical_cpu_count": os.cpu_count(),
        "physical_memory_bytes": _physical_memory_bytes(),
        "python": platform.python_version(),
    }


def _runtime_version(executable: str) -> str | None:
    try:
        completed = subprocess.run(
            [executable, "--version"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=20,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    match = re.search(r"whisper\.cpp version:\s*([^\s]+)", completed.stdout)
    return match.group(1) if match else None


def _expected_matches(expected: str, actual: str) -> bool:
    return expected == actual


def _invalid_input_case(
    case: Mapping[str, Any],
    audio_path: Path,
    error: Exception,
) -> dict[str, Any]:
    actual = "input_error"
    return {
        "id": case["id"],
        "audio_file": audio_path.name,
        "tags": case.get("tags", []),
        "notes": case.get("notes"),
        "expected": case["expect"],
        "actual": actual,
        "expectation_met": _expected_matches(str(case["expect"]), actual),
        "input_error": str(error),
        "runs": [],
    }


def _run_case(
    case: Mapping[str, Any],
    audio_path: Path,
    source: Mapping[str, Any],
    backend: WhisperCppTranscriptionBackend,
    *,
    default_runs: int,
    default_timeout_seconds: float,
) -> dict[str, Any]:
    duration_seconds = source["data_bytes"] / source["byte_rate"]
    run_count = int(case.get("runs", default_runs))
    timeout_seconds = float(case.get("timeout_seconds", default_timeout_seconds))
    runs = []
    for run_index in range(run_count):
        with tempfile.TemporaryDirectory(prefix="pj-transcription-benchmark-") as temporary:
            output_prefix = Path(temporary) / "transcript"
            command = backend.build_command(audio_path, output_prefix, quiet=False)
            command_result = _run_command(command, timeout_seconds)
            combined_output = command_result["stdout"] + "\n" + command_result["stderr"]
            if command_result["timed_out"]:
                actual = "timeout"
                transcript_text = None
                segments: list[dict[str, Any]] = []
                output_error = f"runtime exceeded {timeout_seconds:g} seconds"
            elif command_result["exit_code"] != 0:
                actual = "runtime_error"
                transcript_text = None
                segments = []
                output_error = combined_output.strip().splitlines()[-1][:500] if combined_output.strip() else "runtime failed without a diagnostic"
            else:
                output_path = output_prefix.with_suffix(".json")
                try:
                    payload = json.loads(output_path.read_text(encoding="utf-8"))
                    transcript_text, segments = _whisper_cpp_text(payload, allow_empty=True)
                    actual = "success"
                    output_error = None
                except (OSError, UnicodeError, json.JSONDecodeError, RuntimeError) as exc:
                    actual = "runtime_error"
                    transcript_text = None
                    segments = []
                    output_error = f"invalid runtime output: {exc}"
            timing = _timing_metrics(combined_output)
            cpu_only_evidence_met = (
                "--no-gpu" in command
                and timing["runtime_reported_gpu_enabled"] is False
            )
            quality = None
            quality_expectation_met = True
            if transcript_text is not None and isinstance(case.get("reference"), str):
                quality = word_error_rate(str(case["reference"]), transcript_text)
                max_word_error_rate = case.get("max_word_error_rate")
                if max_word_error_rate is not None:
                    measured_wer = quality["word_error_rate"]
                    quality_expectation_met = (
                        measured_wer is not None
                        and measured_wer <= float(max_word_error_rate)
                    )
                elif quality["reference_words"] == 0:
                    maximum = int(case.get("max_unexpected_words", 0))
                    quality_expectation_met = (
                        quality["unexpected_words_for_empty_reference"] <= maximum
                    )
            runs.append(
                {
                    "sequence": run_index + 1,
                    "cache_state": (
                        "uncontrolled_first_process" if run_index == 0 else "warm_process"
                    ),
                    "actual": actual,
                    "expectation_met": (
                        _expected_matches(str(case["expect"]), actual)
                        and quality_expectation_met
                        and (actual != "success" or cpu_only_evidence_met)
                    ),
                    "exit_code": command_result["exit_code"],
                    "process_reaped": command_result["process_reaped"],
                    "wall_seconds": command_result["wall_seconds"],
                    "audio_seconds": duration_seconds,
                    "real_time_factor": (
                        command_result["wall_seconds"] / duration_seconds
                        if duration_seconds > 0
                        else None
                    ),
                    "peak_rss_bytes": command_result["peak_rss_bytes"],
                    "cpu_only_requested": "--no-gpu" in command,
                    "cpu_only_evidence_met": cpu_only_evidence_met,
                    "command": command,
                    **timing,
                    "runtime_evidence": _runtime_evidence(combined_output),
                    "transcript": transcript_text,
                    "segments": segments,
                    "quality": quality,
                    "quality_expectation_met": quality_expectation_met,
                    "error": output_error,
                }
            )

    actual_outcomes = {run["actual"] for run in runs}
    actual = next(iter(actual_outcomes)) if len(actual_outcomes) == 1 else "mixed"
    return {
        "id": case["id"],
        "audio_file": audio_path.name,
        "source": dict(source),
        "duration_seconds": duration_seconds,
        "tags": case.get("tags", []),
        "notes": case.get("notes"),
        "reference": case.get("reference"),
        "expected": case["expect"],
        "actual": actual,
        "expectation_met": all(run["expectation_met"] for run in runs),
        "runs": runs,
    }


def benchmark_manifest(
    manifest_path: Path,
    backend: WhisperCppTranscriptionBackend,
    *,
    runs: int = 2,
    timeout_seconds: float = 60 * 60,
    runtime_root: Path | None = None,
    provenance: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    if not 1 <= runs <= MAX_RUNS_PER_CASE:
        raise BenchmarkManifestError(f"runs must be 1..{MAX_RUNS_PER_CASE}")
    if not 0 < timeout_seconds <= MAX_TIMEOUT_SECONDS:
        raise BenchmarkManifestError("timeout_seconds must be positive and bounded")
    cases = load_benchmark_manifest(manifest_path)
    fingerprint = backend.fingerprint()
    executable = str(fingerprint["runtime_executable"])
    results = []
    started_at = _utc_now()
    for case in cases:
        audio_path = Path(case["audio_path"])
        try:
            source = inspect_wav(audio_path)
        except (OSError, ValueError) as exc:
            results.append(_invalid_input_case(case, audio_path, exc))
            continue
        results.append(
            _run_case(
                case,
                audio_path,
                source,
                backend,
                default_runs=runs,
                default_timeout_seconds=timeout_seconds,
            )
        )
    passed = all(result["expectation_met"] for result in results)
    return {
        "schema_version": BENCHMARK_SCHEMA_VERSION,
        "started_at": started_at,
        "finished_at": _utc_now(),
        "passed": passed,
        "platform": _platform_profile(),
        "backend": {
            **fingerprint,
            "runtime_version": _runtime_version(executable),
            "runtime_install_root": str(runtime_root) if runtime_root else executable,
            "runtime_install_bytes": _tree_size(runtime_root or Path(executable)),
            "provenance": dict(provenance or {}),
        },
        "manifest": {
            "file": manifest_path.name,
            "sha256": _sha256_file(manifest_path),
        },
        "summary": {
            "case_count": len(results),
            "expectations_met": sum(result["expectation_met"] for result in results),
            "expectations_failed": sum(not result["expectation_met"] for result in results),
        },
        "cases": results,
    }


def write_benchmark_report(path: Path, report: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    serialized = json.dumps(report, indent=2, sort_keys=True) + "\n"
    descriptor, temporary_name = tempfile.mkstemp(
        dir=path.parent,
        prefix=f".{path.name}.",
        suffix=".tmp",
    )
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            handle.write(serialized)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary_path, path)
    except BaseException:
        temporary_path.unlink(missing_ok=True)
        raise
