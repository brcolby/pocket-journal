from __future__ import annotations

from datetime import datetime, timezone
from typing import Any

from .device import DeviceClient
from .storage import PartnerStore
from .transcription import TranscriptionBackend


def sync_device_audio(
    device_id: str,
    client: DeviceClient,
    store: PartnerStore,
    backend: TranscriptionBackend,
) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    for item in client.list_audio():
        audio_path = client.download_audio(item, store.audio_dir(device_id))
        transcript = backend.transcribe(audio_path)
        store.save_transcript(device_id, item.audio_id, transcript)
        client.upload_transcript(item.audio_id, transcript)
        entry = {
            "device_id": device_id,
            "audio_id": item.audio_id,
            "audio_file": str(audio_path),
            "synced_at": datetime.now(timezone.utc).isoformat(),
            "model": transcript.get("model", ""),
        }
        store.append_sync_log(entry)
        results.append(entry)
    return results

