from __future__ import annotations

import hashlib
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from pocket_journal_partner.device import (
    AudioItem,
    DeviceError,
    SerialDeviceClient,
    USB_TRANSFER_CHUNK_BYTES,
    USB_TRANSCRIPT_CHUNK_BYTES,
)
from pocket_journal_partner.operations import DeviceSession


class UsbAudioListTests(unittest.TestCase):
    def test_list_uses_one_hex_safe_item_per_bounded_response(self) -> None:
        client = SerialDeviceClient("/dev/cu.test")
        calls: list[tuple[str, dict]] = []
        responses = iter([
            {
                "cursor": 0,
                "snapshot": 42,
                "next_cursor": 1,
                "done": False,
                "item": {
                    "audio_id_hex": "note-1".encode().hex(),
                    "filename_hex": "note-1.wav".encode().hex(),
                    "label_hex": "First note".encode().hex(),
                    "size": 123,
                    "data_bytes": 79,
                    "source_sha256": "a" * 64,
                    "created_at_hex": "2026-07-14T12:00:00Z".encode().hex(),
                    "duration_ms": 2500,
                    "synced": False,
                    "transcript_uploaded": False,
                },
            },
            {"cursor": 1, "snapshot": 42, "next_cursor": 1, "done": True},
        ])

        def request(command: str, **kwargs):  # type: ignore[no-untyped-def]
            calls.append((command, kwargs))
            return next(responses)

        client._request = request  # type: ignore[method-assign]
        items = client.list_audio()

        self.assertEqual(
            [command for command, _ in calls],
            [
                "PJ_AUDIO_LIST cursor=0 snapshot=0",
                "PJ_AUDIO_LIST cursor=1 snapshot=42",
            ],
        )
        self.assertTrue(all(call[1]["max_attempts"] == 2 for call in calls))
        self.assertEqual(items, [AudioItem(
            "note-1", "note-1.wav", label="First note", size=123, data_bytes=79,
            source_sha256="a" * 64, created_at="2026-07-14T12:00:00Z",
            duration_ms=2500,
        )])

    def test_list_rejects_snapshot_changes_and_path_filenames(self) -> None:
        for name, responses, message in (
            (
                "snapshot",
                [
                    {"cursor": 0, "snapshot": 1, "next_cursor": 1, "done": False,
                     "item": {"audio_id_hex": "a".encode().hex(), "filename_hex": "a.wav".encode().hex()}},
                    {"cursor": 1, "snapshot": 2, "next_cursor": 1, "done": True},
                ],
                "snapshot changed",
            ),
            (
                "filename",
                [{"cursor": 0, "snapshot": 1, "next_cursor": 1, "done": False,
                  "item": {"audio_id_hex": "a".encode().hex(), "filename_hex": "../a.wav".encode().hex()}}],
                "filename",
            ),
        ):
            with self.subTest(name=name):
                client = SerialDeviceClient("/dev/cu.test")
                iterator = iter(responses)
                client._request = lambda *args, **kwargs: next(iterator)  # type: ignore[method-assign]
                with self.assertRaisesRegex(DeviceError, message):
                    client.list_audio()


class UsbAudioReadTests(unittest.TestCase):
    def test_download_streams_chunks_and_verifies_digest_before_replace(self) -> None:
        content = bytes(index % 251 for index in range(USB_TRANSFER_CHUNK_BYTES + 17))
        digest = hashlib.sha256(content).hexdigest()
        item = AudioItem("note/1", "note.wav", size=len(content), source_sha256=digest)
        client = SerialDeviceClient("/dev/cu.test")
        commands: list[str] = []

        def request(command: str, **kwargs):  # type: ignore[no-untyped-def]
            commands.append(command)
            fields = dict(field.split("=", 1) for field in command.split()[1:])
            offset = int(fields["offset"])
            chunk = content[offset:offset + USB_TRANSFER_CHUNK_BYTES]
            return {
                "id_hex": item.audio_id.encode().hex(),
                "offset": offset,
                "total_bytes": len(content),
                "data_hex": chunk.hex(),
                "eof": offset + len(chunk) == len(content),
                "source_sha256": digest,
            }

        client._request = request  # type: ignore[method-assign]
        with TemporaryDirectory() as tmp:
            path = client.download_audio(item, Path(tmp))
            downloaded = path.read_bytes()
            leftovers = list(Path(tmp).glob("*.part"))

        self.assertEqual(downloaded, content)
        self.assertEqual(leftovers, [])
        self.assertEqual(len(commands), 2)
        self.assertIn("id_hex=6e6f74652f31", commands[0])
        self.assertIn("max_bytes=256", commands[0])
        self.assertIn(f"source_sha256={digest}", commands[0])

    def test_download_rejects_inconsistent_offset_and_removes_partial(self) -> None:
        client = SerialDeviceClient("/dev/cu.test")
        client._request = lambda *args, **kwargs: {  # type: ignore[method-assign]
            "id_hex": "note".encode().hex(),
            "offset": 1,
            "total_bytes": 1,
            "data_hex": "00",
            "eof": True,
        }
        with TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(DeviceError, "offset"):
                client.download_audio(AudioItem("note", "note.wav", size=1), Path(tmp))
            self.assertEqual(list(Path(tmp).iterdir()), [])

    def test_download_rejects_noncanonical_hex_and_removes_partial(self) -> None:
        client = SerialDeviceClient("/dev/cu.test")
        digest = hashlib.sha256(b"\xaf").hexdigest()
        client._request = lambda *args, **kwargs: {  # type: ignore[method-assign]
            "id_hex": "note".encode().hex(),
            "offset": 0,
            "total_bytes": 1,
            "data_hex": "AF",
            "eof": True,
            "source_sha256": digest,
        }
        with TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(DeviceError, "invalid audio data chunk"):
                client.download_audio(AudioItem("note", "note.wav", size=1), Path(tmp))
            self.assertEqual(list(Path(tmp).iterdir()), [])

    def test_download_requires_a_source_digest(self) -> None:
        client = SerialDeviceClient("/dev/cu.test")
        client._request = lambda *args, **kwargs: {  # type: ignore[method-assign]
            "id_hex": "note".encode().hex(),
            "offset": 0,
            "total_bytes": 1,
            "data_hex": "00",
            "eof": True,
        }
        with TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(DeviceError, "source digest"):
                client.download_audio(AudioItem("note", "note.wav", size=1), Path(tmp))
            self.assertEqual(list(Path(tmp).iterdir()), [])


