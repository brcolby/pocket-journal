from __future__ import annotations

from collections import Counter
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import time
import unittest

from pocket_journal_partner.device import (
    SerialDeviceClient,
    USB_MAX_AUDIO_READ_CHUNK_BYTES,
    USB_SERIAL_LINE_BYTES,
    USB_TRANSFER_CHUNK_BYTES,
)


class FirmwareUsbProtocolFixture:
    """Executable fixture for the JSON emitted by pj_board.c USB handlers."""

    def __init__(
        self, audio_read_max_bytes: int | None = USB_MAX_AUDIO_READ_CHUNK_BYTES,
    ) -> None:
        self.audio = {
            "rec-a.wav": b"RIFF" + bytes(index % 251 for index in range(300)),
            "rec-b.wav": b"RIFF" + bytes(index % 239 for index in range(37)),
        }
        self.snapshot = 2_145_301_337
        self.received_lines: list[str] = []
        self.drop_ack = Counter({
            "PJ_SETTINGS_SET": 1,
            "PJ_TRANSCRIPT_BEGIN": 1,
            "PJ_TRANSCRIPT_WRITE": 1,
            "PJ_TRANSCRIPT_COMMIT": 1,
        })
        self.upload_id = 4_294_967_295
        self.upload_request_id: str | None = None
        self.upload_audio_id: str | None = None
        self.upload_expected_bytes = 0
        self.upload_sha256: str | None = None
        self.upload = bytearray()
        self.last_write: tuple[int, bytes] | None = None
        self.committed = False
        self.settings: dict[str, object] = {
            "theme": "light",
            "volume": 8,
            "alarm_enabled": False,
            "alarm_hour": 7,
            "alarm_minute": 30,
            "timer_seconds": 300,
            "interval_seconds": 90,
            "clock_24h": True,
            "temperature_unit": "c",
            "transcript_font_size": 3,
            "sync_pending": 0,
            "sync_transferred": 0,
        }
        self.settings_generation = 12
        self.settings_request: tuple[str, int, str] | None = None
        self.audio_read_max_bytes = audio_read_max_bytes

    @staticmethod
    def _parse(line: str) -> tuple[str, dict[str, str]]:
        tokens = line.split(" ")
        command = tokens[0]
        pairs = [token.split("=", 1) for token in tokens[1:]]
        if any(len(pair) != 2 or not pair[0] or not pair[1] for pair in pairs):
            raise AssertionError(f"malformed command line: {line}")
        fields = dict(pairs)
        if len(fields) != len(pairs):
            raise AssertionError(f"duplicate command field: {line}")
        return command, fields

    def _response(
        self,
        command: str,
        request_id: str,
        fields: dict[str, object],
    ) -> bytes | None:
        payload = {"command": command, "request_id": request_id, **fields}
        encoded = (
            "PJ_OK "
            + json.dumps(payload, ensure_ascii=True, separators=(",", ":"))
            + "\n"
        ).encode("ascii")
        if self.drop_ack[command] > 0:
            self.drop_ack[command] -= 1
            return None
        return encoded

    def handle(self, wire: bytes) -> bytes | None:
        if not wire.endswith(b"\n") or len(wire) >= USB_SERIAL_LINE_BYTES:
            raise AssertionError("host emitted a line the firmware reader cannot accept")
        line = wire[:-1].decode("ascii")
        self.received_lines.append(line)
        command, fields = self._parse(line)
        request_id = fields.pop("request_id")
        if not 1 <= len(request_id) <= 32 or any(
            not (character.isalnum() or character in "-_.")
            for character in request_id
        ):
            raise AssertionError("host emitted an invalid firmware request id")

        if command == "PJ_SETTINGS_GET":
            if fields:
                raise AssertionError(f"unexpected settings read fields: {fields}")
            return self._response(command, request_id, {
                **self.settings,
                "generation": self.settings_generation,
                "changed": False,
                "replayed": False,
            })

        if command == "PJ_SETTINGS_SET":
            if set(fields) != {"expected_generation", "payload_hex"}:
                raise AssertionError(f"unexpected settings update fields: {fields}")
            expected = int(fields["expected_generation"])
            payload_hex = fields["payload_hex"]
            replay = self.settings_request == (request_id, expected, payload_hex)
            if not replay:
                if self.settings_request is not None and self.settings_request[0] == request_id:
                    raise AssertionError("host reused a settings request id with different content")
                if expected != self.settings_generation:
                    raise AssertionError("host failed to pin the settings generation")
                update = json.loads(bytes.fromhex(payload_hex).decode("ascii"))
                if not isinstance(update, dict) or not update:
                    raise AssertionError("host emitted an invalid settings update")
                self.settings.update(update)
                self.settings_generation += 1
                self.settings_request = (request_id, expected, payload_hex)
            return self._response(command, request_id, {
                **self.settings,
                "generation": self.settings_generation,
                "changed": not replay,
                "replayed": replay,
            })

        if command == "PJ_AUDIO_LIST":
            if set(fields) != {"cursor", "snapshot"}:
                raise AssertionError(f"unexpected audio list fields: {fields}")
            cursor = int(fields["cursor"])
            requested_snapshot = int(fields["snapshot"])
            if requested_snapshot not in {0, self.snapshot}:
                raise AssertionError("host failed to pin the firmware list snapshot")
            names = list(self.audio)
            if not 0 <= cursor <= len(names):
                raise AssertionError("host emitted an invalid list cursor")
            next_cursor = cursor + 1 if cursor < len(names) else cursor
            response: dict[str, object] = {
                "snapshot": self.snapshot,
                "cursor": cursor,
                "next_cursor": next_cursor,
                # pj_board.c sets done on the response carrying the final item.
                "done": next_cursor >= len(names),
            }
            if self.audio_read_max_bytes is not None:
                response["audio_read_max_bytes"] = self.audio_read_max_bytes
            if cursor < len(names):
                name = names[cursor]
                content = self.audio[name]
                item: dict[str, object] = {
                    "audio_id_hex": name.encode().hex(),
                    "filename_hex": name.encode().hex(),
                    "label_hex": f"NOTE {cursor + 1}".encode().hex(),
                    "size": len(content),
                    "data_bytes": max(0, len(content) - 44),
                    "source_sha256": (
                        None if cursor == 0 else hashlib.sha256(content).hexdigest()
                    ),
                    "created_at_hex": f"2026-07-15T00:0{cursor}:00".encode().hex(),
                    "duration_ms": 1000 + cursor,
                    "synced": cursor == 1,
                    "transcript_uploaded": cursor == 1,
                }
                if cursor == 1:
                    item["transcript_path_hex"] = (
                        f"/sdcard/pj/transcripts/{name}.json".encode().hex()
                    )
                response["item"] = item
            return self._response(command, request_id, response)

        if command == "PJ_AUDIO_READ":
            required = {"id_hex", "offset", "max_bytes"}
            if (
                not required.issubset(fields)
                or not set(fields) - required <= {"source_sha256"}
            ):
                raise AssertionError(f"unexpected audio read fields: {fields}")
            audio_id = bytes.fromhex(fields["id_hex"]).decode("utf-8")
            content = self.audio[audio_id]
            digest = hashlib.sha256(content).hexdigest()
            offset = int(fields["offset"])
            if "source_sha256" in fields and fields["source_sha256"] != digest:
                raise AssertionError("host did not pin the listed source digest")
            if offset > 0 and fields.get("source_sha256") != digest:
                raise AssertionError("host did not pin the source digest after the first chunk")
            maximum = int(fields["max_bytes"])
            supported = self.audio_read_max_bytes or USB_TRANSFER_CHUNK_BYTES
            if not 1 <= maximum <= supported:
                raise AssertionError("host requested an oversized audio chunk")
            chunk = content[offset : offset + maximum]
            return self._response(command, request_id, {
                "id_hex": fields["id_hex"],
                "offset": offset,
                "total_bytes": len(content),
                "data_hex": chunk.hex(),
                "eof": offset + len(chunk) == len(content),
                "source_sha256": digest,
            })

        if command == "PJ_TRANSCRIPT_BEGIN":
            if set(fields) != {"id_hex", "bytes", "sha256"}:
                raise AssertionError(f"unexpected transcript begin fields: {fields}")
            audio_id = bytes.fromhex(fields["id_hex"]).decode("utf-8")
            if audio_id not in self.audio:
                raise AssertionError("host began an upload for unknown audio")
            expected_bytes = int(fields["bytes"])
            if self.upload_request_id is None:
                self.upload_request_id = request_id
                self.upload_audio_id = audio_id
                self.upload_expected_bytes = expected_bytes
                self.upload_sha256 = fields["sha256"]
                attached = False
            else:
                if (
                    request_id != self.upload_request_id
                    or audio_id != self.upload_audio_id
                    or expected_bytes != self.upload_expected_bytes
                    or fields["sha256"] != self.upload_sha256
                ):
                    raise AssertionError("host did not replay transcript begin identically")
                attached = True
            return self._response(command, request_id, {
                "upload_id": self.upload_id,
                "offset": len(self.upload),
                "accepted": True,
                "attached": attached,
            })

        if command == "PJ_TRANSCRIPT_WRITE":
            if set(fields) != {"upload_id", "offset", "data_hex"}:
                raise AssertionError(f"unexpected transcript write fields: {fields}")
            if int(fields["upload_id"]) != self.upload_id:
                raise AssertionError("host changed upload id")
            offset = int(fields["offset"])
            data = bytes.fromhex(fields["data_hex"])
            replayed = False
            if offset == len(self.upload):
                self.upload.extend(data)
                self.last_write = (offset, data)
            elif self.last_write == (offset, data):
                replayed = True
            else:
                raise AssertionError("host did not replay the last chunk identically")
            return self._response(command, request_id, {
                "upload_id": self.upload_id,
                "next_offset": len(self.upload),
                "replayed": replayed,
            })

        if command == "PJ_TRANSCRIPT_COMMIT":
            if set(fields) != {"upload_id", "sha256"}:
                raise AssertionError(f"unexpected transcript commit fields: {fields}")
            if (
                int(fields["upload_id"]) != self.upload_id
                or len(self.upload) != self.upload_expected_bytes
                or hashlib.sha256(self.upload).hexdigest() != fields["sha256"]
                or fields["sha256"] != self.upload_sha256
            ):
                raise AssertionError("host committed an incomplete transcript")
            payload = json.loads(self.upload.decode("utf-8"))
            if not isinstance(payload.get("text"), str) or not payload["text"].strip():
                raise AssertionError("fixture transcript does not satisfy firmware validation")
            self.committed = True
            return self._response(command, request_id, {
                "upload_id": self.upload_id,
                "committed": True,
                "bytes": len(self.upload),
            })

        raise AssertionError(f"unexpected command: {command}")


