from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from pocket_journal_partner.device import AudioItem
from pocket_journal_partner.storage import PartnerStore
from pocket_journal_partner.sync import sync_device_audio


class FakeClient:
    def __init__(self) -> None:
        self.downloaded: list[str] = []
        self.uploaded: list[str] = []

    def list_audio(self) -> list[AudioItem]:
        return [
            AudioItem("done.wav", "done.wav", synced=True),
            AudioItem("legacy-done.wav", "legacy-done.wav", transcript_uploaded=True),
            AudioItem("new.wav", "new.wav", duration_ms=2000),
        ]

    def download_audio(self, item: AudioItem, target_dir: Path) -> Path:
        self.downloaded.append(item.audio_id)
        target_dir.mkdir(parents=True, exist_ok=True)
        path = target_dir / item.filename
        path.write_bytes(b"RIFF")
        return path

    def upload_transcript(self, audio_id: str, transcript: dict[str, str]) -> None:
        self.uploaded.append(audio_id)


class FakeBackend:
    def __init__(self) -> None:
        self.calls = 0

    def transcribe(self, audio_path: Path) -> dict[str, str]:
        self.calls += 1
        return {"text": audio_path.stem, "model": "fake"}


class SyncTests(unittest.TestCase):
    def test_synced_notes_are_not_downloaded_or_transcribed_again(self) -> None:
        with TemporaryDirectory() as tmp:
            client = FakeClient()
            backend = FakeBackend()
            results = sync_device_audio(
                "pj-test",
                client,  # type: ignore[arg-type]
                PartnerStore(Path(tmp)),
                backend,  # type: ignore[arg-type]
            )

        self.assertEqual(client.downloaded, ["new.wav"])
        self.assertEqual(client.uploaded, ["new.wav"])
        self.assertEqual(backend.calls, 1)
        self.assertEqual([result["audio_id"] for result in results], ["new.wav"])

    def test_retry_reuses_download_and_transcript_after_upload_failure(self) -> None:
        class InterruptedClient(FakeClient):
            def __init__(self) -> None:
                super().__init__()
                self.fail_upload = True

            def upload_transcript(self, audio_id: str, transcript: dict[str, str]) -> None:
                if self.fail_upload:
                    self.fail_upload = False
                    raise RuntimeError("interrupted")
                super().upload_transcript(audio_id, transcript)

        with TemporaryDirectory() as tmp:
            client = InterruptedClient()
            backend = FakeBackend()
            store = PartnerStore(Path(tmp))
            with self.assertRaisesRegex(RuntimeError, "interrupted"):
                sync_device_audio("pj-test", client, store, backend)  # type: ignore[arg-type]
            results = sync_device_audio("pj-test", client, store, backend)  # type: ignore[arg-type]

        self.assertEqual(client.downloaded, ["new.wav"])
        self.assertEqual(client.uploaded, ["new.wav"])
        self.assertEqual(backend.calls, 1)
        self.assertEqual([result["audio_id"] for result in results], ["new.wav"])


if __name__ == "__main__":
    unittest.main()
