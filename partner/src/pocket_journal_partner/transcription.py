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
MODEL_REVISION = "728a7691f3ff1d3d971528d3203a6e9559165d41"
DEVICE_SAMPLE_RATE = 16_000
DEVICE_CHANNELS = 1
DEVICE_BITS_PER_SAMPLE = 16
MODEL_ARTIFACT_DIGEST_UNAVAILABLE_REASON = (
    "no canonical SHA-256 exists for the multi-file Hugging Face snapshot; "
    "model_revision pins its immutable contents"
)


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


def backend_from_name(name: str) -> TranscriptionBackend:
    if name == "fake":
        return FakeTranscriptionBackend()
    if name == "hf":
        return HuggingFaceTranscriptionBackend()
    raise ValueError(f"unknown transcription backend: {name}")
