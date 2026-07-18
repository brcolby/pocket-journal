from __future__ import annotations

from contextlib import contextmanager, redirect_stdout
from io import StringIO
import json
from pathlib import Path
import signal
import sqlite3
import subprocess
from tempfile import TemporaryDirectory
import threading
import unittest
from unittest import mock
import wave

from pocket_journal_partner import cli
from pocket_journal_partner.library import LIBRARY_SCHEMA_VERSION, NoteLibrary, stable_note_id
from pocket_journal_partner.storage import PartnerStore
import pocket_journal_partner.tui as tui_module
from pocket_journal_partner.tui import NativeAudioPlayer, run_tui


class FakeCursesError(Exception):
    pass


class FakeScreen:
    def __init__(
        self,
        keys: list[object],
        *,
        sizes: list[tuple[int, int]] | None = None,
    ) -> None:
        self.keys = list(keys)
        self.sizes = sizes or [(14, 96)]
        self.size_index = 0
        self.frames: list[dict[tuple[int, int], str]] = []
        self.current: dict[tuple[int, int], str] = {}
        self.timeouts: list[int] = []

    def keypad(self, enabled: bool) -> None:
        del enabled

    def getmaxyx(self) -> tuple[int, int]:
        return self.sizes[min(self.size_index, len(self.sizes) - 1)]

    def erase(self) -> None:
        had_content = bool(self.current)
        if self.current:
            self.frames.append(dict(self.current))
        self.current.clear()
        if had_content and self.size_index + 1 < len(self.sizes):
            self.size_index += 1

    def addnstr(self, y: int, x: int, value: str, width: int, attribute: int = 0) -> None:
        del attribute
        self.current[(y, x)] = value[:width]

    def refresh(self) -> None:
        pass

    def timeout(self, milliseconds: int) -> None:
        self.timeouts.append(milliseconds)

    def move(self, y: int, x: int) -> None:
        del y, x

    def clrtoeol(self) -> None:
        pass

    def get_wch(self) -> object:
        if not self.keys:
            raise AssertionError("fake terminal ran out of keys")
        key = self.keys.pop(0)
        if isinstance(key, BaseException):
            raise key
        return key

    def rendered_text(self) -> str:
        frames = self.frames + ([self.current] if self.current else [])
        return "\n".join(value for frame in frames for value in frame.values())


class FakeCurses:
    A_BOLD = 1
    A_DIM = 2
    A_REVERSE = 4
    KEY_ENTER = 343
    KEY_BACKSPACE = 263
    KEY_RESIZE = 410
    KEY_UP = 259
    KEY_DOWN = 258
    KEY_LEFT = 260
    KEY_RIGHT = 261
    KEY_PPAGE = 339
    KEY_NPAGE = 338
    KEY_HOME = 262
    KEY_END = 360
    error = FakeCursesError

    def __init__(self, screen: FakeScreen) -> None:
        self.screen = screen
        self.wrapper_called = False

    @staticmethod
    def curs_set(visibility: int) -> None:
        del visibility

    def wrapper(self, callback):  # type: ignore[no-untyped-def]
        self.wrapper_called = True
        return callback(self.screen)


class FakeAudioPlayer:
    def __init__(self, *, statuses: list[str] | None = None) -> None:
        self.events: list[object] = []
        self.active_note: str | None = None
        self.paused = False
        self.closed = False
        self.statuses = list(statuses or [])

    def toggle(
        self,
        note_id: str,
        path: Path | None,
        *,
        duration_ms: int | None = None,
    ) -> bool:
        if self.active_note == note_id:
            self.paused = not self.paused
            self.events.append("pause" if self.paused else "resume")
            return True
        if path is None:
            return False
        if self.active_note != note_id:
            self.events.append(("play", note_id, path, duration_ms))
            self.active_note = note_id
            self.paused = False
        return True

    def stop(self) -> None:
        self.events.append("stop")
        self.active_note = None
        self.paused = False

    def stop_note(self, note_id: str) -> None:
        self.events.append(("stop-note", note_id))
        if self.active_note == note_id:
            self.active_note = None
            self.paused = False

    def status_text(self) -> str:
        if self.statuses:
            return self.statuses.pop(0)
        if self.active_note is None:
            return ""
        return f"{'Paused' if self.paused else 'Playing'} 00:01 / 00:04"

    def close(self) -> None:
        self.events.append("close")
        self.active_note = None
        self.closed = True


class FakeAudioProcess:
    def __init__(self, pid: int = 4242, *, ignore_terminate: bool = False) -> None:
        self.pid = pid
        self.ignore_terminate = ignore_terminate
        self.returncode: int | None = None
        self.terminate_calls = 0
        self.kill_calls = 0
        self.wait_calls: list[float | None] = []

    def poll(self) -> int | None:
        return self.returncode

    def terminate(self) -> None:
        self.terminate_calls += 1
        if not self.ignore_terminate:
            self.returncode = -signal.SIGTERM

    def kill(self) -> None:
        self.kill_calls += 1
        self.returncode = -signal.SIGKILL

    def wait(self, timeout: float | None = None) -> int:
        self.wait_calls.append(timeout)
        if self.returncode is None:
            raise subprocess.TimeoutExpired("audio-player", timeout)
        return self.returncode


