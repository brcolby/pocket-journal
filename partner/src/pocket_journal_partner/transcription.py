from __future__ import annotations

from abc import ABC, abstractmethod
from datetime import datetime, timezone
import hashlib
from importlib import metadata
import json
from pathlib import Path
import struct
from typing import Any, Mapping

MODEL_ID = "distil-whisper/distil-large-v3.5"


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

    format_fields: tuple[int, int, int, int] | None = None
    data_bytes = 0
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
            audio_format, channels, sample_rate = struct.unpack_from("<HHI", blob, payload_start)
            bits_per_sample = struct.unpack_from("<H", blob, payload_start + 14)[0]
            if channels == 0 or sample_rate == 0 or bits_per_sample == 0:
                raise ValueError("invalid WAV format values")
            format_fields = audio_format, channels, sample_rate, bits_per_sample
        elif chunk_id == b"data":
            data_bytes += chunk_size

        offset = padded_end

    if format_fields is None:
        raise ValueError("WAV format chunk is missing")
    if data_bytes == 0:
        raise ValueError("WAV data chunk is missing or empty")

    audio_format, channels, sample_rate, bits_per_sample = format_fields
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
        decoding_parameters: Mapping[str, Any] | None = None,
    ) -> None:
        self.model_id = model_id
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
        return {
            "backend": "huggingface",
            "runtime": "transformers",
            "runtime_version": runtime_version,
            "model_id": self.model_id,
            "decoding_parameters": self.decoding_parameters,
        }

    def _load(self):
        if self._pipeline is None:
            try:
                from transformers import pipeline  # type: ignore
            except ImportError as exc:
                raise RuntimeError("Install transcription dependencies: pip install -e '.[transcription]'") from exc
            self._pipeline = pipeline("automatic-speech-recognition", model=self.model_id)
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


def backend_from_name(name: str) -> TranscriptionBackend:
    if name == "fake":
        return FakeTranscriptionBackend()
    if name == "hf":
        return HuggingFaceTranscriptionBackend()
    raise ValueError(f"unknown transcription backend: {name}")
