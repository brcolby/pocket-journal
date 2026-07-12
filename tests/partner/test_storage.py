from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
import hashlib
import multiprocessing
from pathlib import Path
from tempfile import TemporaryDirectory
import threading
import unittest
from unittest.mock import patch
from urllib.parse import quote

from pocket_journal_partner.storage import PartnerStore


def _save_job_in_process(root: str, value: int) -> None:
    PartnerStore(Path(root)).save_job("device", "audio", {"value": value})


def _visible_items(path: Path) -> list[Path]:
    return [item for item in path.iterdir() if item.name != ".locks"]


def _legacy_component(value: str) -> str:
    encoded = quote(value, safe="")
    digest = hashlib.sha256(value.encode("utf-8", "surrogatepass")).hexdigest()[:16]
    return f"{digest}-{encoded or '%EMPTY'}"


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

            self.assertFalse(path.name.endswith(".not-an-extension"))

    def test_case_distinct_identifiers_have_distinct_persisted_paths(self) -> None:
        with TemporaryDirectory() as tmp:
            store = PartnerStore(Path(tmp))

            self.assertNotEqual(store.audio_dir("Device"), store.audio_dir("device"))
            self.assertNotEqual(
                store.audio_path("device", "Audio", "recording.wav"),
                store.audio_path("device", "audio", "recording.wav"),
            )
            self.assertNotEqual(
                store.transcript_path("device", "Audio"),
                store.transcript_path("device", "audio"),
            )
            self.assertNotEqual(
                store.job_path("device", "Audio"),
                store.job_path("device", "audio"),
            )

    def test_unicode_normalization_distinct_identifiers_have_distinct_paths(self) -> None:
        with TemporaryDirectory() as tmp:
            store = PartnerStore(Path(tmp))
            composed = "caf\u00e9"
            decomposed = "cafe\u0301"

            self.assertNotEqual(store.audio_dir(composed), store.audio_dir(decomposed))
            self.assertNotEqual(
                store.audio_path("device", composed, "recording.wav"),
                store.audio_path("device", decomposed, "recording.wav"),
            )
            self.assertNotEqual(
                store.transcript_path("device", composed),
                store.transcript_path("device", decomposed),
            )
            self.assertNotEqual(
                store.job_path("device", composed),
                store.job_path("device", decomposed),
            )

    def test_job_path_is_contained_for_path_like_identifiers(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            store = PartnerStore(root)

            path = store.job_path("..", "../../job")

            self.assertTrue(path.resolve().is_relative_to((root / "jobs").resolve()))
            self.assertNotIn("..", path.relative_to(root).parts)

    def test_long_identifiers_are_bounded_and_digest_distinct(self) -> None:
        with TemporaryDirectory() as tmp:
            store = PartnerStore(Path(tmp))
            common = "a" * 10_000
            paths = [
                store.audio_path(common + "x", common + "y", "recording.wav"),
                store.transcript_path(common + "x", common + "z"),
                store.job_path(common + "x", common + "w"),
            ]

            for path in paths:
                self.assertTrue(all(len(part.encode("utf-8")) <= 180 for part in path.parts))
            self.assertNotEqual(
                store.job_path("device", common + "x"),
                store.job_path("device", common + "y"),
            )

    def test_prior_cache_paths_are_migrated_and_remain_visible(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            store = PartnerStore(root)
            device_component = _legacy_component("device")
            audio_component = _legacy_component("audio")
            legacy_audio = (
                root
                / "audio"
                / device_component
                / f"{len(audio_component)}-{audio_component}.wav"
            )
            legacy_job = root / "jobs" / device_component / f"{audio_component}.json"
            legacy_transcript = (
                root / "transcripts" / device_component / f"{audio_component}.json"
            )
            legacy_audio.parent.mkdir(parents=True)
            legacy_job.parent.mkdir(parents=True)
            legacy_transcript.parent.mkdir(parents=True)
            legacy_audio.write_bytes(b"old audio")
            legacy_job.write_text('{"state": "downloaded"}', encoding="utf-8")
            legacy_transcript.write_text('{"text": "cached"}', encoding="utf-8")

            audio_path = store.audio_path("device", "audio", "recording.wav")

            self.assertEqual(audio_path.read_bytes(), b"old audio")
            self.assertEqual(store.load_job("device", "audio"), {"state": "downloaded"})
            self.assertEqual(store.load_transcript("device", "audio"), {"text": "cached"})
            self.assertFalse(legacy_audio.exists())
            self.assertFalse(legacy_job.exists())
            self.assertFalse(legacy_transcript.exists())
            self.assertTrue(store.job_path("device", "audio").exists())
            self.assertTrue(store.transcript_path("device", "audio").exists())


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
            self.assertEqual(_visible_items(path.parent), [path])

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
            self.assertEqual(_visible_items(path.parent), [path])

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
            self.assertEqual(_visible_items(path.parent), [path])

    def test_transcript_methods_remain_atomic_and_compatible(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            store = PartnerStore(root)

            path = store.save_transcript("device", "audio", {"text": "first"})
            store.save_transcript("device", "audio", {"text": "replacement"})

            self.assertEqual(path, store.transcript_path("device", "audio"))
            self.assertEqual(store.load_transcript("device", "audio"), {"text": "replacement"})
            self.assertEqual(_visible_items(path.parent), [path])

    def test_concurrent_thread_replacements_publish_complete_json(self) -> None:
        with TemporaryDirectory() as tmp:
            store = PartnerStore(Path(tmp))
            payloads = [
                {"value": value, "text": str(value) * 1_000} for value in range(20)
            ]

            with ThreadPoolExecutor(max_workers=8) as executor:
                list(
                    executor.map(
                        lambda payload: store.save_job("device", "audio", payload),
                        payloads,
                    )
                )

            self.assertIn(store.load_job("device", "audio"), payloads)
            path = store.job_path("device", "audio")
            self.assertEqual(_visible_items(path.parent), [path])

    def test_concurrent_process_replacements_publish_complete_json(self) -> None:
        with TemporaryDirectory() as tmp:
            context = multiprocessing.get_context("spawn")
            processes = [
                context.Process(target=_save_job_in_process, args=(tmp, value))
                for value in range(6)
            ]
            for process in processes:
                process.start()
            for process in processes:
                process.join(10)
                self.assertEqual(process.exitcode, 0)

            self.assertIn(
                PartnerStore(Path(tmp)).load_job("device", "audio"),
                [{"value": value} for value in range(6)],
            )

    def test_temp_file_creation_failure_does_not_replace_existing_payload(self) -> None:
        with TemporaryDirectory() as tmp:
            store = PartnerStore(Path(tmp))
            store.save_job("device", "audio", {"state": "old"})

            with patch(
                "pocket_journal_partner.storage.tempfile.NamedTemporaryFile",
                side_effect=OSError("disk full"),
            ):
                with self.assertRaisesRegex(OSError, "disk full"):
                    store.save_job("device", "audio", {"state": "new"})

            self.assertEqual(store.load_job("device", "audio"), {"state": "old"})

    def test_workflow_lock_serializes_holders_and_allows_json_saves(self) -> None:
        with TemporaryDirectory() as tmp:
            store = PartnerStore(Path(tmp))
            first_acquired = threading.Event()
            release_first = threading.Event()
            second_acquired = threading.Event()

            def first_holder() -> None:
                with store.workflow_lock("device", "audio"):
                    store.save_job("device", "audio", {"holder": "first"})
                    first_acquired.set()
                    self.assertTrue(release_first.wait(5))

            def second_holder() -> None:
                self.assertTrue(first_acquired.wait(5))
                with store.workflow_lock("device", "audio"):
                    second_acquired.set()
                    store.save_job("device", "audio", {"holder": "second"})

            first = threading.Thread(target=first_holder)
            second = threading.Thread(target=second_holder)
            first.start()
            second.start()
            self.assertTrue(first_acquired.wait(5))
            self.assertFalse(second_acquired.wait(0.1))
            release_first.set()
            first.join(5)
            second.join(5)

            self.assertFalse(first.is_alive())
            self.assertFalse(second.is_alive())
            self.assertTrue(second_acquired.is_set())
            self.assertEqual(store.load_job("device", "audio"), {"holder": "second"})


if __name__ == "__main__":
    unittest.main()
