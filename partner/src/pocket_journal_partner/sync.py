from __future__ import annotations

from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .device import DeviceClient
from .storage import PartnerStore
from .transcription import TranscriptionBackend, inspect_wav


JOB_SCHEMA_VERSION = 1
TRANSCRIPT_SCHEMA_VERSION = 1


def _now() -> str:
    return datetime.now(timezone.utc).isoformat()


def _new_job(device_id: str, audio_id: str, filename: str) -> dict[str, Any]:
    return {
        "schema_version": JOB_SCHEMA_VERSION,
        "device_id": device_id,
        "audio_id": audio_id,
        "filename": filename,
        "stage": "discovered",
        "attempt_count": 0,
        "source": None,
        "transcription_fingerprint": None,
        "last_error": None,
        "updated_at": _now(),
    }


def _load_job(store: PartnerStore, device_id: str, audio_id: str, filename: str) -> dict[str, Any]:
    job = store.load_job(device_id, audio_id)
    if (
        not isinstance(job, dict)
        or job.get("schema_version") != JOB_SCHEMA_VERSION
        or job.get("device_id") != device_id
        or job.get("audio_id") != audio_id
    ):
        return _new_job(device_id, audio_id, filename)
    job["filename"] = filename
    return job


def _save_job(store: PartnerStore, device_id: str, audio_id: str, job: dict[str, Any]) -> None:
    job["updated_at"] = _now()
    store.save_job(device_id, audio_id, job)


def _source_identity(metadata: dict[str, Any]) -> dict[str, Any]:
    return {
        "sha256": metadata["sha256"],
        "bytes": metadata["bytes"],
        "data_bytes": metadata["data_bytes"],
        "sample_rate": metadata["sample_rate"],
        "channels": metadata["channels"],
        "sample_width_bits": metadata["sample_width_bits"],
    }


def _transcript_matches(
    transcript: dict[str, Any] | None,
    source: dict[str, Any],
    fingerprint: dict[str, Any],
) -> bool:
    if not isinstance(transcript, dict):
        return False
    if not isinstance(transcript.get("text"), str) or not transcript["text"].strip():
        return False
    sync_metadata = transcript.get("sync")
    return (
        isinstance(sync_metadata, dict)
        and sync_metadata.get("schema_version") == TRANSCRIPT_SCHEMA_VERSION
        and sync_metadata.get("source") == source
        and sync_metadata.get("transcription_fingerprint") == fingerprint
    )


def _prepare_transcript(
    transcript: dict[str, Any],
    source: dict[str, Any],
    fingerprint: dict[str, Any],
) -> dict[str, Any]:
    if not isinstance(transcript, dict):
        raise ValueError("transcription backend returned a non-object result")
    text = transcript.get("text")
    if not isinstance(text, str) or not text.strip():
        raise ValueError("transcription backend returned empty text")
    prepared = dict(transcript)
    prepared["text"] = text.strip()
    prepared["sync"] = {
        "schema_version": TRANSCRIPT_SCHEMA_VERSION,
        "source": source,
        "transcription_fingerprint": fingerprint,
    }
    return prepared


def _download_and_inspect(
    client: DeviceClient,
    store: PartnerStore,
    device_id: str,
    item: Any,
) -> tuple[Path, dict[str, Any]]:
    audio_path = store.audio_path(device_id, item.audio_id, item.filename)
    metadata: dict[str, Any] | None = None
    if audio_path.exists():
        try:
            metadata = inspect_wav(audio_path)
        except (OSError, ValueError):
            audio_path.unlink()
        if metadata is not None and item.size is not None and metadata["bytes"] != item.size:
            audio_path.unlink()
            metadata = None

    if metadata is None:
        downloaded = client.download_audio(item, audio_path.parent)
        audio_path.parent.mkdir(parents=True, exist_ok=True)
        if downloaded != audio_path:
            downloaded.replace(audio_path)
        metadata = inspect_wav(audio_path)

    if item.size is not None and metadata["bytes"] != item.size:
        raise ValueError(
            f"downloaded WAV size mismatch: expected {item.size} bytes, got {metadata['bytes']}"
        )
    if item.data_bytes is not None and metadata["data_bytes"] != item.data_bytes:
        raise ValueError(
            "downloaded WAV data length mismatch: "
            f"expected {item.data_bytes} bytes, got {metadata['data_bytes']}"
        )
    return audio_path, metadata


def _failure_result(
    store: PartnerStore,
    device_id: str,
    audio_id: str,
    job: dict[str, Any],
    operation: str,
    error: Exception,
) -> dict[str, Any]:
    message = str(error).strip() or error.__class__.__name__
    retryable = operation != "verify_audio"
    job["last_error"] = {
        "operation": operation,
        "type": error.__class__.__name__,
        "message": message,
        "retryable": retryable,
        "at": _now(),
    }
    _save_job(store, device_id, audio_id, job)
    return {
        "device_id": device_id,
        "audio_id": audio_id,
        "status": "failed",
        "stage": job["stage"],
        "operation": operation,
        "retryable": retryable,
        "error": message,
    }


def sync_device_audio(
    device_id: str,
    client: DeviceClient,
    store: PartnerStore,
    backend: TranscriptionBackend,
) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    for item in client.list_audio():
        if item.synced or item.transcript_uploaded:
            completed_job = store.load_job(device_id, item.audio_id)
            if isinstance(completed_job, dict) and completed_job.get("audio_id") == item.audio_id:
                completed_job["stage"] = "uploaded"
                completed_job["last_error"] = None
                _save_job(store, device_id, item.audio_id, completed_job)
            continue

        job = _load_job(store, device_id, item.audio_id, item.filename)
        job["attempt_count"] = int(job.get("attempt_count", 0)) + 1
        job["stage"] = "discovered"
        job["source"] = None
        job["transcription_fingerprint"] = None
        job["last_error"] = None
        operation = "download_audio"
        _save_job(store, device_id, item.audio_id, job)
        try:
            audio_path, audio_metadata = _download_and_inspect(client, store, device_id, item)
            source = _source_identity(audio_metadata)
            job["stage"] = "audio_verified"
            job["source"] = source
            _save_job(store, device_id, item.audio_id, job)

            operation = "transcribe"
            fingerprint = backend.fingerprint()
            transcript = store.load_transcript(device_id, item.audio_id)
            if not _transcript_matches(transcript, source, fingerprint):
                transcript = _prepare_transcript(backend.transcribe(audio_path), source, fingerprint)
                store.save_transcript(device_id, item.audio_id, transcript)
            job["stage"] = "transcribed"
            job["transcription_fingerprint"] = fingerprint
            _save_job(store, device_id, item.audio_id, job)

            operation = "upload_transcript"
            client.upload_transcript(item.audio_id, transcript)
            job["stage"] = "uploaded"
            job["last_error"] = None
            _save_job(store, device_id, item.audio_id, job)

            entry = {
                "device_id": device_id,
                "audio_id": item.audio_id,
                "audio_file": str(audio_path),
                "status": "uploaded",
                "source_sha256": source["sha256"],
                "synced_at": _now(),
                "model": transcript.get("model", ""),
            }
            operation = "append_sync_log"
            store.append_sync_log(entry)
            results.append(entry)
        except Exception as error:
            if operation == "download_audio" and isinstance(error, ValueError):
                operation = "verify_audio"
            results.append(
                _failure_result(store, device_id, item.audio_id, job, operation, error)
            )
    return results
