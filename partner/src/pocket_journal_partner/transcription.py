from __future__ import annotations

from abc import ABC, abstractmethod
from datetime import datetime, timezone
import hashlib
from importlib import metadata
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from typing import Any, Mapping

MODEL_ID = "distil-whisper/distil-large-v3.5"
MODEL_REVISION = "728a7691f3ff1d3d971528d3203a6e9559165d41"
DEVICE_SAMPLE_RATE = 16_000
DEVICE_CHANNELS = 1
DEVICE_BITS_PER_SAMPLE = 16
MODEL_ARTIFACT_DIGEST_UNAVAILABLE_REASON = (
    "no canonical SHA-256 exists for the multi-file Hugging Face snapshot; "
    "model_revision pins its immutable contents"
)
WHISPER_CPP_DEFAULT_MODEL = "ggml-base.en-q5_0.bin"
WHISPER_CPP_MODEL_ENV = "PJ_WHISPER_MODEL"
WHISPER_CPP_EXECUTABLE_ENV = "PJ_WHISPER_CPP"
WHISPER_CPP_MAX_MODEL_BYTES = 2 * 1024 * 1024 * 1024
WHISPER_CPP_DEFAULT_THREADS = 4
WHISPER_CPP_BEAM_SIZE = 5
WHISPER_CPP_BEST_OF = 5
WHISPER_CPP_TEMPERATURE = 0.0
WHISPER_CPP_TEMPERATURE_INCREMENT = 0.2
WHISPER_CPP_NO_SPEECH_THRESHOLD = 0.6
WHISPER_CPP_BLANK_AUDIO_SENTINEL = "[BLANK_AUDIO]"
WHISPER_CPP_NO_SPEECH_TEXT = "NO SPEECH"