class FixtureConnection:
    def __init__(self, fixture: FirmwareUsbProtocolFixture) -> None:
        self.fixture = fixture
        self.responses: list[bytes] = []
        self.timeout = 0.0

    def write(self, payload: bytes) -> int:
        response = self.fixture.handle(payload)
        if response is not None:
            self.responses.append(response)
        return len(payload)

    def flush(self) -> None:
        pass

    def readline(self) -> bytes:
        return self.responses.pop(0) if self.responses else b""


class FirmwareFixtureClient(SerialDeviceClient):
    def __init__(self, fixture: FirmwareUsbProtocolFixture) -> None:
        super().__init__("/dev/cu.fixture", timeout=0.1)
        self.fixture = fixture

    def _request(  # type: ignore[override]
        self,
        command: str,
        *,
        timeout: float | None = None,
        deadline: float | None = None,
        request_id: str | None = None,
        retry_interval: float | None = None,
        max_attempts: int = 1,
    ) -> dict[str, object]:
        del timeout, deadline
        connection = FixtureConnection(self.fixture)
        response = self._request_on_connection(
            connection,
            command,
            deadline=time.monotonic() + 0.1,
            request_id=request_id,
            retry_interval=0.001 if retry_interval is not None else None,
            max_attempts=max_attempts,
        )
        if response is None:
            raise AssertionError(f"fixture timed out handling {command}")
        return response

    @contextmanager
    def _serial_request_sequence(self):  # type: ignore[no-untyped-def]
        connection = FixtureConnection(self.fixture)

        def request_command(
            command: str,
            *,
            request_id: str | None = None,
            retry_interval: float | None = None,
            max_attempts: int = 1,
        ) -> dict[str, object]:
            response = self._request_on_connection(
                connection,
                command,
                deadline=time.monotonic() + 0.1,
                request_id=request_id,
                retry_interval=0.001 if retry_interval is not None else None,
                max_attempts=max_attempts,
            )
            if response is None:
                raise AssertionError(f"fixture timed out handling {command}")
            return response

        yield request_command