class NoteLibraryTests(unittest.TestCase):
    def test_schema_and_stable_identity(self) -> None:
        with TemporaryDirectory() as tmp:
            library = NoteLibrary(Path(tmp))
            first = library.upsert_discovered("device-a", "audio-a", "recording.wav")
            second = library.upsert_discovered("device-a", "audio-a", "renamed.wav")
            schema_version = library.schema_version()

        self.assertEqual(schema_version, LIBRARY_SCHEMA_VERSION)
        self.assertEqual(first.note_id, stable_note_id("device-a", "audio-a"))
        self.assertEqual(first.note_id, second.note_id)
        self.assertEqual(second.filename, "renamed.wav")
        self.assertEqual(second.title, "recording")

    def test_audio_transcript_and_edited_title_remain_paired(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            library = NoteLibrary(root)
            store = PartnerStore(root)
            audio = store.audio_path("device", "audio", "sample.wav")
            audio.parent.mkdir(parents=True, exist_ok=True)
            audio.write_bytes(b"RIFF-audio")
            note = library.upsert_discovered(
                "device", "audio", "sample.wav", label="Device label", duration_ms=1234
            )
            library.attach_audio(note.note_id, audio, source_sha256="a" * 64)
            library.attach_transcript(note.note_id, {"text": "Local words", "model": "test"})
            library.update_title(note.note_id, "  Edited   title  ")
            library.upsert_discovered("device", "audio", "sample.wav", label="Changed label")
            loaded = library.get(note.note_id)

            self.assertIsNotNone(loaded)
            assert loaded is not None
            self.assertEqual(loaded.title, "Edited title")
            self.assertEqual(loaded.transcript_text, "Local words")
            self.assertEqual(loaded.transcript, {"text": "Local words", "model": "test"})
            self.assertEqual(library.resolve_audio_path(loaded), audio.resolve())
            self.assertEqual(loaded.source_sha256, "a" * 64)

    def test_title_rejects_terminal_control_characters(self) -> None:
        with TemporaryDirectory() as tmp:
            library = NoteLibrary(Path(tmp))
            note = library.upsert_discovered("device", "audio", "sample.wav")

            for title in ("line\nbreak", "escape\x1b[2J", "c1\x9b2J"):
                with self.subTest(title=repr(title)):
                    with self.assertRaisesRegex(ValueError, "control"):
                        library.update_title(note.note_id, title)

    def test_audio_must_stay_inside_data_directory(self) -> None:
        with TemporaryDirectory() as tmp, TemporaryDirectory() as elsewhere:
            library = NoteLibrary(Path(tmp))
            note = library.upsert_discovered("device", "audio", "sample.wav")
            external = Path(elsewhere) / "sample.wav"
            external.write_bytes(b"audio")

            with self.assertRaisesRegex(ValueError, "inside the partner data"):
                library.attach_audio(note.note_id, external)

    def test_search_escapes_sql_wildcards(self) -> None:
        with TemporaryDirectory() as tmp:
            library = NoteLibrary(Path(tmp))
            library.upsert_discovered("device", "literal", "100_percent.wav")
            library.upsert_discovered("device", "other", "100xpercent.wav")

            matches = library.list_notes(search="100_")

        self.assertEqual([note.audio_id for note in matches], ["literal"])

    def test_search_and_availability_filters_compose(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            library = NoteLibrary(root)
            complete = library.upsert_discovered("device", "complete", "alpha.wav")
            complete_audio = root / "alpha.wav"
            complete_audio.write_bytes(b"RIFF")
            library.attach_audio(complete.note_id, complete_audio)
            library.attach_transcript(complete.note_id, {"text": "meeting words"})
            audio_only = library.upsert_discovered("device", "audio-only", "beta.wav")
            audio_only_file = root / "beta.wav"
            audio_only_file.write_bytes(b"RIFF")
            library.attach_audio(audio_only.note_id, audio_only_file)
            text_only = library.upsert_discovered("device", "text-only", "gamma.wav")
            library.attach_transcript(text_only.note_id, {"text": "meeting summary"})

            self.assertEqual(library.count(availability="audio"), 2)
            self.assertEqual(library.count(availability="text"), 2)
            self.assertEqual(library.count(availability="missing_text"), 1)
            self.assertEqual(library.count(availability="missing_audio"), 1)
            matches = library.list_notes(search="meeting", availability="missing_audio")

            self.assertEqual([note.note_id for note in matches], [text_only.note_id])
            with self.assertRaisesRegex(ValueError, "availability"):
                library.list_notes(availability="unknown")
            with self.assertRaisesRegex(ValueError, "control"):
                library.count(search="bad\nquery")

    def test_delete_removes_database_text_and_unshared_managed_audio(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            store = PartnerStore(root)
            library = NoteLibrary(root)
            note = library.upsert_discovered("device", "audio", "sample.wav")
            audio = store.audio_path("device", "audio", "sample.wav")
            audio.parent.mkdir(parents=True, exist_ok=True)
            audio.write_bytes(b"RIFF")
            library.attach_audio(note.note_id, audio)
            library.attach_transcript(note.note_id, {"text": "Private words"})
            job_path = store.save_job(
                "device",
                "audio",
                {
                    "device_id": "device",
                    "audio_id": "audio",
                    "filename": "sample.wav",
                },
            )
            transcript_path = store.save_transcript(
                "device", "audio", {"text": "Private words"}
            )

            result = library.delete_note(note.note_id)

            self.assertTrue(result.cleanup_complete)
            self.assertTrue(result.audio_removed)
            self.assertIsNone(library.get(note.note_id))
            self.assertFalse(audio.exists())
            self.assertFalse(job_path.exists())
            self.assertFalse(transcript_path.exists())

    def test_delete_accepts_exact_legacy_managed_audio_path(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            store = PartnerStore(root)
            library = NoteLibrary(root)
            note = library.upsert_discovered("device", "audio", "sample.wav")
            current, legacy = store.note_audio_paths(
                "device", "audio", "sample.wav"
            )
            legacy.parent.mkdir(parents=True, exist_ok=True)
            legacy.write_bytes(b"legacy RIFF")
            library.attach_audio(note.note_id, legacy)

            result = library.delete_note(note.note_id)

            self.assertTrue(result.audio_removed)
            self.assertFalse(legacy.exists())
            self.assertFalse(current.exists())

    def test_delete_never_unlinks_cross_identity_shared_audio(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            store = PartnerStore(root)
            library = NoteLibrary(root)
            first = library.upsert_discovered("device", "first", "shared.wav")
            second = library.upsert_discovered("device", "second", "shared.wav")
            audio = store.audio_path("device", "first", "shared.wav")
            audio.parent.mkdir(parents=True, exist_ok=True)
            audio.write_bytes(b"RIFF")
            library.attach_audio(first.note_id, audio)
            library.attach_audio(second.note_id, audio)

            first_result = library.delete_note(first.note_id)

            self.assertIn("shared", first_result.audio_retained_reason or "")
            self.assertTrue(audio.exists())
            second_result = library.delete_note(second.note_id)
            self.assertFalse(second_result.audio_removed)
            self.assertIn("exact managed path", " ".join(second_result.cleanup_errors))
            self.assertTrue(audio.exists())

    def test_delete_retains_unrelated_in_root_file_attached_as_audio(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            library = NoteLibrary(root)
            note = library.upsert_discovered("device", "audio", "sample.wav")
            config = root / "config.json"
            config.write_text('{"private": true}', encoding="utf-8")
            library.attach_audio(note.note_id, config)

            result = library.delete_note(note.note_id)

            self.assertIsNone(library.get(note.note_id))
            self.assertTrue(config.exists())
            self.assertEqual(config.read_text(encoding="utf-8"), '{"private": true}')
            self.assertFalse(result.audio_removed)
            self.assertIn("exact managed path", " ".join(result.cleanup_errors))

    def test_delete_commits_row_but_does_not_follow_unsafe_audio_path(self) -> None:
        with TemporaryDirectory() as tmp, TemporaryDirectory() as elsewhere:
            root = Path(tmp)
            library = NoteLibrary(root)
            note = library.upsert_discovered("device", "audio", "sample.wav")
            external = Path(elsewhere) / "outside.wav"
            external.write_bytes(b"do not delete")
            connection = sqlite3.connect(library.path)
            try:
                connection.execute(
                    "UPDATE notes SET audio_path = ? WHERE note_id = ?",
                    (f"../{Path(elsewhere).name}/outside.wav", note.note_id),
                )
                connection.commit()
            finally:
                connection.close()

            result = library.delete_note(note.note_id)

            self.assertIsNone(library.get(note.note_id))
            self.assertTrue(external.exists())
            self.assertTrue(result.cleanup_errors)

    def test_delete_reports_cleanup_failure_after_database_commit(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            library = NoteLibrary(root)
            note = library.upsert_discovered("device", "audio", "sample.wav")
            audio = PartnerStore(root).audio_path("device", "audio", "sample.wav")
            audio.parent.mkdir(parents=True, exist_ok=True)
            audio.write_bytes(b"RIFF")
            library.attach_audio(note.note_id, audio)

            with mock.patch.object(Path, "unlink", side_effect=PermissionError("read only")):
                result = library.delete_note(note.note_id)

            self.assertIsNone(library.get(note.note_id))
            self.assertTrue(audio.exists())
            self.assertIn("read only", " ".join(result.cleanup_errors))

    def test_delete_does_not_resurrect_from_retained_sidecars_on_restart(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            store = PartnerStore(root)
            job = {
                "device_id": "device",
                "audio_id": "audio",
                "filename": "sample.wav",
                "stage": "uploaded",
                "source": {"sha256": "b" * 64},
            }
            job_path = store.save_job("device", "audio", job)
            transcript_path = store.save_transcript(
                "device", "audio", {"text": "Cached private words"}
            )
            audio = store.audio_path("device", "audio", "sample.wav")
            audio.parent.mkdir(parents=True, exist_ok=True)
            audio.write_bytes(b"RIFF")
            library = NoteLibrary(root)
            self.assertEqual(library.import_partner_store(store), 1)
            note = library.list_notes()[0]

            original_unlink = Path.unlink
            retained_paths = {job_path.resolve(), transcript_path.resolve()}

            def retain_sidecars(path: Path, *args, **kwargs):  # type: ignore[no-untyped-def]
                if path.resolve(strict=False) in retained_paths:
                    raise PermissionError("retained for tombstone regression")
                return original_unlink(path, *args, **kwargs)

            with mock.patch.object(
                type(job_path), "unlink", autospec=True, side_effect=retain_sidecars
            ):
                result = library.delete_note(note.note_id)

            self.assertTrue(result.cleanup_errors)
            self.assertTrue(job_path.exists())
            self.assertTrue(transcript_path.exists())
            reopened = NoteLibrary(root)
            self.assertEqual(reopened.import_partner_store(store), 0)
            self.assertIsNone(reopened.get(note.note_id))

            restored = reopened.upsert_discovered("device", "audio", "sample.wav")
            self.assertEqual(restored.note_id, note.note_id)
            self.assertEqual(reopened.import_partner_store(store), 1)

    def test_import_waits_for_workflow_lock_and_rechecks_delete_tombstone(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            store = PartnerStore(root)
            library = NoteLibrary(root)
            note = library.upsert_discovered("device", "audio", "sample.wav")
            current_audio, legacy_audio = store.note_audio_paths(
                "device", "audio", "sample.wav"
            )
            sidecars = store.note_sidecar_paths("device", "audio")
            current_transcript, legacy_transcript, current_job, legacy_job = sidecars
            legacy_audio.parent.mkdir(parents=True, exist_ok=True)
            legacy_audio.write_bytes(b"legacy audio")
            legacy_transcript.parent.mkdir(parents=True, exist_ok=True)
            legacy_transcript.write_text('{"text": "legacy words"}', encoding="utf-8")
            legacy_job.parent.mkdir(parents=True, exist_ok=True)
            legacy_job.write_text(
                json.dumps(
                    {
                        "device_id": "device",
                        "audio_id": "audio",
                        "filename": "sample.wav",
                    }
                ),
                encoding="utf-8",
            )
            library.attach_audio(note.note_id, legacy_audio)
            retained = {
                legacy_audio.resolve(),
                legacy_transcript.resolve(),
                legacy_job.resolve(),
            }
            original_unlink = Path.unlink

            def retain_legacy(path: Path, *args, **kwargs):  # type: ignore[no-untyped-def]
                if path.resolve(strict=False) in retained:
                    raise PermissionError("retain legacy artifact")
                return original_unlink(path, *args, **kwargs)

            original_lock = PartnerStore.workflow_lock
            attempted = threading.Event()
            finished = threading.Event()
            import_results: list[int] = []

            @contextmanager
            def observed_lock(active_store, device_id, audio_id):  # type: ignore[no-untyped-def]
                attempted.set()
                with original_lock(active_store, device_id, audio_id):
                    yield

            def import_store() -> None:
                try:
                    import_results.append(library.import_partner_store(store))
                finally:
                    finished.set()

            with original_lock(store, "device", "audio"):
                with mock.patch.object(
                    type(legacy_job),
                    "unlink",
                    autospec=True,
                    side_effect=retain_legacy,
                ):
                    result = library._delete_note_locked(note.note_id, store)
                self.assertTrue(result.cleanup_errors)
                with mock.patch.object(
                    PartnerStore,
                    "workflow_lock",
                    observed_lock,
                ):
                    thread = threading.Thread(target=import_store)
                    thread.start()
                    self.assertTrue(attempted.wait(1))
                    self.assertFalse(finished.wait(0.05))

            self.assertTrue(finished.wait(1))
            thread.join(1)
            self.assertEqual(import_results, [0])
            self.assertIsNone(library.get(note.note_id))
            self.assertTrue(legacy_audio.exists())
            self.assertTrue(legacy_transcript.exists())
            self.assertTrue(legacy_job.exists())
            self.assertFalse(current_audio.exists())
            self.assertFalse(current_transcript.exists())
            self.assertFalse(current_job.exists())

    def test_delete_waits_for_the_matching_sync_workflow_lock(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            store = PartnerStore(root)
            library = NoteLibrary(root)
            note = library.upsert_discovered("device", "audio", "sample.wav")
            job_path = store.save_job(
                "device",
                "audio",
                {
                    "device_id": "device",
                    "audio_id": "audio",
                    "filename": "sample.wav",
                },
            )
            original_lock = PartnerStore.workflow_lock
            attempted = threading.Event()
            acquired = threading.Event()
            finished = threading.Event()
            errors: list[Exception] = []

            @contextmanager
            def observed_lock(active_store, device_id, audio_id):  # type: ignore[no-untyped-def]
                attempted.set()
                with original_lock(active_store, device_id, audio_id):
                    acquired.set()
                    yield

            def delete() -> None:
                try:
                    library.delete_note(note.note_id)
                except Exception as error:
                    errors.append(error)
                finally:
                    finished.set()

            with original_lock(store, "device", "audio"):
                with mock.patch.object(PartnerStore, "workflow_lock", observed_lock):
                    thread = threading.Thread(target=delete)
                    thread.start()
                    self.assertTrue(attempted.wait(1))
                    self.assertFalse(acquired.wait(0.05))
                    self.assertFalse(finished.is_set())
                    store.save_job(
                        "device",
                        "audio",
                        {
                            "device_id": "device",
                            "audio_id": "audio",
                            "filename": "sample.wav",
                            "stage": "rewritten-while-locked",
                        },
                    )

            self.assertTrue(acquired.wait(1))
            self.assertTrue(finished.wait(1))
            thread.join(1)
            self.assertFalse(errors)
            self.assertIsNone(library.get(note.note_id))
            self.assertFalse(job_path.exists())

    def test_concurrent_updates_do_not_duplicate_note_identity(self) -> None:
        with TemporaryDirectory() as tmp:
            library = NoteLibrary(Path(tmp))
            errors: list[Exception] = []

            def update(index: int) -> None:
                try:
                    library.upsert_discovered("device", "audio", f"sample-{index}.wav")
                except Exception as error:
                    errors.append(error)

            threads = [threading.Thread(target=update, args=(index,)) for index in range(8)]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join(5)

            self.assertFalse(errors)
            self.assertEqual(library.count(), 1)

    def test_concurrent_first_open_serializes_schema_migrations(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            errors: list[Exception] = []
            versions: list[int] = []

            def open_library() -> None:
                try:
                    versions.append(NoteLibrary(root).schema_version())
                except Exception as error:
                    errors.append(error)

            threads = [threading.Thread(target=open_library) for _ in range(6)]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join(5)

        self.assertFalse(errors)
        self.assertEqual(versions, [LIBRARY_SCHEMA_VERSION] * 6)

    def test_imports_existing_job_audio_and_transcript_idempotently(self) -> None:
        with TemporaryDirectory() as tmp:
            store = PartnerStore(Path(tmp))
            job = {
                "device_id": "device",
                "audio_id": "audio",
                "filename": "sample.wav",
                "stage": "uploaded",
                "source": {"sha256": "b" * 64},
            }
            store.save_job("device", "audio", job)
            audio = store.audio_path("device", "audio", "sample.wav")
            audio.parent.mkdir(parents=True)
            audio.write_bytes(b"RIFF")
            store.save_transcript("device", "audio", {"text": "Imported"})
            library = NoteLibrary(Path(tmp))

            self.assertEqual(library.import_partner_store(store), 1)
            self.assertEqual(library.import_partner_store(store), 1)
            note = library.list_notes()[0]

        self.assertEqual(note.transcript_text, "Imported")
        self.assertTrue(note.device_synced)
        self.assertIsNotNone(note.audio_path)


class LibraryCliAndTuiTests(unittest.TestCase):
    def test_cli_lists_shows_and_titles_notes(self) -> None:
        with TemporaryDirectory() as tmp:
            library = NoteLibrary(Path(tmp))
            note = library.upsert_discovered("device", "audio", "sample.wav")
            library.attach_transcript(note.note_id, {"text": "Read me"})

            stdout = StringIO()
            with redirect_stdout(stdout):
                self.assertEqual(cli.main(["library", "list", "--data-dir", tmp]), 0)
            listed = json.loads(stdout.getvalue())
            self.assertEqual(listed["total"], 1)
            self.assertNotIn("transcript", listed["notes"][0])

            stdout = StringIO()
            with redirect_stdout(stdout):
                self.assertEqual(
                    cli.main(["library", "title", note.note_id, "New title", "--data-dir", tmp]),
                    0,
                )
            self.assertEqual(json.loads(stdout.getvalue())["title"], "New title")

            stdout = StringIO()
            with redirect_stdout(stdout):
                self.assertEqual(cli.main(["library", "show", note.note_id, "--data-dir", tmp]), 0)
            self.assertEqual(json.loads(stdout.getvalue())["transcript"]["text"], "Read me")

    def test_tui_searches_reads_edits_plays_and_deletes_without_shelling_out(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            library = NoteLibrary(root)
            note = library.upsert_discovered("device", "audio", "sample.wav")
            library.attach_transcript(note.note_id, {"text": "Transcript\x1b[2J remains readable"})
            audio = PartnerStore(root).audio_path("device", "audio", "sample.wav")
            audio.parent.mkdir(parents=True, exist_ok=True)
            audio.write_bytes(b"RIFF")
            library.attach_audio(note.note_id, audio)
            player = FakeAudioPlayer()
            keys: list[object] = [
                "/",
                *"sample",
                "\n",
                " ",
                " ",
                FakeCurses.KEY_RIGHT,
                " ",
                FakeCurses.KEY_LEFT,
                "f",
                "t",
                *("\b" * len("sample")),
                *"Renamed note",
                "\n",
                FakeCurses.KEY_RIGHT,
                "d",
                *"DELETE",
                "\n",
                "q",
            ]
            screen = FakeScreen(keys, sizes=[(14, 44)])
            curses = FakeCurses(screen)

            result = run_tui(
                library,
                audio_player=player,
                screen=screen,
                curses_module=curses,
            )
            self.assertEqual(result, 0)
            rendered = screen.rendered_text()
            self.assertIn("[audio] [text]", rendered)
            self.assertIn("Transcript?[2J remains readable", rendered)
            self.assertNotIn("\x1b", rendered)
            self.assertIn("Renamed note", rendered)
            self.assertIn("Playing 00:01 / 00:04", rendered)
            self.assertIn("filter: has audio", rendered)
            self.assertIn("→ text", rendered)
            self.assertIn("← menu", rendered)
            self.assertIn("Space play/pause", rendered)
            self.assertIn("T title", rendered)
            self.assertIn("D delete", rendered)
            self.assertNotIn("Enter read", rendered)
            self.assertNotIn("P play/restart", rendered)
            self.assertNotIn("E title", rendered)
            self.assertNotIn("S stop", rendered)
            self.assertNotIn("B back", rendered)
            self.assertEqual(
                player.events,
                [
                    ("play", note.note_id, audio.resolve(), None),
                    "pause",
                    "resume",
                    ("stop-note", note.note_id),
                    "close",
                ],
            )
            self.assertIn(200, screen.timeouts)
            self.assertIn(-1, screen.timeouts)
            self.assertTrue(player.closed)
            self.assertIsNone(library.get(note.note_id))
            self.assertFalse(audio.exists())

    def test_tui_space_reports_when_selected_note_has_no_audio(self) -> None:
        with TemporaryDirectory() as tmp:
            library = NoteLibrary(Path(tmp))
            library.upsert_discovered("device", "missing", "missing.wav")
            player = FakeAudioPlayer()
            screen = FakeScreen([" ", "q"])

            self.assertEqual(
                run_tui(
                    library,
                    audio_player=player,
                    screen=screen,
                    curses_module=FakeCurses(screen),
                ),
                0,
            )

            self.assertIn("No local audio is available", screen.rendered_text())
            self.assertEqual(player.events, ["close"])

    def test_native_player_uses_fixed_argv_tracks_time_and_reaps_paused_audio(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            audio = root / "audio" / "note name.wav"
            audio.parent.mkdir()
            with wave.open(str(audio), "wb") as stream:
                stream.setnchannels(1)
                stream.setsampwidth(2)
                stream.setframerate(8_000)
                stream.writeframes(b"\0\0" * 16_000)
            now = [10.0]
            processes: list[FakeAudioProcess] = []
            launches: list[tuple[list[str], dict[str, object]]] = []
            signals: list[tuple[int, int]] = []

            def launch(arguments: list[str], **options: object) -> FakeAudioProcess:
                launches.append((arguments, options))
                process = FakeAudioProcess(pid=5000 + len(processes))
                processes.append(process)
                return process

            player = NativeAudioPlayer(
                root,
                command=("/usr/bin/afplay",),
                clock=lambda: now[0],
                popen=launch,  # type: ignore[arg-type]
                signal_process=lambda pid, action: signals.append((pid, action)),
            )
            player.play("note-a", audio)
            self.assertEqual(launches[0][0], ["/usr/bin/afplay", str(audio.resolve())])
            self.assertIs(launches[0][1]["stdin"], subprocess.DEVNULL)
            self.assertIs(launches[0][1]["stdout"], subprocess.DEVNULL)
            self.assertIs(launches[0][1]["stderr"], subprocess.DEVNULL)
            self.assertTrue(launches[0][1]["close_fds"])
            self.assertFalse(launches[0][1]["shell"])
            self.assertTrue(launches[0][1]["start_new_session"])
            self.assertEqual(player.status_text(), "Playing 00:00 / 00:02")

            now[0] = 11.25
            player.pause_resume()
            self.assertEqual(player.status_text(), "Paused 00:01 / 00:02")
            now[0] = 30.0
            self.assertEqual(player.status_text(), "Paused 00:01 / 00:02")
            player.close()

            self.assertEqual(
                signals,
                [(processes[0].pid, signal.SIGSTOP), (processes[0].pid, signal.SIGCONT)],
            )
            self.assertEqual(processes[0].terminate_calls, 1)
            self.assertEqual(processes[0].kill_calls, 0)
            self.assertIsNotNone(processes[0].returncode)

    def test_native_player_toggle_switches_notes_reports_failure_and_rejects_paths(self) -> None:
        with TemporaryDirectory() as tmp, TemporaryDirectory() as outside_tmp:
            root = Path(tmp)
            first_audio = root / "first.wav"
            second_audio = root / "second.wav"
            outside_audio = Path(outside_tmp) / "outside.wav"
            for path in (first_audio, second_audio, outside_audio):
                path.write_bytes(b"not-a-wave")
            processes: list[FakeAudioProcess] = []
            launches: list[list[str]] = []
            signals: list[tuple[int, int]] = []

            def launch(arguments: list[str], **_options: object) -> FakeAudioProcess:
                launches.append(arguments)
                process = FakeAudioProcess(pid=6000 + len(processes))
                processes.append(process)
                return process

            player = NativeAudioPlayer(
                root,
                command=("/usr/bin/afplay",),
                popen=launch,  # type: ignore[arg-type]
                signal_process=lambda pid, action: signals.append((pid, action)),
            )
            player.toggle("first", first_audio, duration_ms=4_000)
            player.toggle("first", first_audio, duration_ms=4_000)
            self.assertIn("Paused", player.status_text())
            first_audio.unlink()
            self.assertTrue(player.toggle("first", None, duration_ms=4_000))
            self.assertIn("Playing", player.status_text())
            self.assertTrue(player.toggle("first", None, duration_ms=4_000))
            self.assertIn("Paused", player.status_text())
            player.toggle("second", second_audio, duration_ms=4_000)
            self.assertEqual(processes[0].terminate_calls, 1)
            self.assertEqual(len(launches), 2)
            self.assertEqual(
                signals,
                [
                    (processes[0].pid, signal.SIGSTOP),
                    (processes[0].pid, signal.SIGCONT),
                    (processes[0].pid, signal.SIGSTOP),
                    (processes[0].pid, signal.SIGCONT),
                ],
            )

            processes[1].returncode = 0
            self.assertEqual(player.status_text(), "Playback finished at 00:04")
            self.assertTrue(player.toggle("second", second_audio, duration_ms=4_000))
            self.assertEqual(len(launches), 3)
            processes[2].returncode = 7
            self.assertEqual(player.status_text(), "Playback failed (player exited 7)")
            self.assertTrue(player.toggle("outside", outside_audio))
            self.assertEqual(len(launches), 3)
            self.assertIn("inside the library", player.status_text())
            self.assertFalse(player.toggle("missing", None))
            player.close()

            unavailable = NativeAudioPlayer(root, command=None, popen=launch)  # type: ignore[arg-type]
            replacement = root / "replacement.wav"
            replacement.write_bytes(b"not-a-wave")
            self.assertTrue(unavailable.toggle("first", replacement))
            self.assertIn("unavailable", unavailable.status_text().lower())
            self.assertEqual(len(launches), 3)

    def test_native_player_owns_child_before_deferred_signal_unwinds(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            audio = root / "signal-race.wav"
            audio.write_bytes(b"not-a-wave")
            process = FakeAudioProcess()

            def launch(_arguments: list[str], **_options: object) -> FakeAudioProcess:
                handler = signal.getsignal(signal.SIGTERM)
                if not callable(handler):
                    raise AssertionError("launch signal was not deferred")
                handler(signal.SIGTERM, None)
                return process

            player = NativeAudioPlayer(
                root,
                command=("/usr/bin/afplay",),
                popen=launch,  # type: ignore[arg-type]
            )
            with tui_module._unwind_on_termination_signal():
                with self.assertRaises(tui_module._TuiSignal) as termination:
                    player.play("note", audio)

            self.assertEqual(termination.exception.signum, signal.SIGTERM)
            self.assertEqual(process.terminate_calls, 1)
            self.assertIsNotNone(process.returncode)
            player.close()

    def test_native_player_restores_signals_after_partial_install_interrupt(self) -> None:
        with TemporaryDirectory() as tmp:
            player = NativeAudioPlayer(Path(tmp), command=None)

            def previous_int(_signum: int, _frame: object) -> None:
                pass

            def previous_term(_signum: int, _frame: object) -> None:
                pass

            handlers: dict[int, object] = {
                signal.SIGINT: previous_int,
                signal.SIGTERM: previous_term,
            }

            def install(signum: int, handler: object) -> object:
                if signum == signal.SIGTERM and handler is not previous_term:
                    raise KeyboardInterrupt
                previous = handlers[signum]
                handlers[signum] = handler
                return previous

            with mock.patch.object(
                signal,
                "getsignal",
                side_effect=lambda signum: handlers.get(signum, signal.SIG_IGN),
            ):
                with mock.patch.object(signal, "signal", side_effect=install):
                    with self.assertRaises(KeyboardInterrupt):
                        with player._defer_launch_signals():
                            self.fail("partial installation should not enter the body")

            self.assertIs(handlers[signal.SIGINT], previous_int)
            self.assertIs(handlers[signal.SIGTERM], previous_term)

    def test_native_player_restores_all_signals_when_restore_is_interrupted(self) -> None:
        with TemporaryDirectory() as tmp:
            player = NativeAudioPlayer(Path(tmp), command=None)

            def previous_int(_signum: int, _frame: object) -> None:
                pass

            def previous_term(_signum: int, _frame: object) -> None:
                pass

            handlers: dict[int, object] = {
                signal.SIGINT: previous_int,
                signal.SIGTERM: previous_term,
            }

            def install(signum: int, handler: object) -> object:
                previous = handlers[signum]
                handlers[signum] = handler
                if signum == signal.SIGTERM and handler is previous_term:
                    raise KeyboardInterrupt
                return previous

            with mock.patch.object(
                signal,
                "getsignal",
                side_effect=lambda signum: handlers.get(signum, signal.SIG_IGN),
            ):
                with mock.patch.object(signal, "signal", side_effect=install):
                    with self.assertRaises(KeyboardInterrupt):
                        with player._defer_launch_signals():
                            pass

            self.assertIs(handlers[signal.SIGINT], previous_int)
            self.assertIs(handlers[signal.SIGTERM], previous_term)

    def test_native_player_force_reaps_child_that_inherits_ignored_sigterm(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            audio = root / "ignored-term.wav"
            audio.write_bytes(b"not-a-wave")
            process = FakeAudioProcess(ignore_terminate=True)
            player = NativeAudioPlayer(
                root,
                command=("/usr/bin/afplay",),
                popen=lambda *_args, **_options: process,  # type: ignore[arg-type]
            )

            player.play("note", audio)
            player.stop()

            self.assertEqual(process.terminate_calls, 1)
            self.assertEqual(process.kill_calls, 1)
            self.assertEqual(
                process.wait_calls,
                [tui_module.PLAYER_TERMINATE_GRACE_SECONDS, tui_module.PLAYER_KILL_WAIT_SECONDS],
            )
            self.assertLessEqual(tui_module.PLAYER_TERMINATE_GRACE_SECONDS, 0.05)
            self.assertIn("Playback stopped", player.status_text())

    def test_tui_refreshes_without_input_and_closes_on_errors_and_signals(self) -> None:
        with TemporaryDirectory() as tmp:
            library = NoteLibrary(Path(tmp))
            timed_player = FakeAudioPlayer(statuses=["Playing 00:00 / 00:04", "Playing 00:01 / 00:04"])
            timed_screen = FakeScreen([FakeCursesError(), "q"])
            self.assertEqual(
                run_tui(
                    library,
                    audio_player=timed_player,
                    screen=timed_screen,
                    curses_module=FakeCurses(timed_screen),
                ),
                0,
            )
            rendered = timed_screen.rendered_text()
            self.assertIn("Playing 00:00 / 00:04", rendered)
            self.assertIn("Playing 00:01 / 00:04", rendered)
            self.assertTrue(timed_player.closed)

            failing_player = FakeAudioPlayer()
            failing_screen = FakeScreen([RuntimeError("terminal failed")])
            with self.assertRaisesRegex(RuntimeError, "terminal failed"):
                run_tui(
                    library,
                    audio_player=failing_player,
                    screen=failing_screen,
                    curses_module=FakeCurses(failing_screen),
                )
            self.assertTrue(failing_player.closed)

            termination_player = FakeAudioPlayer()

            class TerminationScreen(FakeScreen):
                def get_wch(self) -> object:
                    handler = signal.getsignal(signal.SIGTERM)
                    if not callable(handler):
                        raise AssertionError("SIGTERM unwind handler was not installed")
                    handler(signal.SIGTERM, None)
                    raise AssertionError("SIGTERM unwind handler returned")

            termination_screen = TerminationScreen([])
            previous = signal.getsignal(signal.SIGTERM)
            self.assertEqual(
                run_tui(
                    library,
                    audio_player=termination_player,
                    screen=termination_screen,
                    curses_module=FakeCurses(termination_screen),
                ),
                128 + signal.SIGTERM,
            )
            self.assertTrue(termination_player.closed)
            self.assertIs(signal.getsignal(signal.SIGTERM), previous)

    def test_tui_preserves_ignored_termination_signals(self) -> None:
        with mock.patch.object(signal, "getsignal", return_value=signal.SIG_IGN):
            with mock.patch.object(signal, "signal") as install_handler:
                with tui_module._unwind_on_termination_signal():
                    pass

        install_handler.assert_not_called()

    def test_tui_wrapper_resize_and_unavailable_curses_are_graceful(self) -> None:
        with TemporaryDirectory() as tmp:
            library = NoteLibrary(Path(tmp))
            screen = FakeScreen(
                [FakeCurses.KEY_RESIZE, "q"],
                sizes=[(6, 32), (12, 80)],
            )
            curses = FakeCurses(screen)

            result = run_tui(library, curses_module=curses)

            self.assertEqual(result, 0)
            self.assertTrue(curses.wrapper_called)
            self.assertIn("Resize to at least", screen.rendered_text())

            errors = StringIO()
            with mock.patch.object(tui_module, "_curses", None):
                unavailable = run_tui(library, error_stream=errors)
            self.assertEqual(unavailable, 2)
            self.assertIn("unavailable", errors.getvalue())


if __name__ == "__main__":
    unittest.main()