def inspect_wav(path: Path) -> dict[str, Any]:
    """Return stable audio identity and format metadata for a RIFF/WAVE file."""
    blob = path.read_bytes()
    byte_length = len(blob)
    if byte_length < 12:
        raise ValueError("truncated RIFF/WAVE header")
    if blob[:4] != b"RIFF":
        raise ValueError("not a RIFF file")
    if blob[8:12] != b"WAVE":
        raise ValueError("RIFF file is not WAVE audio")

    riff_end = 8 + struct.unpack_from("<I", blob, 4)[0]
    if riff_end < 12:
        raise ValueError("invalid RIFF size")
    if riff_end > byte_length:
        raise ValueError("truncated RIFF payload")
    if riff_end != byte_length:
        raise ValueError("WAV file has trailing bytes after RIFF payload")

    format_fields: tuple[int, int, int, int, int, int] | None = None
    data_chunk_sizes: list[int] = []
    offset = 12
    while offset < riff_end:
        if riff_end - offset < 8:
            raise ValueError("truncated WAV chunk header")
        chunk_id = blob[offset : offset + 4]
        chunk_size = struct.unpack_from("<I", blob, offset + 4)[0]
        payload_start = offset + 8
        payload_end = payload_start + chunk_size
        padded_end = payload_end + (chunk_size & 1)
        if payload_end > riff_end:
            raise ValueError(f"truncated {chunk_id!r} chunk payload")
        if padded_end > riff_end:
            raise ValueError(f"truncated {chunk_id!r} chunk padding")

        if chunk_id == b"fmt " and format_fields is None:
            if chunk_size < 16:
                raise ValueError("truncated WAV format chunk")
            audio_format, channels, sample_rate, byte_rate, block_align, bits_per_sample = (
                struct.unpack_from("<HHIIHH", blob, payload_start)
            )
            if audio_format != 1:
                raise ValueError("unsupported WAV format: expected PCM")
            if channels == 0 or sample_rate == 0 or bits_per_sample == 0 or block_align == 0:
                raise ValueError("invalid WAV format values")
            expected_block_align = channels * ((bits_per_sample + 7) // 8)
            expected_byte_rate = sample_rate * expected_block_align
            if block_align != expected_block_align or byte_rate != expected_byte_rate:
                raise ValueError("inconsistent WAV byte rate or block alignment")
            format_fields = (
                audio_format,
                channels,
                sample_rate,
                byte_rate,
                block_align,
                bits_per_sample,
            )
        elif chunk_id == b"data":
            data_chunk_sizes.append(chunk_size)

        offset = padded_end

    if format_fields is None:
        raise ValueError("WAV format chunk is missing")
    data_bytes = sum(data_chunk_sizes)
    if data_bytes == 0:
        raise ValueError("WAV data chunk is missing or empty")

    audio_format, channels, sample_rate, byte_rate, block_align, bits_per_sample = format_fields
    if (
        channels != DEVICE_CHANNELS
        or sample_rate != DEVICE_SAMPLE_RATE
        or bits_per_sample != DEVICE_BITS_PER_SAMPLE
    ):
        raise ValueError(
            "unsupported device PCM layout: expected "
            f"{DEVICE_SAMPLE_RATE} Hz, {DEVICE_CHANNELS} channel, "
            f"{DEVICE_BITS_PER_SAMPLE}-bit"
        )
    if any(chunk_size % block_align != 0 for chunk_size in data_chunk_sizes):
        raise ValueError("WAV data chunk is not frame aligned")
    return {
        "sha256": hashlib.sha256(blob).hexdigest(),
        "bytes": byte_length,
        "byte_length": byte_length,
        "data_bytes": data_bytes,
        "sample_rate": sample_rate,
        "channels": channels,
        "sample_width_bits": bits_per_sample,
        "sample_width": (bits_per_sample + 7) // 8,
        "bits_per_sample": bits_per_sample,
        "audio_format": audio_format,
        "byte_rate": byte_rate,
        "block_align": block_align,
    }


class TranscriptionBackend(ABC):
    @abstractmethod
    def fingerprint(self) -> dict[str, Any]:
        """Return stable, JSON-serializable settings that identify the backend."""
        raise NotImplementedError

    @abstractmethod
    def transcribe(self, audio_path: Path) -> dict[str, Any]:
        raise NotImplementedError


class FakeTranscriptionBackend(TranscriptionBackend):
    def fingerprint(self) -> dict[str, Any]:
        return {
            "backend": "fake",
            "runtime": "pocket-journal-partner",
            "runtime_version": None,
            "model_id": "fake",
            "decoding_parameters": {},
        }

    def transcribe(self, audio_path: Path) -> dict[str, Any]:
        recording_name = audio_path.stem.strip() or "recording"
        return {
            "model": "fake",
            "audio_file": audio_path.name,
            "text": f"Transcript for {recording_name}.",
            "created_at": datetime.now(timezone.utc).isoformat(),
        }


class HuggingFaceTranscriptionBackend(TranscriptionBackend):
    def __init__(
        self,
        model_id: str = MODEL_ID,
        model_revision: str = MODEL_REVISION,
        model_artifact_sha256: str | None = None,
        decoding_parameters: Mapping[str, Any] | None = None,
    ) -> None:
        if not model_revision.strip():
            raise ValueError("model_revision must pin an immutable model revision")
        if model_artifact_sha256 is not None:
            digest = model_artifact_sha256.lower()
            if len(digest) != 64 or any(character not in "0123456789abcdef" for character in digest):
                raise ValueError("model_artifact_sha256 must be a 64-character hexadecimal digest")
            model_artifact_sha256 = digest
        self.model_id = model_id
        self.model_revision = model_revision
        self.model_artifact_sha256 = model_artifact_sha256
        # A JSON round trip both validates and detaches caller-owned structures.
        self.decoding_parameters = json.loads(
            json.dumps(decoding_parameters or {}, sort_keys=True)
        )
        self._pipeline = None

    def fingerprint(self) -> dict[str, Any]:
        try:
            runtime_version: str | None = metadata.version("transformers")
        except metadata.PackageNotFoundError:
            runtime_version = None
        artifact_identity = {
            "algorithm": "sha256",
            "digest": self.model_artifact_sha256,
            "status": "verified" if self.model_artifact_sha256 is not None else "unavailable",
        }
        if self.model_artifact_sha256 is None:
            artifact_identity["reason"] = MODEL_ARTIFACT_DIGEST_UNAVAILABLE_REASON
        return {
            "backend": "huggingface",
            "runtime": "transformers",
            "runtime_version": runtime_version,
            "model_id": self.model_id,
            "model_revision": self.model_revision,
            "model_artifact": artifact_identity,
            "decoding_parameters": self.decoding_parameters,
        }

    def _load(self):
        if self._pipeline is None:
            try:
                from transformers import pipeline  # type: ignore
            except ImportError as exc:
                raise RuntimeError("Install transcription dependencies: pip install -e '.[transcription]'") from exc
            self._pipeline = pipeline(
                "automatic-speech-recognition",
                model=self.model_id,
                revision=self.model_revision,
            )
        return self._pipeline

    def transcribe(self, audio_path: Path) -> dict[str, Any]:
        pipe = self._load()
        result = pipe(str(audio_path), **self.decoding_parameters)
        text = result["text"] if isinstance(result, dict) and "text" in result else str(result)
        return {
            "model": self.model_id,
            "audio_file": audio_path.name,
            "text": text,
            "created_at": datetime.now(timezone.utc).isoformat(),
        }


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _whisper_cpp_text(
    payload: Any,
    *,
    allow_empty: bool = False,
) -> tuple[str, list[dict[str, Any]]]:
    if not isinstance(payload, dict):
        raise RuntimeError("whisper.cpp returned invalid JSON")
    segments: list[dict[str, Any]] = []
    raw_segments = payload.get("transcription")
    if not isinstance(raw_segments, list):
        raw_segments = payload.get("segments")
    if isinstance(raw_segments, list):
        for raw in raw_segments:
            if not isinstance(raw, dict) or not isinstance(raw.get("text"), str):
                continue
            raw_text = raw["text"].strip()
            if raw_text.casefold() == WHISPER_CPP_BLANK_AUDIO_SENTINEL.casefold():
                continue
            segment: dict[str, Any] = {"text": raw_text}
            timestamps = raw.get("timestamps")
            if isinstance(timestamps, dict):
                if isinstance(timestamps.get("from"), str):
                    segment["start"] = timestamps["from"]
                if isinstance(timestamps.get("to"), str):
                    segment["end"] = timestamps["to"]
            for source, target in (("t0", "start_ms"), ("t1", "end_ms")):
                value = raw.get(source)
                if isinstance(value, (int, float)) and not isinstance(value, bool):
                    segment[target] = int(value * 10)
            if segment["text"]:
                segments.append(segment)
    text = " ".join(segment["text"] for segment in segments).strip()
    if not text and isinstance(payload.get("text"), str):
        text = payload["text"].strip()
    if text.casefold() == WHISPER_CPP_BLANK_AUDIO_SENTINEL.casefold():
        text = ""
    if not text and not allow_empty:
        raise RuntimeError("whisper.cpp returned an empty transcript")
    return text, segments


def _whisper_cpp_has_blank_audio_sentinel(payload: Any) -> bool:
    if not isinstance(payload, dict):
        return False
    if (
        isinstance(payload.get("text"), str)
        and payload["text"].strip().casefold()
        == WHISPER_CPP_BLANK_AUDIO_SENTINEL.casefold()
    ):
        return True
    for key in ("transcription", "segments"):
        raw_segments = payload.get(key)
        if isinstance(raw_segments, list) and any(
            isinstance(raw, dict)
            and isinstance(raw.get("text"), str)
            and raw["text"].strip().casefold()
            == WHISPER_CPP_BLANK_AUDIO_SENTINEL.casefold()
            for raw in raw_segments
        ):
            return True
    return False


class WhisperCppTranscriptionBackend(TranscriptionBackend):
    """CPU-first whisper.cpp backend with an explicitly supplied local model."""

    def __init__(
        self,
        model_path: Path | str | None = None,
        executable: str | None = None,
        *,
        language: str = "en",
        threads: int | None = None,
    ) -> None:
        configured_model = model_path or os.environ.get(WHISPER_CPP_MODEL_ENV)
        self.model_path = Path(configured_model).expanduser() if configured_model else None
        self.executable_name = executable or os.environ.get(WHISPER_CPP_EXECUTABLE_ENV) or "whisper-cli"
        self.executable = shutil.which(self.executable_name)
        self.language = language
        self.threads = threads if threads is not None else WHISPER_CPP_DEFAULT_THREADS
        if threads is not None and threads <= 0:
            raise ValueError("whisper.cpp thread count must be positive")

    def availability(self, *, digest: bool = False) -> dict[str, Any]:
        issues: list[str] = []
        if self.executable is None:
            issues.append(
                f"whisper.cpp executable {self.executable_name!r} was not found on PATH; "
                f"set {WHISPER_CPP_EXECUTABLE_ENV} or pass --whisper-executable"
            )
        model_size = None
        model_digest = None
        if self.model_path is None:
            issues.append(
                f"no whisper.cpp model configured; set {WHISPER_CPP_MODEL_ENV} or pass --model "
                f"(recommended: {WHISPER_CPP_DEFAULT_MODEL})"
            )
        elif not self.model_path.is_file():
            issues.append(f"whisper.cpp model does not exist: {self.model_path}")
        else:
            model_size = self.model_path.stat().st_size
            if model_size > WHISPER_CPP_MAX_MODEL_BYTES:
                issues.append(
                    "whisper.cpp model exceeds the 2 GiB partner guardrail; use base.en or small.en "
                    "for the <=16 GB CPU profile"
                )
            if digest:
                model_digest = _sha256_file(self.model_path)
        return {
            "backend": "whisper-cpp",
            "available": not issues,
            "cpu_only": True,
            "cloud_required": False,
            "recommended_model": WHISPER_CPP_DEFAULT_MODEL,
            "executable": self.executable,
            "model_path": str(self.model_path) if self.model_path is not None else None,
            "model_bytes": model_size,
            "model_sha256": model_digest,
            "issues": issues,
        }

    def _require_available(self) -> tuple[str, Path]:
        status = self.availability()
        if not status["available"]:
            raise RuntimeError("; ".join(status["issues"]))
        assert self.executable is not None
        assert self.model_path is not None
        return self.executable, self.model_path

    def fingerprint(self) -> dict[str, Any]:
        executable, model_path = self._require_available()
        return {
            "backend": "whisper-cpp",
            "runtime": "whisper.cpp",
            "runtime_executable": str(Path(executable).resolve()),
            "runtime_artifact_sha256": _sha256_file(Path(executable).resolve()),
            "model_id": model_path.name,
            "model_artifact": {
                "algorithm": "sha256",
                "digest": _sha256_file(model_path),
                "bytes": model_path.stat().st_size,
                "status": "verified",
            },
            "decoding_parameters": {
                "task": "transcribe",
                "language": self.language,
                "threads": self.threads,
                "processors": 1,
                "beam_size": WHISPER_CPP_BEAM_SIZE,
                "best_of": WHISPER_CPP_BEST_OF,
                "temperature": WHISPER_CPP_TEMPERATURE,
                "temperature_increment": WHISPER_CPP_TEMPERATURE_INCREMENT,
                "no_speech_threshold": WHISPER_CPP_NO_SPEECH_THRESHOLD,
                "blank_audio_sentinel": WHISPER_CPP_BLANK_AUDIO_SENTINEL,
                "timestamps": "segment",
                "vad": False,
                "cpu_only": True,
            },
        }

    def build_command(
        self,
        audio_path: Path,
        output_prefix: Path,
        *,
        quiet: bool = True,
    ) -> list[str]:
        executable, model_path = self._require_available()
        command = [
            executable,
            "-m",
            str(model_path),
            "-f",
            str(audio_path),
            "-l",
            self.language,
            "-oj",
            "-of",
            str(output_prefix),
            "--no-gpu",
            "--beam-size",
            str(WHISPER_CPP_BEAM_SIZE),
            "--best-of",
            str(WHISPER_CPP_BEST_OF),
            "--temperature",
            str(WHISPER_CPP_TEMPERATURE),
            "--temperature-inc",
            str(WHISPER_CPP_TEMPERATURE_INCREMENT),
            "--no-speech-thold",
            str(WHISPER_CPP_NO_SPEECH_THRESHOLD),
        ]
        if quiet:
            command.append("--no-prints")
        command.extend(("-t", str(self.threads)))
        return command

    def transcribe(self, audio_path: Path) -> dict[str, Any]:
        _, model_path = self._require_available()
        with tempfile.TemporaryDirectory(prefix="pj-whisper-") as temporary:
            output_prefix = Path(temporary) / "transcript"
            completed = subprocess.run(
                self.build_command(audio_path, output_prefix),
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=24 * 60 * 60,
                check=False,
            )
            if completed.returncode != 0:
                detail = completed.stderr.strip().splitlines()[-1] if completed.stderr.strip() else "no diagnostic"
                raise RuntimeError(
                    f"whisper.cpp exited with status {completed.returncode}: {detail[:300]}"
                )
            output_path = output_prefix.with_suffix(".json")
            try:
                payload = json.loads(output_path.read_text(encoding="utf-8"))
            except FileNotFoundError as exc:
                raise RuntimeError("whisper.cpp did not create its JSON transcript") from exc
            except (UnicodeError, json.JSONDecodeError) as exc:
                raise RuntimeError("whisper.cpp created an invalid JSON transcript") from exc
        blank_audio = _whisper_cpp_has_blank_audio_sentinel(payload)
        text, segments = _whisper_cpp_text(payload, allow_empty=blank_audio)
        no_speech = blank_audio and not text
        if no_speech:
            text = WHISPER_CPP_NO_SPEECH_TEXT
        result: dict[str, Any] = {
            "model": model_path.name,
            "runtime": "whisper.cpp",
            "audio_file": audio_path.name,
            "text": text,
            "created_at": datetime.now(timezone.utc).isoformat(),
            "language": self.language,
            "no_speech": no_speech,
        }
        if segments:
            result["segments"] = segments
        return result

def backend_from_name(
    name: str,
    *,
    model_path: Path | str | None = None,
    executable: str | None = None,
    threads: int | None = None,
) -> TranscriptionBackend:
    if name == "fake":
        return FakeTranscriptionBackend()
    if name == "hf":
        return HuggingFaceTranscriptionBackend()
    if name == "whisper-cpp":
        return WhisperCppTranscriptionBackend(
            model_path=model_path,
            executable=executable,
            threads=threads,
        )
    raise ValueError(f"unknown transcription backend: {name}")
