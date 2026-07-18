from __future__ import annotations

from contextlib import contextmanager, redirect_stdout
from io import StringIO
import json
from pathlib import Path
import sqlite3
from tempfile import TemporaryDirectory
import threading
import unittest
from unittest import mock

from pocket_journal_partner import cli
from pocket_journal_partner.library import LIBRARY_SCHEMA_VERSION, NoteLibrary, stable_note_id
from pocket_journal_partner.storage import PartnerStore
import pocket_journal_partner.tui as tui_module
from pocket_journal_partner.tui import run_tui


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

    def move(self, y: int, x: int) -> None:
        del y, x

    def clrtoeol(self) -> None:
        pass

    def get_wch(self) -> object:
        if not self.keys:
            raise AssertionError("fake terminal ran out of keys")
        return self.keys.pop(0)

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
            audio = root / "audio" / "sample.wav"
            audio.parent.mkdir()
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
            audio = root / "audio" / "sample.wav"
            audio.parent.mkdir()
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

    def test_delete_retains_shared_audio_until_last_reference_is_removed(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            library = NoteLibrary(root)
            first = library.upsert_discovered("device", "first", "shared.wav")
            second = library.upsert_discovered("device", "second", "shared.wav")
            audio = root / "shared.wav"
            audio.write_bytes(b"RIFF")
            library.attach_audio(first.note_id, audio)
            library.attach_audio(second.note_id, audio)

            first_result = library.delete_note(first.note_id)

            self.assertIn("shared", first_result.audio_retained_reason or "")
            self.assertTrue(audio.exists())
            second_result = library.delete_note(second.note_id)
            self.assertTrue(second_result.audio_removed)
            self.assertFalse(audio.exists())

    def test_delete_commits_row_but_does_not_follow_unsafe_audio_path(self) -> None:
        with TemporaryDirectory() as tmp, TemporaryDirectory() as elsewhere:
            root = Path(tmp)
            library = NoteLibrary(root)
            note = library.upsert_discovered("device", "audio", "sample.wav")
            external = Path(elsewhere) / "outside.wav"
            external.write_bytes(b"do not delete")
            with sqlite3.connect(library.path) as connection:
                connection.execute(
                    "UPDATE notes SET audio_path = ? WHERE note_id = ?",
                    (f"../{Path(elsewhere).name}/outside.wav", note.note_id),
                )

            result = library.delete_note(note.note_id)

            self.assertIsNone(library.get(note.note_id))
            self.assertTrue(external.exists())
            self.assertTrue(result.cleanup_errors)

    def test_delete_reports_cleanup_failure_after_database_commit(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            library = NoteLibrary(root)
            note = library.upsert_discovered("device", "audio", "sample.wav")
            audio = root / "sample.wav"
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
            audio = root / "sample.wav"
            audio.write_bytes(b"RIFF")
            library.attach_audio(note.note_id, audio)
            opened: list[Path] = []
            keys: list[object] = [
                "/",
                *"sample",
                "\n",
                "\n",
                "p",
                "e",
                *("\b" * len("sample")),
                *"Renamed note",
                "\n",
                "d",
                *"DELETE",
                "\n",
                "q",
            ]
            screen = FakeScreen(keys)
            curses = FakeCurses(screen)

            result = run_tui(
                library,
                open_audio=lambda path: opened.append(path) or True,
                screen=screen,
                curses_module=curses,
            )
            self.assertEqual(result, 0)
            rendered = screen.rendered_text()
            self.assertIn("[audio] [text]", rendered)
            self.assertIn("Transcript?[2J remains readable", rendered)
            self.assertNotIn("\x1b", rendered)
            self.assertIn("Renamed note", rendered)
            self.assertEqual(opened, [audio.resolve()])
            self.assertIsNone(library.get(note.note_id))
            self.assertFalse(audio.exists())

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
