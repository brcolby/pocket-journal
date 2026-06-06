from __future__ import annotations

from abc import ABC, abstractmethod
from pathlib import Path
from typing import Any
from datetime import datetime, timezone

MODEL_ID = "distil-whisper/distil-large-v3.5"


class TranscriptionBackend(ABC):
    @abstractmethod
    def transcribe(self, audio_path: Path) -> dict[str, Any]:
        raise NotImplementedError


class FakeTranscriptionBackend(TranscriptionBackend):
    def transcribe(self, audio_path: Path) -> dict[str, Any]:
        return {
            "model": "fake",
            "audio_file": audio_path.name,
            "text": "",
            "created_at": datetime.now(timezone.utc).isoformat(),
        }


class HuggingFaceTranscriptionBackend(TranscriptionBackend):
    def __init__(self, model_id: str = MODEL_ID) -> None:
        self.model_id = model_id
        self._pipeline = None

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
        result = pipe(str(audio_path))
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