class FirmwareUsbProtocolParityTests(unittest.TestCase):
    def test_real_host_parser_matches_firmware_settings_frames_and_replay(self) -> None:
        fixture = FirmwareUsbProtocolFixture()
        client = FirmwareFixtureClient(fixture)

        self.assertEqual(client.get_settings()["generation"], 12)
        updated = client.put_settings({"volume": 9, "theme": "dark"})

        self.assertEqual(updated["generation"], 13)
        self.assertEqual(updated["volume"], 9)
        setting_lines = [
            line for line in fixture.received_lines if line.startswith("PJ_SETTINGS_SET ")
        ]
        self.assertEqual(len(setting_lines), 2)
        self.assertEqual(setting_lines[0], setting_lines[1])

    def test_real_host_parser_matches_firmware_list_read_and_upload_frames(self) -> None:
        fixture = FirmwareUsbProtocolFixture()
        client = FirmwareFixtureClient(fixture)

        items = client.list_audio()

        self.assertEqual([item.audio_id for item in items], ["rec-a.wav", "rec-b.wav"])
        self.assertIsNone(items[0].source_sha256)
        self.assertTrue(items[1].transcript_uploaded)
        self.assertEqual(
            items[1].transcript_path,
            "/sdcard/pj/transcripts/rec-b.wav.json",
        )
        list_lines = [
            line for line in fixture.received_lines if line.startswith("PJ_AUDIO_LIST ")
        ]
        self.assertEqual(len(list_lines), 2)
        self.assertIn("cursor=0 snapshot=0", list_lines[0])
        self.assertIn(f"cursor=1 snapshot={fixture.snapshot}", list_lines[1])

        with TemporaryDirectory() as temporary:
            downloaded = client.download_audio(items[0], Path(temporary))
            self.assertEqual(downloaded.read_bytes(), fixture.audio[items[0].audio_id])
        audio_read_lines = [
            line for line in fixture.received_lines if line.startswith("PJ_AUDIO_READ ")
        ]
        self.assertEqual(len(audio_read_lines), 1)
        self.assertIn("max_bytes=1024", audio_read_lines[0])

        transcript = {"model": "fixture", "text": "word " * 80}
        client.upload_transcript(items[0].audio_id, transcript)

        self.assertTrue(fixture.committed)
        self.assertEqual(json.loads(fixture.upload.decode("utf-8")), transcript)
        counts = Counter(fixture.received_lines)
        self.assertEqual(
            sorted(count for line, count in counts.items() if line.startswith("PJ_TRANSCRIPT_BEGIN ")),
            [2],
        )
        self.assertIn(
            2,
            [count for line, count in counts.items() if line.startswith("PJ_TRANSCRIPT_WRITE ")],
        )
        self.assertEqual(
            sorted(count for line, count in counts.items() if line.startswith("PJ_TRANSCRIPT_COMMIT ")),
            [2],
        )

    def test_old_firmware_without_chunk_capability_uses_legacy_reads(self) -> None:
        fixture = FirmwareUsbProtocolFixture(audio_read_max_bytes=None)
        client = FirmwareFixtureClient(fixture)

        item = client.list_audio()[0]
        with TemporaryDirectory() as temporary:
            downloaded = client.download_audio(item, Path(temporary))
            self.assertEqual(downloaded.read_bytes(), fixture.audio[item.audio_id])

        audio_read_lines = [
            line for line in fixture.received_lines if line.startswith("PJ_AUDIO_READ ")
        ]
        self.assertEqual(len(audio_read_lines), 2)
        self.assertTrue(all("max_bytes=256" in line for line in audio_read_lines))


if __name__ == "__main__":
    unittest.main()
