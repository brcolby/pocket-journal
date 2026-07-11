from __future__ import annotations

from pathlib import Path
from typing import Any
from urllib.parse import quote
import json
import tempfile

from .config import default_data_dir


class PartnerStore:
    def __init__(self, root: Path | None = None) -> None:
        self.root = root or default_data_dir()

    def audio_dir(self, device_id: str) -> Path:
        return self.root / "audio" / quote(device_id, safe="")

    def transcript_dir(self, device_id: str) -> Path:
        return self.root / "transcripts" / quote(device_id, safe="")

    def transcript_path(self, device_id: str, audio_id: str) -> Path:
        return self.transcript_dir(device_id) / f"{quote(audio_id, safe='')}.json"

    def save_transcript(self, device_id: str, audio_id: str, transcript: dict[str, Any]) -> Path:
        target = self.transcript_dir(device_id)
        target.mkdir(parents=True, exist_ok=True)
        path = self.transcript_path(device_id, audio_id)
        serialized = json.dumps(transcript, indent=2, sort_keys=True) + "\n"
        temp_path: Path | None = None
        try:
            with tempfile.NamedTemporaryFile(
                "w",
                encoding="utf-8",
                dir=target,
                prefix=f".{path.name}.",
                delete=False,
            ) as handle:
                temp_path = Path(handle.name)
                handle.write(serialized)
            temp_path.replace(path)
        finally:
            if temp_path is not None and temp_path.exists():
                temp_path.unlink()
        return path

    def load_transcript(self, device_id: str, audio_id: str) -> dict[str, Any] | None:
        path = self.transcript_path(device_id, audio_id)
        if not path.exists():
            return None
        payload = json.loads(path.read_text(encoding="utf-8"))
        return payload if isinstance(payload, dict) else None

    def append_sync_log(self, entry: dict[str, Any]) -> None:
        self.root.mkdir(parents=True, exist_ok=True)
        with (self.root / "sync-log.jsonl").open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(entry, sort_keys=True) + "\n")
