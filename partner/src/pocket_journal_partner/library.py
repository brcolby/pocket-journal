from __future__ import annotations

from contextlib import contextmanager
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import sqlite3
from typing import Any, Iterator
import unicodedata


LIBRARY_SCHEMA_VERSION = 2
MAX_TITLE_LENGTH = 200


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
    ) -> LibraryNote:
        if not device_id or not audio_id or not filename:
            raise ValueError("device id, audio id, and filename are required")
        note_id = stable_note_id(device_id, audio_id)
        fallback_title = label or Path(filename).stem or "Untitled note"
        title = _validated_title(fallback_title[:MAX_TITLE_LENGTH])
        now = _now()
        with self._connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
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
    ) -> list[LibraryNote]:
        if not 1 <= limit <= 1000:
            raise ValueError("note list limit must be between 1 and 1000")
        if offset < 0:
            raise ValueError("note list offset must not be negative")
        params: list[Any] = []
        where = ""
        if search:
            escaped = search.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_")
            where = (
                "WHERE title LIKE ? ESCAPE '\\' OR transcript_text LIKE ? ESCAPE '\\' "
                "OR filename LIKE ? ESCAPE '\\'"
            )
            pattern = f"%{escaped}%"
            params.extend((pattern, pattern, pattern))
        params.extend((limit, offset))
        with self._connect() as connection:
            rows = connection.execute(
                f"SELECT * FROM notes {where} "
                "ORDER BY COALESCE(created_at, updated_at) DESC, note_id LIMIT ? OFFSET ?",
                params,
            ).fetchall()
        return [self._row_to_note(row) for row in rows]

    def count(self, *, search: str | None = None) -> int:
        if not search:
            query = "SELECT COUNT(*) FROM notes"
            params: tuple[Any, ...] = ()
        else:
            escaped = search.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_")
            pattern = f"%{escaped}%"
            query = (
                "SELECT COUNT(*) FROM notes WHERE title LIKE ? ESCAPE '\\' "
                "OR transcript_text LIKE ? ESCAPE '\\' OR filename LIKE ? ESCAPE '\\'"
            )
            params = (pattern, pattern, pattern)
        with self._connect() as connection:
            row = connection.execute(query, params).fetchone()
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
