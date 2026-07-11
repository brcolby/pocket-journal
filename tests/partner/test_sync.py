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
from pocket_journal_partner.sync import sync_device_audio


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
        self.assertFalse(results[0]["retryable"])
        self.assertEqual(failed_job["stage"], "discovered")  # type: ignore[index]
        self.assertEqual(client.uploaded, ["good.wav"])
        self.assertEqual(backend.calls, 1)

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
        args = SimpleNamespace(data_dir=None, backend="fake")
        stdout = StringIO()
        with patch("pocket_journal_partner.cli._lan_session_from_args", return_value=FakeSession()):
            with patch("pocket_journal_partner.cli.sync_device_audio", return_value=results):
                with redirect_stdout(stdout):
                    exit_code = cli.cmd_sync(args)

        payload = json.loads(stdout.getvalue())
        self.assertEqual(exit_code, 1)
        self.assertEqual(payload["result"]["uploaded_count"], 1)
        self.assertEqual(payload["result"]["failed_count"], 1)
        self.assertEqual(payload["result"]["results"], results)


if __name__ == "__main__":
    unittest.main()
