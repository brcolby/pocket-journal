from __future__ import annotations

from pathlib import Path
from typing import Any
from urllib.parse import quote
import json
import os
import re
import tempfile

from .config import default_data_dir


_SAFE_SUFFIX = re.compile(r"\.[A-Za-z0-9]{1,16}\Z")


def _path_component(value: str) -> str:
    """Encode an identifier as one non-traversing path component."""
    encoded = quote(value, safe="")
    if not encoded:
        return "%EMPTY"
    if encoded in {".", ".."}:
        return encoded.replace(".", "%2E")
    return encoded


def _audio_filename(audio_id: str, filename: str) -> str:
    encoded_id = _path_component(audio_id)
    suffix = Path(filename).suffix
    safe_suffix = suffix if _SAFE_SUFFIX.fullmatch(suffix) else ""
    return f"{len(encoded_id)}-{encoded_id}{safe_suffix}"


class PartnerStore:
    def __init__(self, root: Path | None = None) -> None:
        self.root = root or default_data_dir()

    def audio_dir(self, device_id: str) -> Path:
        return self.root / "audio" / _path_component(device_id)

    def audio_path(self, device_id: str, audio_id: str, filename: str) -> Path:
        return self.audio_dir(device_id) / _audio_filename(audio_id, filename)

    def transcript_dir(self, device_id: str) -> Path:
        return self.root / "transcripts" / _path_component(device_id)

    def transcript_path(self, device_id: str, audio_id: str) -> Path:
        return self.transcript_dir(device_id) / f"{quote(audio_id, safe='')}.json"

    def job_dir(self, device_id: str) -> Path:
        return self.root / "jobs" / _path_component(device_id)

    def job_path(self, device_id: str, audio_id: str) -> Path:
        return self.job_dir(device_id) / f"{_path_component(audio_id)}.json"

    def save_transcript(self, device_id: str, audio_id: str, transcript: dict[str, Any]) -> Path:
        path = self.transcript_path(device_id, audio_id)
        self._save_json(path, transcript)
        return path

    def load_transcript(self, device_id: str, audio_id: str) -> dict[str, Any] | None:
        return self._load_json(self.transcript_path(device_id, audio_id))

    def save_job(self, device_id: str, audio_id: str, job: dict[str, Any]) -> Path:
        path = self.job_path(device_id, audio_id)
        self._save_json(path, job)
        return path

    def load_job(self, device_id: str, audio_id: str) -> dict[str, Any] | None:
        return self._load_json(self.job_path(device_id, audio_id))

    @staticmethod
    def _save_json(path: Path, payload: dict[str, Any]) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        serialized = json.dumps(payload, indent=2, sort_keys=True) + "\n"
        temp_path: Path | None = None
        try:
            with tempfile.NamedTemporaryFile(
                "w",
                encoding="utf-8",
                dir=path.parent,
                prefix=f".{path.name}.",
                delete=False,
            ) as handle:
                temp_path = Path(handle.name)
                handle.write(serialized)
                handle.flush()
                os.fsync(handle.fileno())
            temp_path.replace(path)
        finally:
            if temp_path is not None and temp_path.exists():
                temp_path.unlink()

    @staticmethod
    def _load_json(path: Path) -> dict[str, Any] | None:
        if not path.exists():
            return None
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError):
            return None
        return payload if isinstance(payload, dict) else None

    def append_sync_log(self, entry: dict[str, Any]) -> None:
        self.root.mkdir(parents=True, exist_ok=True)
        with (self.root / "sync-log.jsonl").open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(entry, sort_keys=True) + "\n")
