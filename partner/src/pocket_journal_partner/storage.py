from __future__ import annotations

from pathlib import Path
from typing import Any
import json

from .config import default_data_dir


class PartnerStore:
    def __init__(self, root: Path | None = None) -> None:
        self.root = root or default_data_dir()

    def audio_dir(self, device_id: str) -> Path:
        return self.root / "audio" / device_id

    def transcript_dir(self, device_id: str) -> Path:
        return self.root / "transcripts" / device_id

    def save_transcript(self, device_id: str, audio_id: str, transcript: dict[str, Any]) -> Path:
        target = self.transcript_dir(device_id)
        target.mkdir(parents=True, exist_ok=True)
        path = target / f"{audio_id}.json"
        path.write_text(json.dumps(transcript, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        return path

    def append_sync_log(self, entry: dict[str, Any]) -> None:
        self.root.mkdir(parents=True, exist_ok=True)
        with (self.root / "sync-log.jsonl").open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(entry, sort_keys=True) + "\n")

