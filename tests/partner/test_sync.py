from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
from contextlib import redirect_stdout
from io import StringIO
from types import SimpleNamespace
from unittest.mock import patch
import json
import struct
import unittest

from pocket_journal_partner import cli
from pocket_journal_partner.device import AudioItem
from pocket_journal_partner.storage import PartnerStore
from pocket_journal_partner.sync import DEVICE_TRANSCRIPT_MAX_BYTES, sync_device_audio


def wav_bytes(samples: bytes = b"\x00\x00\x01\x00") -> bytes:
    return struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF",
        36 + len(samples),
        b"WAVE",
        b"fmt ",
        16,
        1,
        1,
        16000,
        32000,
        2,
        16,
        b"data",
        len(samples),
    ) + samples


class FakeClient:
    def __init__(self, items: list[AudioItem] | None = None) -> None:
        payload = wav_bytes()
        self.items = items or [
            AudioItem("done.wav", "done.wav", synced=True),
            AudioItem("legacy-done.wav", "legacy-done.wav", transcript_uploaded=True),
            AudioItem("new.wav", "new.wav", size=len(payload), data_bytes=4, duration_ms=1),
        ]
        self.payloads = {item.audio_id: payload for item in self.items}
        self.downloaded: list[str] = []
        self.uploaded: list[str] = []
        self.upload_payloads: list[dict[str, object]] = []

    def list_audio(self) -> list[AudioItem]:
        return self.items

    def download_audio(self, item: AudioItem, target_dir: Path) -> Path:
        self.downloaded.append(item.audio_id)
        target_dir.mkdir(parents=True, exist_ok=True)
        path = target_dir / item.filename
        path.write_bytes(self.payloads[item.audio_id])
        return path

    def upload_transcript(self, audio_id: str, transcript: dict[str, object]) -> None:
        self.uploaded.append(audio_id)
        self.upload_payloads.append(transcript)


class FakeBackend:
    def __init__(self, model: str = "fake-v1", text: str = "transcript") -> None:
        self.model = model
        self.text = text
        self.calls = 0

    def fingerprint(self) -> dict[str, object]:
        return {"backend": "fake", "model": self.model, "parameters": {}}

    def transcribe(self, audio_path: Path) -> dict[str, object]:
        self.calls += 1
        return {"text": self.text, "model": self.model}


