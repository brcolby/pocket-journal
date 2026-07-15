from __future__ import annotations

from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable
import hashlib
import json
import tempfile

from .device import DeviceClient, DeviceHTTPError, SerialDeviceClient
from .library import NoteLibrary
from .storage import PartnerStore
from .transcript_payload import DEVICE_TRANSCRIPT_MAX_BYTES, build_device_transcript_payload
from .transcription import FakeTranscriptionBackend, TranscriptionBackend, inspect_wav


JOB_SCHEMA_VERSION = 2
TRANSCRIPT_SCHEMA_VERSION = 2
class PermanentAudioError(ValueError):
    pass


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
        "item_identity": None,
        "source": None,
        "transcription_fingerprint": None,
        "cache_key": None,
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
    cache_key = _cache_key(source, fingerprint)
    return (
        isinstance(sync_metadata, dict)
        and sync_metadata.get("schema_version") == TRANSCRIPT_SCHEMA_VERSION
        and sync_metadata.get("source") == source
        and sync_metadata.get("transcription_fingerprint") == fingerprint
        and sync_metadata.get("cache_key") == cache_key
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
        "cache_key": _cache_key(source, fingerprint),
    }
    return prepared


def _json_bytes(payload: dict[str, Any]) -> bytes:
    return json.dumps(payload, ensure_ascii=False, separators=(",", ":"), sort_keys=True).encode(
        "utf-8"
    )


def _cache_key(source: dict[str, Any], fingerprint: dict[str, Any]) -> str:
    identity = {
        "source": {
            "sha256": source["sha256"],
            "bytes": source["bytes"],
        },
        "transcription_fingerprint": fingerprint,
    }
    return hashlib.sha256(_json_bytes(identity)).hexdigest()


def _verified_cached_source(
    store: PartnerStore, device_id: str, item: Any
) -> dict[str, Any] | None:
    path = store.audio_path(device_id, item.audio_id, item.filename)
    if not path.exists():
        return None
    try:
        metadata = inspect_wav(path)
    except (OSError, ValueError):
        return None
    if item.size is not None and metadata["bytes"] != item.size:
        return None
    if item.data_bytes is not None and metadata["data_bytes"] != item.data_bytes:
        return None
    expected_sha256 = getattr(item, "source_sha256", None)
    if expected_sha256 is not None and metadata["sha256"] != expected_sha256:
        return None
    return _source_identity(metadata)


def _download_and_inspect(
    client: DeviceClient | SerialDeviceClient,
    store: PartnerStore,
    device_id: str,
    item: Any,
    *,
    force_download: bool = False,
) -> tuple[Path, dict[str, Any]]:
    audio_path = store.audio_path(device_id, item.audio_id, item.filename)
    metadata: dict[str, Any] | None = None
    if audio_path.exists() and not force_download:
        try:
            metadata = inspect_wav(audio_path)
        except (OSError, ValueError):
            metadata = None
        if metadata is not None and item.size is not None and metadata["bytes"] != item.size:
            metadata = None
        expected_sha256 = getattr(item, "source_sha256", None)
        if metadata is not None and expected_sha256 is not None:
            if metadata["sha256"] != expected_sha256:
                metadata = None

    if metadata is None:
        audio_path.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".download-", dir=audio_path.parent) as tmp:
            downloaded = client.download_audio(item, Path(tmp))
            downloaded_sha256 = hashlib.sha256(downloaded.read_bytes()).hexdigest()
            try:
                metadata = inspect_wav(downloaded)
            except ValueError as error:
                expected_sha256 = getattr(item, "source_sha256", None)
                if expected_sha256 is not None and downloaded_sha256 == expected_sha256:
                    raise PermanentAudioError(str(error)) from error
                raise
            if item.size is not None and metadata["bytes"] != item.size:
                raise ValueError(
                    f"downloaded WAV size mismatch: expected {item.size} bytes, "
                    f"got {metadata['bytes']}"
                )
            if item.data_bytes is not None and metadata["data_bytes"] != item.data_bytes:
                raise ValueError(
                    "downloaded WAV data length mismatch: "
                    f"expected {item.data_bytes} bytes, got {metadata['data_bytes']}"
                )
            expected_sha256 = getattr(item, "source_sha256", None)
            if expected_sha256 is not None and metadata["sha256"] != expected_sha256:
                raise ValueError(
                    "downloaded WAV digest mismatch: "
                    f"expected {expected_sha256}, got {metadata['sha256']}"
                )
            downloaded.replace(audio_path)

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
    retryable = operation != "prepare_upload"
    if isinstance(error, PermanentAudioError):
        retryable = False
    if isinstance(error, DeviceHTTPError):
        retryable = error.retryable
    if operation == "transcribe" and isinstance(error, ValueError):
        retryable = False
    job["last_error"] = {
        "operation": operation,
        "type": error.__class__.__name__,
        "message": message,
        "retryable": retryable,
        "at": _now(),
    }
    try:
        _save_job(store, device_id, audio_id, job)
    except Exception as save_error:
        message = f"{message}; failed to save job state: {save_error}"
    return {
        "device_id": device_id,
        "audio_id": audio_id,
        "status": "failed",
        "stage": job["stage"],
        "operation": operation,
        "retryable": retryable,
        "error": message,
    }


