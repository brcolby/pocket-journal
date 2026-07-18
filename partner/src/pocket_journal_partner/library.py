from __future__ import annotations

from contextlib import contextmanager
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import sqlite3
from typing import Any, Iterator
import unicodedata

from .storage import PartnerStore


LIBRARY_SCHEMA_VERSION = 3
MAX_TITLE_LENGTH = 200
MAX_SEARCH_LENGTH = 200
LIBRARY_AVAILABILITY_FILTERS = (
    "all",
    "audio",
    "text",
    "missing_text",
    "missing_audio",
)


def stable_note_id(device_id: str, audio_id: str) -> str:
    identity = json.dumps(
        [device_id, audio_id], ensure_ascii=False, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(identity).hexdigest()


def _now() -> str:
    return datetime.now(timezone.utc).isoformat()


def _validated_title(title: str) -> str:
    if not isinstance(title, str):
        raise ValueError("note title must be text")
    if any(unicodedata.category(character) == "Cc" for character in title):
        raise ValueError("note title must not contain control characters")
    normalized = " ".join(title.split())
    if not normalized:
        raise ValueError("note title must not be empty")
    if len(normalized) > MAX_TITLE_LENGTH:
        raise ValueError(f"note title must be at most {MAX_TITLE_LENGTH} characters")
    return normalized


def _validated_search(search: str | None) -> str | None:
    if search is None:
        return None
    if not isinstance(search, str):
        raise ValueError("note search must be text")
    if any(unicodedata.category(character) == "Cc" for character in search):
        raise ValueError("note search must not contain control characters")
    normalized = " ".join(search.split())
    if not normalized:
        return None
    if len(normalized) > MAX_SEARCH_LENGTH:
        raise ValueError(f"note search must be at most {MAX_SEARCH_LENGTH} characters")
    return normalized


def _validated_availability(availability: str) -> str:
    if availability not in LIBRARY_AVAILABILITY_FILTERS:
        raise ValueError(
            "note availability must be one of "
            + ", ".join(LIBRARY_AVAILABILITY_FILTERS)
        )
    return availability


@dataclass(frozen=True)
class LibraryNote:
    note_id: str
    device_id: str
    audio_id: str
    filename: str
    title: str
    audio_path: str | None
    transcript_text: str | None
    transcript: dict[str, Any] | None
    source_sha256: str | None
    created_at: str | None
    duration_ms: int | None
    device_synced: bool
    updated_at: str

    def as_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(frozen=True)
class LibraryDeleteResult:
    note_id: str
    title: str
    audio_path: str | None
    audio_removed: bool
    audio_retained_reason: str | None
    cleanup_errors: tuple[str, ...]

    @property
    def cleanup_complete(self) -> bool:
        return not self.cleanup_errors


_MIGRATIONS = (
    (
        1,
        """
        CREATE TABLE notes (
            note_id TEXT PRIMARY KEY,
            device_id TEXT NOT NULL,
            audio_id TEXT NOT NULL,
            filename TEXT NOT NULL,
            title TEXT NOT NULL,
            audio_path TEXT,
            transcript_text TEXT,
            transcript_json TEXT,
            source_sha256 TEXT,
            created_at TEXT,
            duration_ms INTEGER,
            device_synced INTEGER NOT NULL DEFAULT 0,
            updated_at TEXT NOT NULL,
            UNIQUE (device_id, audio_id),
            CHECK (length(note_id) = 64),
            CHECK (length(title) BETWEEN 1 AND 200),
            CHECK (duration_ms IS NULL OR duration_ms >= 0),
            CHECK (device_synced IN (0, 1))
        );
        """,
    ),
    (
        2,
        """
        CREATE INDEX notes_updated_at_idx ON notes(updated_at DESC, note_id);
        CREATE INDEX notes_device_idx ON notes(device_id, created_at DESC, note_id);
        """,
    ),
    (
        3,
        """
        CREATE TABLE note_tombstones (
            note_id TEXT PRIMARY KEY,
            device_id TEXT NOT NULL,
            audio_id TEXT NOT NULL,
            deleted_at TEXT NOT NULL,
            UNIQUE (device_id, audio_id),
            CHECK (length(note_id) = 64)
        );
        """,
    ),
)


class NoteLibrary:
    """Structured index pairing each device recording with local audio and text."""

    def __init__(self, root: Path) -> None:
        self.root = root.resolve()
        self.path = self.root / "library.sqlite3"
        self.root.mkdir(parents=True, exist_ok=True)
        try:
            os.chmod(self.root, 0o700)
        except OSError:
            pass
        self._migrate()

    @contextmanager
    def _connect(self) -> Iterator[sqlite3.Connection]:
        connection = sqlite3.connect(self.path, timeout=5.0)
        connection.row_factory = sqlite3.Row
        connection.execute("PRAGMA foreign_keys = ON")
        connection.execute("PRAGMA busy_timeout = 5000")
        try:
            yield connection
        finally:
            connection.close()
            for path in (self.path, Path(str(self.path) + "-wal"), Path(str(self.path) + "-shm")):
                try:
                    if path.exists():
                        os.chmod(path, 0o600)
                except OSError:
                    pass

    def _migrate(self) -> None:
        with self._connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            try:
                connection.execute(
                    "CREATE TABLE IF NOT EXISTS schema_migrations "
                    "(version INTEGER PRIMARY KEY, applied_at TEXT NOT NULL)"
                )
                applied = {
                    int(row[0])
                    for row in connection.execute("SELECT version FROM schema_migrations")
                }
                for version, script in _MIGRATIONS:
                    if version in applied:
                        continue
                    if version != (max(applied) + 1 if applied else 1):
                        raise RuntimeError("note library migrations are not contiguous")
                    for statement in script.split(";"):
                        if statement.strip():
                            connection.execute(statement)
                    connection.execute(
                        "INSERT INTO schema_migrations(version, applied_at) VALUES (?, ?)",
                        (version, _now()),
                    )
                    applied.add(version)
                current = max(applied) if applied else 0
                if current != LIBRARY_SCHEMA_VERSION:
                    raise RuntimeError(
                        f"unsupported note library schema {current}; expected {LIBRARY_SCHEMA_VERSION}"
                    )
                connection.commit()
            except Exception:
                connection.rollback()
                raise

    def schema_version(self) -> int:
        with self._connect() as connection:
            row = connection.execute("SELECT MAX(version) FROM schema_migrations").fetchone()
        return int(row[0] or 0)

    def _relative_audio_path(self, path: Path) -> str:
        resolved = path.resolve()
        try:
            relative = resolved.relative_to(self.root)
        except ValueError as exc:
            raise ValueError("note audio must be stored inside the partner data directory") from exc
        return relative.as_posix()

    def resolve_audio_path(self, note: LibraryNote) -> Path | None:
        if note.audio_path is None:
            return None
        candidate = (self.root / note.audio_path).resolve()
        try:
            candidate.relative_to(self.root)
        except ValueError:
            return None
        return candidate if candidate.is_file() else None

    @staticmethod
    def _row_to_note(row: sqlite3.Row) -> LibraryNote:
        transcript: dict[str, Any] | None = None
        raw_transcript = row["transcript_json"]
        if raw_transcript:
            try:
                parsed = json.loads(raw_transcript)
            except json.JSONDecodeError:
                parsed = None
            if isinstance(parsed, dict):
                transcript = parsed
        return LibraryNote(
            note_id=row["note_id"],
            device_id=row["device_id"],
            audio_id=row["audio_id"],
            filename=row["filename"],
            title=row["title"],
            audio_path=row["audio_path"],
            transcript_text=row["transcript_text"],
            transcript=transcript,
            source_sha256=row["source_sha256"],
            created_at=row["created_at"],
            duration_ms=row["duration_ms"],
            device_synced=bool(row["device_synced"]),
            updated_at=row["updated_at"],
        )

    def upsert_discovered(
        self,
        device_id: str,
        audio_id: str,
        filename: str,
        *,
        label: str | None = None,
        source_sha256: str | None = None,
        created_at: str | None = None,
        duration_ms: int | None = None,
        device_synced: bool = False,
        restore_deleted: bool = True,
    ) -> LibraryNote:
        if not device_id or not audio_id or not filename:
            raise ValueError("device id, audio id, and filename are required")
        note_id = stable_note_id(device_id, audio_id)
        fallback_title = label or Path(filename).stem or "Untitled note"
        title = _validated_title(fallback_title[:MAX_TITLE_LENGTH])
        now = _now()
        with self._connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            if restore_deleted:
                connection.execute(
                    "DELETE FROM note_tombstones WHERE note_id = ?",
                    (note_id,),
                )
            elif connection.execute(
                "SELECT 1 FROM note_tombstones WHERE note_id = ?",
                (note_id,),
            ).fetchone() is not None:
                connection.rollback()
                raise KeyError(f"locally deleted note: {note_id}")
            connection.execute(
                """
                INSERT INTO notes(
                    note_id, device_id, audio_id, filename, title, source_sha256,
                    created_at, duration_ms, device_synced, updated_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(device_id, audio_id) DO UPDATE SET
                    filename = excluded.filename,
                    source_sha256 = COALESCE(excluded.source_sha256, notes.source_sha256),
                    created_at = COALESCE(excluded.created_at, notes.created_at),
                    duration_ms = COALESCE(excluded.duration_ms, notes.duration_ms),
                    device_synced = MAX(notes.device_synced, excluded.device_synced),
                    updated_at = excluded.updated_at
                """,
                (
                    note_id,
                    device_id,
                    audio_id,
                    Path(filename).name,
                    title,
                    source_sha256,
                    created_at,
                    duration_ms,
                    int(device_synced),
                    now,
                ),
            )
            connection.commit()
        note = self.get(note_id)
        assert note is not None
        return note

    def attach_audio(
        self,
        note_id: str,
        path: Path,
        *,
        source_sha256: str | None = None,
    ) -> LibraryNote:
        if not path.is_file():
            raise ValueError("note audio file does not exist")
        relative = self._relative_audio_path(path)
        with self._connect() as connection:
            cursor = connection.execute(
                "UPDATE notes SET audio_path = ?, source_sha256 = COALESCE(?, source_sha256), "
                "updated_at = ? WHERE note_id = ?",
                (relative, source_sha256, _now(), note_id),
            )
            connection.commit()
        if cursor.rowcount != 1:
            raise KeyError(f"unknown note: {note_id}")
        note = self.get(note_id)
        assert note is not None
        return note

    def attach_transcript(self, note_id: str, transcript: dict[str, Any]) -> LibraryNote:
        text = transcript.get("text") if isinstance(transcript, dict) else None
        if not isinstance(text, str) or not text.strip():
            raise ValueError("note transcript must contain non-empty text")
        serialized = json.dumps(
            transcript,
            ensure_ascii=False,
            separators=(",", ":"),
            sort_keys=True,
            allow_nan=False,
        )
        with self._connect() as connection:
            cursor = connection.execute(
                "UPDATE notes SET transcript_text = ?, transcript_json = ?, updated_at = ? "
                "WHERE note_id = ?",
                (text.strip(), serialized, _now(), note_id),
            )
            connection.commit()
        if cursor.rowcount != 1:
            raise KeyError(f"unknown note: {note_id}")
        note = self.get(note_id)
        assert note is not None
        return note

    def update_title(self, note_id: str, title: str) -> LibraryNote:
        normalized = _validated_title(title)
        with self._connect() as connection:
            cursor = connection.execute(
                "UPDATE notes SET title = ?, updated_at = ? WHERE note_id = ?",
                (normalized, _now(), note_id),
            )
            connection.commit()
        if cursor.rowcount != 1:
            raise KeyError(f"unknown note: {note_id}")
        note = self.get(note_id)
        assert note is not None
        return note

    def _managed_audio_candidate(self, relative_path: str) -> tuple[Path | None, str | None]:
        if not isinstance(relative_path, str):
            return None, "audio path is not a safe library-relative path"
        pure = PurePosixPath(relative_path)
        if (
            not relative_path
            or pure.is_absolute()
            or any(part in {"", ".", ".."} for part in pure.parts)
        ):
            return None, "audio path is not a safe library-relative path"
        candidate = self.root.joinpath(*pure.parts)
        current = self.root
        for part in pure.parts:
            current /= part
            try:
                if current.is_symlink():
                    return None, "audio path contains a symbolic link and was not removed"
            except OSError as error:
                return None, f"audio path could not be inspected: {error}"
        try:
            resolved = candidate.resolve(strict=False)
            resolved.relative_to(self.root)
        except (OSError, RuntimeError, ValueError):
            return None, "audio path resolves outside the partner data directory"
        return candidate, None

    def _remove_managed_artifact(
        self,
        path: Path,
        label: str,
        errors: list[str],
    ) -> bool:
        """Remove one exact generated artifact without following directory links."""
        try:
            path.relative_to(self.root)
            resolved_parent = path.parent.resolve(strict=False)
            resolved_parent.relative_to(self.root)
        except (OSError, RuntimeError, ValueError):
            errors.append(f"{label} path is outside the partner data directory")
            return False
        try:
            if path.is_symlink():
                path.unlink()
                return True
            if not path.exists():
                return False
            if not path.is_file():
                errors.append(f"{label} is not a regular file")
                return False
            path.unlink()
            return True
        except FileNotFoundError:
            return False
        except OSError as error:
            errors.append(f"{label} could not be removed: {error}")
            return False

    def delete_note(self, note_id: str) -> LibraryDeleteResult:
        """Delete one local index row and its unshared, verified managed audio.

        The note workflow lock serializes this operation against device sync.
        The database commit is authoritative; cleanup happens afterward so an
        unlink failure cannot roll back into a row pointing at a partially
        removed artifact. Cleanup never expands beyond exact managed paths.
        """
        with self._connect() as connection:
            identity = connection.execute(
                "SELECT device_id, audio_id FROM notes WHERE note_id = ?",
                (note_id,),
            ).fetchone()
        if identity is None:
            raise KeyError(f"unknown note: {note_id}")
        device_id = identity["device_id"]
        audio_id = identity["audio_id"]
        if not isinstance(device_id, str) or not isinstance(audio_id, str):
            raise ValueError("note identity is not valid text")
        store = PartnerStore(self.root)
        with store.workflow_lock(device_id, audio_id):
            return self._delete_note_locked(note_id, store)

    def _delete_note_locked(
        self,
        note_id: str,
        store: PartnerStore,
    ) -> LibraryDeleteResult:
        with self._connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            row = connection.execute(
                "SELECT note_id, device_id, audio_id, title, audio_path "
                "FROM notes WHERE note_id = ?",
                (note_id,),
            ).fetchone()
            if row is None:
                connection.rollback()
                raise KeyError(f"unknown note: {note_id}")
            audio_path = row["audio_path"]
            shared_audio = False
            if audio_path:
                shared_audio = (
                    connection.execute(
                        "SELECT 1 FROM notes WHERE note_id != ? AND audio_path = ? LIMIT 1",
                        (note_id, audio_path),
                    ).fetchone()
                    is not None
                )
            connection.execute(
                "INSERT OR REPLACE INTO note_tombstones("
                "note_id, device_id, audio_id, deleted_at) VALUES (?, ?, ?, ?)",
                (row["note_id"], row["device_id"], row["audio_id"], _now()),
            )
            connection.execute("DELETE FROM notes WHERE note_id = ?", (note_id,))
            connection.commit()

        removed = False
        retained_reason: str | None = None
        errors: list[str] = []
        if audio_path and shared_audio:
            retained_reason = "audio is shared by another library note"
        elif audio_path:
            candidate, unsafe_reason = self._managed_audio_candidate(audio_path)
            if candidate is None:
                errors.append(unsafe_reason or "audio path was not safe to remove")
            else:
                try:
                    if candidate.exists():
                        if not candidate.is_file():
                            errors.append("audio attachment is not a regular file")
                        else:
                            candidate.unlink()
                            removed = True
                except FileNotFoundError:
                    pass
                except OSError as error:
                    errors.append(f"audio file could not be removed: {error}")

        # Legacy sync sidecars can otherwise recreate a deleted index row when
        # the library next opens. Remove only the deterministic current/legacy
        # paths for this identity; the tombstone also protects against failed
        # cleanup until a real device sync explicitly rediscovers the note.
        try:
            sidecars = store.note_sidecar_paths(row["device_id"], row["audio_id"])
        except (OSError, TypeError, ValueError) as error:
            errors.append(f"note cache paths could not be derived: {error}")
        else:
            for index, path in enumerate(sidecars):
                label = "transcript cache" if index < 2 else "sync job cache"
                self._remove_managed_artifact(path, label, errors)
        return LibraryDeleteResult(
            note_id=row["note_id"],
            title=row["title"],
            audio_path=audio_path,
            audio_removed=removed,
            audio_retained_reason=retained_reason,
            cleanup_errors=tuple(errors),
        )

    def get(self, note_id: str) -> LibraryNote | None:
        if len(note_id) != 64 or any(character not in "0123456789abcdef" for character in note_id):
            return None
        with self._connect() as connection:
            row = connection.execute(
                "SELECT * FROM notes WHERE note_id = ?", (note_id,)
            ).fetchone()
        return self._row_to_note(row) if row is not None else None

    def list_notes(
        self,
        *,
        limit: int = 100,
        offset: int = 0,
        search: str | None = None,
        availability: str = "all",
    ) -> list[LibraryNote]:
        if not 1 <= limit <= 1000:
            raise ValueError("note list limit must be between 1 and 1000")
        if offset < 0:
            raise ValueError("note list offset must not be negative")
        where, params = self._filter_query(search, availability)
        params.extend((limit, offset))
        with self._connect() as connection:
            rows = connection.execute(
                f"SELECT * FROM notes {where} "
                "ORDER BY COALESCE(created_at, updated_at) DESC, note_id LIMIT ? OFFSET ?",
                params,
            ).fetchall()
        return [self._row_to_note(row) for row in rows]

    @staticmethod
    def _filter_query(
        search: str | None, availability: str
    ) -> tuple[str, list[Any]]:
        normalized_search = _validated_search(search)
        normalized_availability = _validated_availability(availability)
        clauses: list[str] = []
        params: list[Any] = []
        if normalized_search:
            escaped = (
                normalized_search.replace("\\", "\\\\")
                .replace("%", "\\%")
                .replace("_", "\\_")
            )
            pattern = f"%{escaped}%"
            clauses.append(
                "(title LIKE ? ESCAPE '\\' OR transcript_text LIKE ? ESCAPE '\\' "
                "OR filename LIKE ? ESCAPE '\\')"
            )
            params.extend((pattern, pattern, pattern))
        availability_clauses = {
            "all": None,
            "audio": "audio_path IS NOT NULL",
            "text": "transcript_text IS NOT NULL",
            "missing_text": "transcript_text IS NULL",
            "missing_audio": "audio_path IS NULL",
        }
        availability_clause = availability_clauses[normalized_availability]
        if availability_clause:
            clauses.append(availability_clause)
        return ("WHERE " + " AND ".join(clauses) if clauses else ""), params

    def count(
        self,
        *,
        search: str | None = None,
        availability: str = "all",
    ) -> int:
        where, params = self._filter_query(search, availability)
        with self._connect() as connection:
            row = connection.execute(
                f"SELECT COUNT(*) FROM notes {where}", params
            ).fetchone()
        return int(row[0])

    def import_partner_store(self, store: Any) -> int:
        """Index legacy job/transcript files without rewriting or deleting them."""
        imported = 0
        for job_path in sorted((store.root / "jobs").glob("*/*.json")):
            try:
                job = json.loads(job_path.read_text(encoding="utf-8"))
            except (OSError, UnicodeError, json.JSONDecodeError):
                continue
            if not isinstance(job, dict):
                continue
            device_id = job.get("device_id")
            audio_id = job.get("audio_id")
            filename = job.get("filename")
            if not all(isinstance(value, str) and value for value in (device_id, audio_id, filename)):
                continue
            source = job.get("source") if isinstance(job.get("source"), dict) else {}
            source_sha256 = source.get("sha256") if isinstance(source.get("sha256"), str) else None
            try:
                note = self.upsert_discovered(
                    device_id,
                    audio_id,
                    filename,
                    source_sha256=source_sha256,
                    created_at=(
                        job.get("updated_at")
                        if isinstance(job.get("updated_at"), str)
                        else None
                    ),
                    device_synced=job.get("stage") == "uploaded",
                    restore_deleted=False,
                )
                audio_path = store.audio_path(device_id, audio_id, filename)
                if audio_path.is_file():
                    self.attach_audio(note.note_id, audio_path, source_sha256=source_sha256)
                transcript = store.load_transcript(device_id, audio_id)
                if (
                    isinstance(transcript, dict)
                    and isinstance(transcript.get("text"), str)
                    and transcript["text"].strip()
                ):
                    self.attach_transcript(note.note_id, transcript)
            except (KeyError, ValueError):
                continue
            imported += 1
        return imported