class SyncTests(unittest.TestCase):
    def test_sync_persists_verified_identity_and_uploaded_stage(self) -> None:
        with TemporaryDirectory() as tmp:
            client = FakeClient()
            backend = FakeBackend()
            store = PartnerStore(Path(tmp))
            results = sync_device_audio("pj-test", client, store, backend)  # type: ignore[arg-type]
            job = store.load_job("pj-test", "new.wav")
            transcript = store.load_transcript("pj-test", "new.wav")

        self.assertEqual(client.downloaded, ["new.wav"])
        self.assertEqual(client.uploaded, ["new.wav"])
        self.assertEqual(backend.calls, 1)
        self.assertEqual(results[0]["status"], "uploaded")
        self.assertEqual(job["stage"], "uploaded")  # type: ignore[index]
        self.assertEqual(job["attempt_count"], 1)  # type: ignore[index]
        self.assertEqual(job["source"]["sha256"], results[0]["source_sha256"])  # type: ignore[index]
        self.assertEqual(transcript["sync"]["source"], job["source"])  # type: ignore[index]
        self.assertNotIn("sync", client.upload_payloads[0])
        self.assertLessEqual(
            len(json.dumps(client.upload_payloads[0], separators=(",", ":")).encode()),
            DEVICE_TRANSCRIPT_MAX_BYTES,
        )

    def test_retry_reuses_verified_audio_and_matching_transcript(self) -> None:
        class InterruptedClient(FakeClient):
            def __init__(self) -> None:
                super().__init__()
                self.fail_upload = True

            def upload_transcript(self, audio_id: str, transcript: dict[str, object]) -> None:
                if self.fail_upload:
                    self.fail_upload = False
                    raise RuntimeError("interrupted")
                super().upload_transcript(audio_id, transcript)

        with TemporaryDirectory() as tmp:
            client = InterruptedClient()
            backend = FakeBackend()
            store = PartnerStore(Path(tmp))
            failed = sync_device_audio("pj-test", client, store, backend)  # type: ignore[arg-type]
            failed_job = store.load_job("pj-test", "new.wav")
            completed = sync_device_audio("pj-test", client, store, backend)  # type: ignore[arg-type]
            completed_job = store.load_job("pj-test", "new.wav")

        self.assertEqual(failed[0]["status"], "failed")
        self.assertEqual(failed[0]["stage"], "transcribed")
        self.assertEqual(failed[0]["operation"], "upload_transcript")
        self.assertTrue(failed[0]["retryable"])
        self.assertEqual(failed_job["last_error"]["message"], "interrupted")  # type: ignore[index]
        self.assertEqual(completed[0]["status"], "uploaded")
        self.assertEqual(completed_job["attempt_count"], 2)  # type: ignore[index]
        self.assertIsNone(completed_job["last_error"])  # type: ignore[index]
        self.assertEqual(client.downloaded, ["new.wav"])
        self.assertEqual(client.uploaded, ["new.wav"])
        self.assertEqual(backend.calls, 1)

    def test_fingerprint_change_invalidates_cached_transcript(self) -> None:
        with TemporaryDirectory() as tmp:
            client = FakeClient()
            store = PartnerStore(Path(tmp))
            first = FakeBackend("model-a")
            second = FakeBackend("model-b")
            sync_device_audio("pj-test", client, store, first)  # type: ignore[arg-type]
            sync_device_audio("pj-test", client, store, second)  # type: ignore[arg-type]
            transcript = store.load_transcript("pj-test", "new.wav")

        self.assertEqual(client.downloaded, ["new.wav"])
        self.assertEqual(first.calls, 1)
        self.assertEqual(second.calls, 1)
        self.assertEqual(transcript["model"], "model-b")  # type: ignore[index]

    def test_explicit_reprocess_includes_device_synced_note(self) -> None:
        payload = wav_bytes()
        item = AudioItem("done.wav", "done.wav", size=len(payload), data_bytes=4, synced=True)
        with TemporaryDirectory() as tmp:
            client = FakeClient([item])
            store = PartnerStore(Path(tmp))
            backend = FakeBackend("model-a")
            skipped = sync_device_audio("pj-test", client, store, backend)  # type: ignore[arg-type]
            processed = sync_device_audio(  # type: ignore[arg-type]
                "pj-test", client, store, backend, reprocess_synced=True
            )

        self.assertEqual(skipped, [])
        self.assertEqual(processed[0]["status"], "uploaded")
        self.assertEqual(client.downloaded, ["done.wav"])
        self.assertEqual(client.uploaded, ["done.wav"])

    def test_synced_fingerprint_change_requires_explicit_reprocess(self) -> None:
        payload = wav_bytes()
        item = AudioItem("note.wav", "note.wav", size=len(payload), data_bytes=4)
        with TemporaryDirectory() as tmp:
            client = FakeClient([item])
            store = PartnerStore(Path(tmp))
            first = FakeBackend("model-a")
            second = FakeBackend("model-b")
            sync_device_audio("pj-test", client, store, first)  # type: ignore[arg-type]
            item.synced = True
            skipped = sync_device_audio("pj-test", client, store, second)  # type: ignore[arg-type]
            processed = sync_device_audio(  # type: ignore[arg-type]
                "pj-test", client, store, second, reprocess_synced=True
            )

        self.assertEqual(skipped, [])
        self.assertEqual(second.calls, 1)
        self.assertEqual(processed[0]["model"], "model-b")
        self.assertEqual(client.downloaded, ["note.wav", "note.wav"])

    def test_complete_long_text_is_uploaded_without_truncation(self) -> None:
        with TemporaryDirectory() as tmp:
            client = FakeClient()
            store = PartnerStore(Path(tmp))
            results = sync_device_audio(  # type: ignore[arg-type]
                "pj-test", client, store, FakeBackend(text="x" * 20_000)
            )

        self.assertEqual(results[0]["status"], "uploaded")
        self.assertEqual(client.upload_payloads[0]["text"], "x" * 20_000)
        self.assertLessEqual(
            len(json.dumps(client.upload_payloads[0], separators=(",", ":")).encode()),
            DEVICE_TRANSCRIPT_MAX_BYTES,
        )

    def test_non_ascii_payload_uses_utf8_size_boundary(self) -> None:
        text = "\u00e9" * 30_000
        with TemporaryDirectory() as tmp:
            client = FakeClient()
            results = sync_device_audio(  # type: ignore[arg-type]
                "pj-test", client, PartnerStore(Path(tmp)), FakeBackend(text=text)
            )

        wire_bytes = json.dumps(
            client.upload_payloads[0], ensure_ascii=False, separators=(",", ":"), sort_keys=True
        ).encode("utf-8")
        self.assertEqual(results[0]["status"], "uploaded")
        self.assertEqual(client.upload_payloads[0]["text"], text)
        self.assertLessEqual(len(wire_bytes), DEVICE_TRANSCRIPT_MAX_BYTES)

    def test_local_only_transcription_does_not_upload_placeholder(self) -> None:
        with TemporaryDirectory() as tmp:
            client = FakeClient()
            store = PartnerStore(Path(tmp))
            results = sync_device_audio(  # type: ignore[arg-type]
                "pj-test", client, store, FakeBackend(), upload_transcripts=False
            )
            job = store.load_job("pj-test", "new.wav")

        self.assertEqual(results[0]["status"], "transcribed")
        self.assertEqual(job["stage"], "transcribed")  # type: ignore[index]
        self.assertEqual(client.uploaded, [])

    def test_changed_same_size_audio_invalidates_cached_transcript(self) -> None:
        with TemporaryDirectory() as tmp:
            client = FakeClient()
            store = PartnerStore(Path(tmp))
            backend = FakeBackend()
            first = sync_device_audio("pj-test", client, store, backend)  # type: ignore[arg-type]
            audio_path = store.audio_path("pj-test", "new.wav", "new.wav")
            audio_path.write_bytes(wav_bytes(b"\x02\x00\x03\x00"))
            second = sync_device_audio("pj-test", client, store, backend)  # type: ignore[arg-type]

        self.assertEqual(client.downloaded, ["new.wav"])
        self.assertEqual(backend.calls, 2)
        self.assertNotEqual(first[0]["source_sha256"], second[0]["source_sha256"])

    def test_invalid_audio_is_recorded_without_blocking_later_items(self) -> None:
        good = wav_bytes()
        items = [
            AudioItem("bad.wav", "bad.wav", size=4, data_bytes=0),
            AudioItem("good.wav", "good.wav", size=len(good), data_bytes=4),
        ]
        with TemporaryDirectory() as tmp:
            client = FakeClient(items)
            client.payloads["bad.wav"] = b"RIFF"
            backend = FakeBackend()
            store = PartnerStore(Path(tmp))
            results = sync_device_audio("pj-test", client, store, backend)  # type: ignore[arg-type]
            failed_job = store.load_job("pj-test", "bad.wav")

        self.assertEqual([result["status"] for result in results], ["failed", "uploaded"])
        self.assertEqual(results[0]["operation"], "verify_audio")
        self.assertTrue(results[0]["retryable"])
        self.assertEqual(failed_job["stage"], "discovered")  # type: ignore[index]
        self.assertEqual(client.uploaded, ["good.wav"])
        self.assertEqual(backend.calls, 1)

    def test_invalid_download_is_retried_unchanged(self) -> None:
        item = AudioItem("bad.wav", "bad.wav", size=4, data_bytes=0)
        with TemporaryDirectory() as tmp:
            client = FakeClient([item])
            client.payloads["bad.wav"] = b"RIFF"
            store = PartnerStore(Path(tmp))
            first = sync_device_audio("pj-test", client, store, FakeBackend())  # type: ignore[arg-type]
            second = sync_device_audio("pj-test", client, store, FakeBackend())  # type: ignore[arg-type]
            job = store.load_job("pj-test", "bad.wav")

        self.assertTrue(first[0]["retryable"])
        self.assertNotIn("cached", second[0])
        self.assertEqual(client.downloaded, ["bad.wav", "bad.wav"])
        self.assertEqual(job["attempt_count"], 2)  # type: ignore[index]

    def test_initial_job_save_failure_does_not_block_later_items(self) -> None:
        class SelectiveFailStore(PartnerStore):
            def save_job(self, device_id, audio_id, job):  # type: ignore[no-untyped-def]
                if audio_id == "bad.wav":
                    raise OSError("disk unavailable")
                return super().save_job(device_id, audio_id, job)

        payload = wav_bytes()
        items = [
            AudioItem("bad.wav", "bad.wav", size=len(payload), data_bytes=4),
            AudioItem("good.wav", "good.wav", size=len(payload), data_bytes=4),
        ]
        with TemporaryDirectory() as tmp:
            client = FakeClient(items)
            results = sync_device_audio(  # type: ignore[arg-type]
                "pj-test", client, SelectiveFailStore(Path(tmp)), FakeBackend()
            )

        self.assertEqual([result["status"] for result in results], ["failed", "uploaded"])
        self.assertEqual(results[0]["operation"], "save_job")
        self.assertIn("disk unavailable", results[0]["error"])
        self.assertEqual(client.uploaded, ["good.wav"])

    def test_legacy_transcript_without_identity_is_not_reused(self) -> None:
        with TemporaryDirectory() as tmp:
            client = FakeClient()
            backend = FakeBackend()
            store = PartnerStore(Path(tmp))
            store.save_transcript("pj-test", "new.wav", {"text": "stale", "model": "legacy"})
            sync_device_audio("pj-test", client, store, backend)  # type: ignore[arg-type]
            transcript = store.load_transcript("pj-test", "new.wav")

        self.assertEqual(backend.calls, 1)
        self.assertEqual(transcript["model"], "fake-v1")  # type: ignore[index]
        self.assertIn("sync", transcript)  # type: ignore[operator]

    def test_empty_backend_result_is_persisted_as_item_failure(self) -> None:
        with TemporaryDirectory() as tmp:
            client = FakeClient()
            store = PartnerStore(Path(tmp))
            results = sync_device_audio(
                "pj-test", client, store, FakeBackend(text="   ")  # type: ignore[arg-type]
            )
            job = store.load_job("pj-test", "new.wav")

        self.assertEqual(results[0]["status"], "failed")
        self.assertEqual(results[0]["stage"], "audio_verified")
        self.assertEqual(results[0]["operation"], "transcribe")
        self.assertIn("empty text", results[0]["error"])
        self.assertEqual(job["last_error"]["operation"], "transcribe")  # type: ignore[index]

    def test_cli_reports_item_failures_and_returns_failure_exit(self) -> None:
        class FakeSession:
            device_id = "pj-test"
            client = object()

            def require(self, capability: str) -> None:
                self.capability = capability

            def envelope(self, result: dict[str, object]) -> dict[str, object]:
                return {"device_id": self.device_id, "result": result}

        results = [
            {"audio_id": "good.wav", "status": "uploaded"},
            {"audio_id": "bad.wav", "status": "failed", "error": "invalid WAV"},
        ]
        args = SimpleNamespace(
            data_dir=None, backend="fake", allow_fake_upload=False, reprocess=False
        )
        stdout = StringIO()
        with patch("pocket_journal_partner.cli._lan_session_from_args", return_value=FakeSession()):
            with patch("pocket_journal_partner.cli.sync_device_audio", return_value=results):
                with redirect_stdout(stdout):
                    exit_code = cli.cmd_sync(args)

        payload = json.loads(stdout.getvalue())
        self.assertEqual(exit_code, 1)
        self.assertEqual(payload["result"]["uploaded_count"], 1)
        self.assertEqual(payload["result"]["failed_count"], 1)
        self.assertEqual(payload["result"]["synced"], results)
        self.assertEqual(payload["result"]["count"], 2)
        self.assertEqual(payload["result"]["results"], results)


if __name__ == "__main__":
    unittest.main()
