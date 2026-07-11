from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from pocket_journal_partner.storage import PartnerStore


class StoragePathTests(unittest.TestCase):
    def test_audio_path_is_contained_collision_safe_and_preserves_safe_suffix(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            store = PartnerStore(root)

            path = store.audio_path("..", "../../note", "../recording.WAV")
            colliding_without_length = store.audio_path("..", "../../note.WAV", "recording")
            encoded_delimiter = store.audio_path("..", "../../note%2FWAV", "recording")
            empty_id = store.audio_path("", "", "recording.wav")
            nul_id = store.audio_path("\0", "\0", "recording.wav")

            self.assertTrue(path.resolve().is_relative_to((root / "audio").resolve()))
            self.assertEqual(path.suffix, ".WAV")
            self.assertNotEqual(path, colliding_without_length)
            self.assertNotEqual(path, encoded_delimiter)
            self.assertNotEqual(empty_id, nul_id)
            self.assertNotIn("..", path.relative_to(root).parts)

    def test_unsafe_filename_suffix_is_not_copied(self) -> None:
        with TemporaryDirectory() as tmp:
            store = PartnerStore(Path(tmp))

            path = store.audio_path("device", "audio", "recording.not-an-extension")

            self.assertEqual(path.name, "5-audio")

    def test_job_path_is_contained_for_path_like_identifiers(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            store = PartnerStore(root)

            path = store.job_path("..", "../../job")

            self.assertTrue(path.resolve().is_relative_to((root / "jobs").resolve()))
            self.assertNotIn("..", path.relative_to(root).parts)


class JsonStorageTests(unittest.TestCase):
    def test_job_round_trip_preserves_versioned_payload(self) -> None:
        with TemporaryDirectory() as tmp:
            store = PartnerStore(Path(tmp))
            payload = {
                "schema_version": 1,
                "state": "downloaded",
                "attempts": 2,
                "metadata": {"model": "test"},
            }

            path = store.save_job("device", "audio", payload)

            self.assertEqual(store.load_job("device", "audio"), payload)
            self.assertEqual(path, store.job_path("device", "audio"))
            self.assertEqual(list(path.parent.iterdir()), [path])

    def test_job_save_atomically_replaces_existing_payload(self) -> None:
        with TemporaryDirectory() as tmp:
            store = PartnerStore(Path(tmp))
            store.save_job("device", "audio", {"schema_version": 1, "state": "new"})

            path = store.save_job(
                "device", "audio", {"schema_version": 1, "state": "uploaded"}
            )

            self.assertEqual(
                store.load_job("device", "audio"),
                {"schema_version": 1, "state": "uploaded"},
            )
            self.assertEqual(list(path.parent.iterdir()), [path])

    def test_invalid_and_non_object_job_json_return_none(self) -> None:
        with TemporaryDirectory() as tmp:
            store = PartnerStore(Path(tmp))
            path = store.job_path("device", "audio")
            path.parent.mkdir(parents=True)

            path.write_text("not json", encoding="utf-8")
            self.assertIsNone(store.load_job("device", "audio"))

            path.write_text('["valid", "but", "not", "an", "object"]', encoding="utf-8")
            self.assertIsNone(store.load_job("device", "audio"))

    def test_failed_replacement_keeps_old_payload_and_removes_temp_file(self) -> None:
        with TemporaryDirectory() as tmp:
            store = PartnerStore(Path(tmp))
            path = store.save_job("device", "audio", {"schema_version": 1, "state": "old"})

            with patch.object(Path, "replace", side_effect=OSError("replace failed")):
                with self.assertRaisesRegex(OSError, "replace failed"):
                    store.save_job("device", "audio", {"schema_version": 1, "state": "new"})

            self.assertEqual(store.load_job("device", "audio"), {"schema_version": 1, "state": "old"})
            self.assertEqual(list(path.parent.iterdir()), [path])

    def test_transcript_methods_remain_atomic_and_compatible(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            store = PartnerStore(root)

            path = store.save_transcript("device", "audio", {"text": "first"})
            store.save_transcript("device", "audio", {"text": "replacement"})

            self.assertEqual(path, root / "transcripts" / "device" / "audio.json")
            self.assertEqual(store.load_transcript("device", "audio"), {"text": "replacement"})
            self.assertEqual(list(path.parent.iterdir()), [path])


if __name__ == "__main__":
    unittest.main()
