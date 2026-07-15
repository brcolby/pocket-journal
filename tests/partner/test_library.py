from __future__ import annotations

from contextlib import redirect_stdout
from io import StringIO
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import threading
import unittest

from pocket_journal_partner import cli
from pocket_journal_partner.library import LIBRARY_SCHEMA_VERSION, NoteLibrary, stable_note_id
from pocket_journal_partner.storage import PartnerStore
from pocket_journal_partner.tui import run_tui


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

    def test_tui_reads_titles_and_opens_audio_without_shelling_out(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            library = NoteLibrary(root)
            note = library.upsert_discovered("device", "audio", "sample.wav")
            library.attach_transcript(note.note_id, {"text": "Transcript\x1b[2J remains readable"})
            audio = root / "sample.wav"
            audio.write_bytes(b"RIFF")
            library.attach_audio(note.note_id, audio)
            opened: list[Path] = []
            output = StringIO()

            result = run_tui(
                library,
                input_stream=StringIO("o 1\nt 1 Renamed note\nl 1\nq\n"),
                output_stream=output,
                open_audio=lambda path: opened.append(path) or True,
            )

        self.assertEqual(result, 0)
        rendered = output.getvalue()
        self.assertIn("Transcript?[2J remains readable", rendered)
        self.assertNotIn("\x1b", rendered)
        self.assertIn("Renamed note", output.getvalue())
        self.assertEqual(opened, [audio.resolve()])


if __name__ == "__main__":
    unittest.main()