class UsbTranscriptUploadTests(unittest.TestCase):
    def test_upload_uses_begin_chunk_commit_and_exact_digest(self) -> None:
        client = SerialDeviceClient("/dev/cu.test")
        transcript = {"text": "word " * 90, "model": "local"}
        commands: list[str] = []

        def request(command: str, **kwargs):  # type: ignore[no-untyped-def]
            commands.append(command)
            fields = dict(field.split("=", 1) for field in command.split()[1:])
            if command.startswith("PJ_TRANSCRIPT_BEGIN"):
                return {"upload_id": 7, "offset": 0, "accepted": True}
            if command.startswith("PJ_TRANSCRIPT_WRITE"):
                data = bytes.fromhex(fields["data_hex"])
                return {"upload_id": 7, "next_offset": int(fields["offset"]) + len(data)}
            if command.startswith("PJ_TRANSCRIPT_COMMIT"):
                payload = json.dumps(
                    transcript, ensure_ascii=False, separators=(",", ":"),
                    sort_keys=True, allow_nan=False,
                ).encode()
                self.assertEqual(fields["sha256"], hashlib.sha256(payload).hexdigest())
                return {"upload_id": 7, "committed": True, "bytes": len(payload)}
            self.fail(f"unexpected command {command}")

        client._request = request  # type: ignore[method-assign]
        client.upload_transcript("note", transcript)

        self.assertTrue(commands[0].startswith("PJ_TRANSCRIPT_BEGIN id_hex=6e6f7465 "))
        self.assertGreaterEqual(sum(command.startswith("PJ_TRANSCRIPT_WRITE") for command in commands), 2)
        self.assertTrue(commands[-1].startswith("PJ_TRANSCRIPT_COMMIT upload_id=7"))
        self.assertLessEqual(
            max(len(command.split("data_hex=", 1)[1]) for command in commands if "data_hex=" in command),
            USB_TRANSCRIPT_CHUNK_BYTES * 2,
        )
        worst_case = (
            f"PJ_TRANSCRIPT_WRITE upload_id={2**64 - 1} offset=65536 "
            f"data_hex={'f' * (USB_TRANSCRIPT_CHUNK_BYTES * 2)} "
            f"request_id={'a' * 32}\n"
        )
        self.assertLessEqual(len(worst_case.encode("ascii")), 512)

    def test_failed_chunk_triggers_best_effort_abort(self) -> None:
        client = SerialDeviceClient("/dev/cu.test")
        commands: list[str] = []

        def request(command: str, **kwargs):  # type: ignore[no-untyped-def]
            commands.append(command)
            if command.startswith("PJ_TRANSCRIPT_BEGIN"):
                return {"upload_id": 9, "offset": 0, "accepted": True}
            if command.startswith("PJ_TRANSCRIPT_ABORT"):
                return {"upload_id": 9, "aborted": True}
            raise DeviceError("link failed")

        client._request = request  # type: ignore[method-assign]
        with self.assertRaisesRegex(DeviceError, "link failed"):
            client.upload_transcript("note", {"text": "hello"})

        self.assertTrue(commands[-1].startswith("PJ_TRANSCRIPT_ABORT upload_id=9"))

    def test_device_session_advertises_usb_sync_capabilities(self) -> None:
        client = SerialDeviceClient("/dev/cu.test")
        client.status = lambda: {"api_version": 1}  # type: ignore[method-assign]
        client.list_audio = lambda: [AudioItem("note", "note.wav")]  # type: ignore[method-assign]
        session = DeviceSession("device", client)

        session.require("audio.sync")
        self.assertEqual(session.list_recordings()[0].audio_id, "note")


if __name__ == "__main__":
    unittest.main()