def _item_identity(item: Any) -> dict[str, Any]:
    return {
        "filename": item.filename,
        "size": item.size,
        "data_bytes": item.data_bytes,
        "source_sha256": getattr(item, "source_sha256", None),
    }


def _cached_permanent_failure(
    job: dict[str, Any], item: Any, fingerprint: dict[str, Any]
) -> dict[str, Any] | None:
    error = job.get("last_error")
    if not isinstance(error, dict) or error.get("retryable") is not False:
        return None
    if job.get("item_identity") != _item_identity(item):
        return None
    if error.get("operation") == "transcribe" and job.get("transcription_fingerprint") != fingerprint:
        return None
    return {
        "device_id": job.get("device_id"),
        "audio_id": job.get("audio_id"),
        "status": "failed",
        "stage": job.get("stage", "discovered"),
        "operation": error.get("operation", "unknown"),
        "retryable": False,
        "error": error.get("message", "permanent failure"),
        "cached": True,
    }
def _sync_audio_item(
    device_id: str,
    client: DeviceClient | SerialDeviceClient,
    store: PartnerStore,
    library: NoteLibrary,
    backend: TranscriptionBackend,
    fingerprint: dict[str, Any],
    item: Any,
    upload_transcripts: bool = True,
    reprocess_synced: bool = False,
) -> dict[str, Any] | None:
    job = _new_job(device_id, item.audio_id, item.filename)
    operation = "index_note"
    try:
        note = library.upsert_discovered(
            device_id,
            item.audio_id,
            item.filename,
            label=getattr(item, "label", None),
            source_sha256=getattr(item, "source_sha256", None),
            created_at=getattr(item, "created_at", None),
            duration_ms=getattr(item, "duration_ms", None),
            device_synced=bool(item.synced or item.transcript_uploaded),
        )
        operation = "load_state"
        completed_job = store.load_job(device_id, item.audio_id)
        if item.synced or item.transcript_uploaded:
            completed_transcript = store.load_transcript(device_id, item.audio_id)
            completed_source = (
                completed_job.get("source") if isinstance(completed_job, dict) else None
            )
            cached_source = _verified_cached_source(store, device_id, item)
            if not reprocess_synced and (
                isinstance(completed_job, dict)
                and completed_job.get("audio_id") == item.audio_id
                and completed_job.get("stage") == "uploaded"
                and isinstance(completed_source, dict)
                and cached_source == completed_source
                and _transcript_matches(completed_transcript, cached_source, fingerprint)
            ):
                job = completed_job
                completed_job["last_error"] = None
                _save_job(store, device_id, item.audio_id, completed_job)
                cached_path = store.audio_path(device_id, item.audio_id, item.filename)
                if cached_path.is_file():
                    library.attach_audio(
                        note.note_id,
                        cached_path,
                        source_sha256=cached_source.get("sha256"),
                    )
                assert completed_transcript is not None
                library.attach_transcript(note.note_id, completed_transcript)
                return None
            if not reprocess_synced and not isinstance(completed_job, dict):
                return None

        job = _load_job(store, device_id, item.audio_id, item.filename)
        cached_failure = _cached_permanent_failure(job, item, fingerprint)
        if cached_failure is not None:
            return cached_failure
        job["attempt_count"] = int(job.get("attempt_count", 0)) + 1
        job["item_identity"] = _item_identity(item)
        job["stage"] = "discovered"
        job["source"] = None
        job["transcription_fingerprint"] = None
        job["cache_key"] = None
        job["last_error"] = None
        operation = "save_job"
        _save_job(store, device_id, item.audio_id, job)

        operation = "download_audio"
        audio_path, audio_metadata = _download_and_inspect(
            client,
            store,
            device_id,
            item,
            force_download=reprocess_synced and (item.synced or item.transcript_uploaded),
        )
        source = _source_identity(audio_metadata)
        job["stage"] = "audio_verified"
        job["source"] = source
        job["cache_key"] = _cache_key(source, fingerprint)
        _save_job(store, device_id, item.audio_id, job)
        operation = "index_audio"
        library.attach_audio(note.note_id, audio_path, source_sha256=source["sha256"])

        operation = "transcribe"
        job["transcription_fingerprint"] = fingerprint
        transcript = store.load_transcript(device_id, item.audio_id)
        if not _transcript_matches(transcript, source, fingerprint):
            transcript = _prepare_transcript(backend.transcribe(audio_path), source, fingerprint)
            store.save_transcript(device_id, item.audio_id, transcript)
        job["stage"] = "transcribed"
        _save_job(store, device_id, item.audio_id, job)
        operation = "index_transcript"
        library.attach_transcript(note.note_id, transcript)

        if upload_transcripts:
            operation = "prepare_upload"
            device_payload = build_device_transcript_payload(
                transcript, source, fingerprint, title=note.title
            )
            operation = "upload_transcript"
            client.upload_transcript(item.audio_id, device_payload)
            job["stage"] = "uploaded"
        else:
            job["stage"] = "transcribed"
        job["last_error"] = None
        _save_job(store, device_id, item.audio_id, job)
        operation = "index_note"
        library.upsert_discovered(
            device_id,
            item.audio_id,
            item.filename,
            label=getattr(item, "label", None),
            source_sha256=source["sha256"],
            created_at=getattr(item, "created_at", None),
            duration_ms=getattr(item, "duration_ms", None),
            device_synced=upload_transcripts,
        )

        entry = {
            "device_id": device_id,
            "audio_id": item.audio_id,
            "audio_file": str(audio_path),
            "status": "uploaded" if upload_transcripts else "transcribed",
            "source_sha256": source["sha256"],
            "synced_at": _now(),
            "model": transcript.get("model", ""),
            "uploaded": upload_transcripts,
        }
        operation = "append_sync_log"
        store.append_sync_log(entry)
        return entry
    except Exception as error:
        if operation == "download_audio" and isinstance(error, ValueError):
            operation = "verify_audio"
        return _failure_result(store, device_id, item.audio_id, job, operation, error)


