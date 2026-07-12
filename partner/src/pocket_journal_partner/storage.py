from __future__ import annotations

from contextlib import contextmanager
import errno
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import threading
import time
from typing import Any
from urllib.parse import quote

if os.name == "nt":
    import msvcrt
else:
    import fcntl

from .config import default_data_dir


_SAFE_SUFFIX = re.compile(r"\.[A-Za-z0-9]{1,16}\Z")
_MAX_COMPONENT_LENGTH = 180
_DIGEST_LENGTH = 64
_thread_locks: dict[str, threading.Lock] = {}
_thread_locks_guard = threading.Lock()


def _path_component(value: str) -> str:
    """Encode an identifier as one non-traversing, case-stable path component."""
    encoded = quote(value, safe="")
    digest = hashlib.sha256(value.encode("utf-8", "surrogatepass")).hexdigest()[
        :_DIGEST_LENGTH
    ]
    preview_length = _MAX_COMPONENT_LENGTH - len(digest) - 1
    return f"{digest}-{(encoded or '%EMPTY')[:preview_length]}"


def _audio_filename(audio_id: str, filename: str) -> str:
    encoded_id = _path_component(audio_id)
    suffix = Path(filename).suffix
    safe_suffix = suffix if _SAFE_SUFFIX.fullmatch(suffix) else ""
    stem_length = _MAX_COMPONENT_LENGTH - len(safe_suffix)
    return f"{encoded_id[:stem_length]}{safe_suffix}"


def _legacy_path_component(value: str) -> str:
    encoded = quote(value, safe="")
    digest = hashlib.sha256(value.encode("utf-8", "surrogatepass")).hexdigest()[:16]
    return f"{digest}-{encoded or '%EMPTY'}"


def _legacy_audio_filename(audio_id: str, filename: str) -> str:
    encoded_id = _legacy_path_component(audio_id)
    suffix = Path(filename).suffix
    safe_suffix = suffix if _SAFE_SUFFIX.fullmatch(suffix) else ""
    return f"{len(encoded_id)}-{encoded_id}{safe_suffix}"


def _thread_lock(path: Path) -> threading.Lock:
    key = os.path.abspath(os.fspath(path))
    with _thread_locks_guard:
        return _thread_locks.setdefault(key, threading.Lock())


def _acquire_windows_lock(handle) -> None:
    while True:
        try:
            msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            return
        except OSError as error:
            if error.errno not in {errno.EACCES, errno.EAGAIN, errno.EDEADLK}:
                raise
            time.sleep(0.05)


@contextmanager
def _file_lock(lock_path: Path):
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    local_lock = _thread_lock(lock_path)
    with local_lock, lock_path.open("a+b") as handle:
        if os.name == "nt":
            handle.seek(0, os.SEEK_END)
            if handle.tell() == 0:
                handle.write(b"\0")
                handle.flush()
            handle.seek(0)
            _acquire_windows_lock(handle)
        else:
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            if os.name == "nt":
                handle.seek(0)
                msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(handle.fileno(), fcntl.LOCK_UN)


@contextmanager
def _item_lock(path: Path):
    """Serialize a persisted item across threads and local processes."""
    lock_name = hashlib.sha256(path.name.encode("utf-8")).hexdigest() + ".lock"
    with _file_lock(path.parent / ".locks" / lock_name):
        yield


def _migrate_legacy_file(legacy_path: Path, path: Path) -> None:
    try:
        legacy_exists = legacy_path.exists()
    except OSError as error:
        if error.errno in {errno.ENAMETOOLONG, errno.EINVAL}:
            return
        raise
    if path.exists() or not legacy_exists:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with _item_lock(path):
        try:
            if not path.exists() and legacy_path.exists():
                legacy_path.replace(path)
        except OSError as error:
            if error.errno not in {errno.ENAMETOOLONG, errno.EINVAL}:
                raise


class PartnerStore:
    def __init__(self, root: Path | None = None) -> None:
        self.root = root or default_data_dir()

    def audio_dir(self, device_id: str) -> Path:
        return self.root / "audio" / _path_component(device_id)

    def audio_path(self, device_id: str, audio_id: str, filename: str) -> Path:
        path = self.audio_dir(device_id) / _audio_filename(audio_id, filename)
        legacy_path = (
            self.root
            / "audio"
            / _legacy_path_component(device_id)
            / _legacy_audio_filename(audio_id, filename)
        )
        _migrate_legacy_file(legacy_path, path)
        return path

    def transcript_dir(self, device_id: str) -> Path:
        return self.root / "transcripts" / _path_component(device_id)

    def transcript_path(self, device_id: str, audio_id: str) -> Path:
        component = _path_component(audio_id)
        path = self.transcript_dir(device_id) / f"{component[:_MAX_COMPONENT_LENGTH - 5]}.json"
        legacy_path = (
            self.root
            / "transcripts"
            / _legacy_path_component(device_id)
            / f"{_legacy_path_component(audio_id)}.json"
        )
        _migrate_legacy_file(legacy_path, path)
        return path

    def job_dir(self, device_id: str) -> Path:
        return self.root / "jobs" / _path_component(device_id)

    def job_path(self, device_id: str, audio_id: str) -> Path:
        component = _path_component(audio_id)
        path = self.job_dir(device_id) / f"{component[:_MAX_COMPONENT_LENGTH - 5]}.json"
        legacy_path = (
            self.root
            / "jobs"
            / _legacy_path_component(device_id)
            / f"{_legacy_path_component(audio_id)}.json"
        )
        _migrate_legacy_file(legacy_path, path)
        return path

    @contextmanager
    def workflow_lock(self, device_id: str, audio_id: str):
        """Serialize one audio item's complete sync workflow."""
        identity = json.dumps([device_id, audio_id], ensure_ascii=True, separators=(",", ":"))
        lock_name = hashlib.sha256(identity.encode("ascii")).hexdigest() + ".lock"
        with _file_lock(self.root / ".workflow-locks" / lock_name):
            yield

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
        with _item_lock(path):
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