def sync_device_audio(
    device_id: str,
    client: DeviceClient | SerialDeviceClient,
    store: PartnerStore,
    backend: TranscriptionBackend,
    *,
    upload_transcripts: bool = True,
    reprocess_synced: bool = False,
    library: NoteLibrary | None = None,
    progress: Callable[[dict[str, Any]], None] | None = None,
) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    if isinstance(backend, FakeTranscriptionBackend):
        upload_transcripts = False
    library = library or NoteLibrary(store.root)
    fingerprint = backend.fingerprint()
    with store.workflow_lock(device_id, "__device_sync__"):
        items = client.list_audio()
        if progress is not None:
            progress({"event": "listed", "total": len(items)})
        for index, item in enumerate(items):
            if progress is not None:
                progress({
                    "event": "item_started",
                    "index": index,
                    "total": len(items),
                    "audio_id": item.audio_id,
                })
            with store.workflow_lock(device_id, item.audio_id):
                result = _sync_audio_item(
                    device_id,
                    client,
                    store,
                    library,
                    backend,
                    fingerprint,
                    item,
                    upload_transcripts,
                    reprocess_synced,
                )
            if result is not None:
                results.append(result)
            if progress is not None:
                progress({
                    "event": "item_complete",
                    "index": index,
                    "total": len(items),
                    "audio_id": item.audio_id,
                    "status": "skipped" if result is None else result.get("status", "failed"),
                })
        if progress is not None:
            progress({"event": "complete", "total": len(items)})
    return results
