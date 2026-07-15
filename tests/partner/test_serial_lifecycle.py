from __future__ import annotations

from unittest.mock import patch
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
import threading
import tty
import unittest

from pocket_journal_partner.device import (
    AudioItem,
    DeviceError,
    DeviceOperationError,
    DeviceOperationTimeout,
    DeviceRequestTimeout,
    SerialDeviceClient,
    _stop_child_process,
)


class FakeSerialException(Exception):
    pass


class FakeConnection:
    def __init__(self, lines: list[bytes | BaseException] | None = None) -> None:
        self.lines = list(lines or [])
        self.port: str | None = None
        self.dtr: bool | None = None
        self.rts: bool | None = None
        self.is_open = False
        self.open_control_lines: tuple[bool | None, bool | None] | None = None
        self.closed_control_lines: tuple[bool | None, bool | None] | None = None
        self.open_count = 0
        self.close_count = 0
        self.writes: list[bytes] = []
        self.input_reset = False

    def open(self) -> None:
        self.open_control_lines = (self.dtr, self.rts)
        self.open_count += 1
        self.is_open = True

    def close(self) -> None:
        self.closed_control_lines = (self.dtr, self.rts)
        self.close_count += 1
        self.is_open = False

    def reset_input_buffer(self) -> None:
        self.input_reset = True

    def write(self, payload: bytes) -> int:
        self.writes.append(payload)
        return len(payload)

    def flush(self) -> None:
        pass

    def readline(self) -> bytes:
        if not self.lines:
            return b""
        value = self.lines.pop(0)
        if isinstance(value, BaseException):
            raise value
        return value


class FakeSerialModule:
    SerialException = FakeSerialException

    def __init__(self, connection: FakeConnection) -> None:
        self.connection = connection
        self.constructor_kwargs: dict[str, object] | None = None

    def Serial(self, **kwargs):  # noqa: N802 - mirrors pyserial
        self.constructor_kwargs = kwargs
        return self.connection


class FakeTermiosModule:
    HUPCL = 0x4000
    TCSANOW = 0

    def __init__(self) -> None:
        self.attributes = [1, 2, self.HUPCL | 0x20, 4, 5, 6, [7]]
        self.events: list[tuple[str, int, object]] = []

    def tcgetattr(self, fd: int):
        self.events.append(("get", fd, None))
        return list(self.attributes)

    def tcsetattr(self, fd: int, when: int, attributes) -> None:
        self.attributes = list(attributes)
        self.events.append(("set", fd, when))


class QueuedSerialModule:
    SerialException = FakeSerialException

    def __init__(self, connections: list[FakeConnection]) -> None:
        self.connections = list(connections)
        self.constructor_kwargs: list[dict[str, object]] = []

    def Serial(self, **kwargs):  # noqa: N802 - mirrors pyserial
        self.constructor_kwargs.append(kwargs)
        if not self.connections:
            raise AssertionError("unexpected serial connection")
        return self.connections.pop(0)


class TracedConnection(FakeConnection):
    def __init__(self, lines: list[bytes | BaseException] | None = None) -> None:
        self.control_line_events: list[tuple[str, bool | None]] = []
        super().__init__(lines)
        self.control_line_events.clear()

    def __setattr__(self, name: str, value) -> None:
        if name in {"dtr", "rts"} and "control_line_events" in self.__dict__:
            self.control_line_events.append((name, value))
        super().__setattr__(name, value)


class ModemApplyingConnection(TracedConnection):
    def __init__(self, lines: list[bytes | BaseException] | None = None) -> None:
        self.modem_update_events: list[tuple[str, bool | None]] = []
        super().__init__(lines)

    def open(self) -> None:
        super().open()
        self._update_dtr_state()
        self._update_rts_state()

    def _update_dtr_state(self) -> None:
        self.modem_update_events.append(("dtr", self.dtr))

    def _update_rts_state(self) -> None:
        self.modem_update_events.append(("rts", self.rts))


class ResponseAfterWriteConnection(FakeConnection):
    def __init__(self, write_number: int, response: bytes) -> None:
        super().__init__()
        self.write_number = write_number
        self.response = response

    def write(self, payload: bytes) -> int:
        written = super().write(payload)
        if len(self.writes) == self.write_number:
            self.lines.append(self.response)
        return written


class ResponsesAfterWriteConnection(FakeConnection):
    def __init__(self, responses: dict[int, bytes]) -> None:
        super().__init__()
        self.responses = responses

    def write(self, payload: bytes) -> int:
        written = super().write(payload)
        response = self.responses.get(len(self.writes))
        if response is not None:
            self.lines.append(response)
        return written


class AudioTransferConnection(FakeConnection):
    def __init__(self, content: bytes, *, drop_once_at: int | None = None) -> None:
        super().__init__()
        self.content = content
        self.digest = hashlib.sha256(content).hexdigest()
        self.drop_once_at = drop_once_at
        self.attempts: dict[int, int] = {}

    def write(self, payload: bytes) -> int:
        written = super().write(payload)
        fields = dict(
            token.split("=", 1) for token in payload.decode("ascii").strip().split()[1:]
        )
        offset = int(fields["offset"])
        self.attempts[offset] = self.attempts.get(offset, 0) + 1
        if offset == self.drop_once_at and self.attempts[offset] == 1:
            return written
        maximum = int(fields["max_bytes"])
        chunk = self.content[offset:offset + maximum]
        response = {
            "command": "PJ_AUDIO_READ",
            "request_id": fields["request_id"],
            "id_hex": fields["id_hex"],
            "offset": offset,
            "total_bytes": len(self.content),
            "data_hex": chunk.hex(),
            "eof": offset + len(chunk) == len(self.content),
            "source_sha256": self.digest,
        }
        self.lines.append(
            ("PJ_OK " + json.dumps(response, separators=(",", ":")) + "\n").encode("ascii")
        )
        return written


class BlockingAudioConnection(FakeConnection):
    def __init__(self) -> None:
        super().__init__()
        self.read_started = threading.Event()
        self.cancelled = threading.Event()

    def readline(self) -> bytes:
        self.read_started.set()
        self.cancelled.wait(2)
        raise FakeSerialException("cancelled")

    def cancel_read(self) -> None:
        self.cancelled.set()


class SteppingClock:
    def __init__(self, step: float = 0.1) -> None:
        self.value = 0.0
        self.step = step

    def __call__(self) -> float:
        value = self.value
        self.value += self.step
        return value


class FakeProcess:
    def __init__(
        self,
        outcomes: list[tuple[str, str] | BaseException] | None = None,
        *,
        returncode: int | None = 0,
    ) -> None:
        self.outcomes = list(outcomes or [("", "")])
        self.returncode = returncode
        self.events: list[tuple[str, float | None] | tuple[str]] = []

    def communicate(self, timeout: float | None = None) -> tuple[str, str]:
        self.events.append(("communicate", timeout))
        outcome = self.outcomes.pop(0) if self.outcomes else ("", "")
        if isinstance(outcome, BaseException):
            raise outcome
        return outcome

    def poll(self) -> int | None:
        self.events.append(("poll",))
        return self.returncode

    def terminate(self) -> None:
        self.events.append(("terminate",))

    def kill(self) -> None:
        self.events.append(("kill",))


class SerialLifecycleTests(unittest.TestCase):
    def _request(self, connection: FakeConnection, *, timeout: float = 0.05):
        serial_module = FakeSerialModule(connection)
        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch("pocket_journal_partner.device.time.sleep"):
                result = SerialDeviceClient("/dev/cu.test", timeout=timeout).status()
        return result, serial_module

    def test_success_deasserts_control_lines_before_open_and_closes(self) -> None:
        connection = FakeConnection([b'PJ_OK {"device_id":"pj-test"}\n'])

        result, serial_module = self._request(connection)

        self.assertEqual(result, {"device_id": "pj-test"})
        self.assertEqual(serial_module.constructor_kwargs, {
            "port": None,
            "baudrate": 115200,
            "timeout": 0.05,
            "write_timeout": 2.0,
            "dsrdtr": False,
            "rtscts": False,
            "exclusive": True,
        })
        self.assertEqual(connection.port, "/dev/cu.test")
        self.assertEqual(connection.open_control_lines, (False, False))
        self.assertEqual(connection.closed_control_lines, (False, False))
        self.assertEqual(connection.close_count, 1)
        self.assertFalse(connection.is_open)
        self.assertTrue(connection.input_reset)
        self.assertEqual(connection.writes, [b"PJ_STATUS\n"])

    def test_audio_download_reuses_one_connection_and_settles_once(self) -> None:
        content = bytes(index % 251 for index in range(700))
        connection = AudioTransferConnection(content)
        serial_module = FakeSerialModule(connection)
        client = SerialDeviceClient("/dev/cu.test", timeout=1)
        item = AudioItem(
            "note.wav", "note.wav", size=len(content),
            source_sha256=hashlib.sha256(content).hexdigest(),
        )

        with TemporaryDirectory() as tmp:
            with patch.dict(sys.modules, {"serial": serial_module}):
                with patch("pocket_journal_partner.device.time.sleep") as sleep:
                    path = client.download_audio(item, Path(tmp))
            self.assertEqual(path.read_bytes(), content)

        self.assertEqual(connection.open_count, 1)
        self.assertEqual(connection.close_count, 1)
        self.assertEqual(len(connection.writes), 3)
        sleep.assert_called_once()

    def test_audio_download_retries_same_request_on_one_connection(self) -> None:
        content = bytes(index % 251 for index in range(300))
        connection = AudioTransferConnection(content, drop_once_at=256)
        serial_module = FakeSerialModule(connection)
        client = SerialDeviceClient("/dev/cu.test", timeout=2)
        item = AudioItem(
            "note.wav", "note.wav", size=len(content),
            source_sha256=hashlib.sha256(content).hexdigest(),
        )

        with TemporaryDirectory() as tmp:
            with patch.dict(sys.modules, {"serial": serial_module}):
                with patch("pocket_journal_partner.device.time.sleep"):
                    with patch(
                        "pocket_journal_partner.device.time.monotonic",
                        side_effect=SteppingClock(0.2),
                    ):
                        path = client.download_audio(item, Path(tmp))
            self.assertEqual(path.read_bytes(), content)

        retried = [wire for wire in connection.writes if b"offset=256 " in wire]
        self.assertEqual(len(retried), 2)
        self.assertEqual(retried[0], retried[1])
        self.assertEqual(connection.open_count, 1)
        self.assertEqual(connection.close_count, 1)

    def test_audio_download_interrupt_removes_partial_and_releases_port(self) -> None:
        connection = FakeConnection([KeyboardInterrupt()])
        serial_module = FakeSerialModule(connection)
        client = SerialDeviceClient("/dev/cu.test", timeout=1)
        item = AudioItem("note.wav", "note.wav", size=1, source_sha256="0" * 64)

        with TemporaryDirectory() as tmp:
            with patch.dict(sys.modules, {"serial": serial_module}):
                with patch("pocket_journal_partner.device.time.sleep"):
                    with self.assertRaises(KeyboardInterrupt):
                        client.download_audio(item, Path(tmp))
            self.assertEqual(list(Path(tmp).iterdir()), [])

        self.assertEqual(connection.close_count, 1)
        self.assertFalse(connection.is_open)

    def test_audio_download_timeout_removes_partial_and_releases_port(self) -> None:
        connection = FakeConnection()
        serial_module = FakeSerialModule(connection)
        client = SerialDeviceClient("/dev/cu.test", timeout=0.1)
        item = AudioItem("note.wav", "note.wav", size=1, source_sha256="0" * 64)

        with TemporaryDirectory() as tmp:
            with patch.dict(sys.modules, {"serial": serial_module}):
                with patch("pocket_journal_partner.device.time.sleep"):
                    with patch(
                        "pocket_journal_partner.device.time.monotonic",
                        side_effect=SteppingClock(0.2),
                    ):
                        with self.assertRaises(DeviceRequestTimeout):
                            client.download_audio(item, Path(tmp))
            self.assertEqual(list(Path(tmp).iterdir()), [])

        self.assertEqual(connection.close_count, 1)
        self.assertFalse(connection.is_open)

    def test_close_cancels_active_audio_read_and_releases_port(self) -> None:
        connection = BlockingAudioConnection()
        serial_module = FakeSerialModule(connection)
        client = SerialDeviceClient("/dev/cu.test", timeout=6)
        item = AudioItem("note.wav", "note.wav", size=1, source_sha256="0" * 64)
        errors: list[BaseException] = []

        with TemporaryDirectory() as tmp:
            def download() -> None:
                try:
                    client.download_audio(item, Path(tmp))
                except BaseException as exc:
                    errors.append(exc)

            with patch.dict(sys.modules, {"serial": serial_module}):
                with patch("pocket_journal_partner.device.time.sleep"):
                    worker = threading.Thread(target=download)
                    worker.start()
                    self.assertTrue(connection.read_started.wait(1))
                    client.close()
                    worker.join(timeout=1)
            self.assertFalse(worker.is_alive())
            self.assertEqual(list(Path(tmp).iterdir()), [])

        self.assertEqual(len(errors), 1)
        self.assertIsInstance(errors[0], DeviceError)
        self.assertIn("cancelled", str(errors[0]))
        self.assertEqual(connection.close_count, 1)
        self.assertFalse(connection.is_open)

    def test_posix_connection_clears_hupcl_without_redundant_line_writes(self) -> None:
        connection = TracedConnection([b'PJ_OK {"device_id":"pj-test"}\n'])
        connection.fd = 73
        serial_module = FakeSerialModule(connection)
        termios_module = FakeTermiosModule()

        with patch.dict(sys.modules, {"serial": serial_module, "termios": termios_module}):
            with patch("pocket_journal_partner.device.time.sleep"):
                result = SerialDeviceClient("/dev/cu.test", timeout=1).status()

        self.assertEqual(result, {"device_id": "pj-test"})
        self.assertEqual(termios_module.events, [
            ("get", 73, None),
            ("set", 73, termios_module.TCSANOW),
        ])
        self.assertEqual(termios_module.attributes[2] & termios_module.HUPCL, 0)
        self.assertEqual(termios_module.attributes[2] & 0x20, 0x20)
        self.assertEqual(connection.control_line_events, [
            ("dtr", False),
            ("rts", False),
        ])
        self.assertEqual(connection.open_count, 1)
        self.assertEqual(connection.close_count, 1)
        self.assertEqual(connection.closed_control_lines, (False, False))

    def test_posix_open_suppresses_pyserial_modem_updates_without_flow_control(self) -> None:
        connection = ModemApplyingConnection([b'PJ_OK {"device_id":"pj-test"}\n'])
        serial_module = FakeSerialModule(connection)

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch("pocket_journal_partner.device.time.sleep"):
                result = SerialDeviceClient("/dev/cu.test", timeout=1).status()

        self.assertEqual(result, {"device_id": "pj-test"})
        self.assertEqual(connection.modem_update_events, [])
        self.assertNotIn("_update_dtr_state", connection.__dict__)
        self.assertNotIn("_update_rts_state", connection.__dict__)
        connection._update_dtr_state()
        connection._update_rts_state()
        self.assertEqual(connection.modem_update_events, [
            ("dtr", False),
            ("rts", False),
        ])
        self.assertEqual(serial_module.constructor_kwargs["dsrdtr"], False)
        self.assertEqual(serial_module.constructor_kwargs["rtscts"], False)
        self.assertEqual(connection.open_count, 1)
        self.assertEqual(connection.close_count, 1)

    def test_interval_reset_is_tagged_confirmed_and_releases_descriptor(self) -> None:
        connection = FakeConnection([
            b'PJ_OK {"command":"PJ_INTERVAL_RESET","request_id":"stop-1",'
            b'"silenced":true,"reset":true,"persisted":true}\n',
        ])
        serial_module = FakeSerialModule(connection)

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch("pocket_journal_partner.device.time.sleep"):
                with patch(
                    "pocket_journal_partner.device._new_request_id",
                    return_value="stop-1",
                ):
                    result = SerialDeviceClient(
                        "/dev/cu.test", timeout=1,
                    ).reset_interval()

        self.assertTrue(result["reset"])
        self.assertTrue(result["persisted"])
        self.assertEqual(
            connection.writes,
            [b"PJ_INTERVAL_RESET request_id=stop-1\n"],
        )
        self.assertEqual(connection.close_count, 1)
        self.assertFalse(connection.is_open)

    def test_timeout_releases_descriptor_and_reports_recovery(self) -> None:
        connection = FakeConnection()
        serial_module = FakeSerialModule(connection)

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch("pocket_journal_partner.device.time.sleep"):
                with self.assertRaisesRegex(DeviceError, "port was released") as raised:
                    SerialDeviceClient("/dev/cu.test", timeout=0).status()

        self.assertIn("AUX/BOOT released", str(raised.exception))
        self.assertEqual(connection.close_count, 1)
        self.assertFalse(connection.is_open)

    def test_wipe_rejects_stale_frames_on_one_lifecycle_descriptor(self) -> None:
        connection = FakeConnection([
            b'PJ_OK {"command":"PJ_WIPE_RECORDINGS","request_id":"start",'
            b'"accepted":true,"recording_wipe":{"id":7,"state":"queued",'
            b'"audio_deleted":0,"transcripts_deleted":0,"notes_deleted":0,'
            b'"code":null,"retryable":false}}\n',
            b'PJ_OK {"command":"PJ_WIPE_RECORDINGS","request_id":"poll-1",'
            b'"recording_wipe":{"id":7,"state":"succeeded","audio_deleted":91}}\n',
            b'PJ_OK {"command":"PJ_STATUS","request_id":"wrong",'
            b'"recording_wipe":{"id":7,"state":"succeeded","audio_deleted":92}}\n',
            b'PJ_OK {"command":"PJ_STATUS","request_id":"poll-1",'
            b'"recording_wipe":{"id":7,"state":"running","audio_deleted":1}}\n',
            b'PJ_OK {"command":"PJ_STATUS","request_id":"poll-2",'
            b'"recording_wipe":{"id":7,"state":"succeeded","audio_deleted":93}}\n',
            b'PJ_OK {"command":"PJ_STATUS","request_id":"poll-1",'
            b'"recording_wipe":{"id":7,"state":"succeeded","audio_deleted":3,'
            b'"transcripts_deleted":2,"notes_deleted":1,"code":null,'
            b'"retryable":false}}\n',
        ])
        serial_module = QueuedSerialModule([connection])

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch("pocket_journal_partner.device.time.sleep"):
                with patch(
                    "pocket_journal_partner.device._new_request_id",
                    side_effect=["start", "poll-1"],
                ):
                    result = SerialDeviceClient("/dev/cu.test", timeout=1).wipe_recordings()

        self.assertEqual(result["operation_id"], 7)
        self.assertEqual(result["deleted"], 3)
        self.assertEqual(connection.writes, [
            b"PJ_WIPE_RECORDINGS request_id=start\n",
            b"PJ_STATUS request_id=poll-1\n",
            b"PJ_STATUS request_id=poll-1\n",
        ])
        self.assertEqual(connection.open_count, 1)
        self.assertEqual(connection.close_count, 1)
        self.assertFalse(connection.input_reset)

    def test_wipe_timeout_reports_operation_id_and_releases_lifecycle_descriptor(self) -> None:
        connection = FakeConnection([
            b'PJ_OK {"command":"PJ_WIPE_RECORDINGS","request_id":"start",'
            b'"recording_wipe":{"id":41,"state":"running","audio_deleted":0}}\n',
        ])
        serial_module = QueuedSerialModule([connection])

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch("pocket_journal_partner.device.time.sleep"):
                with patch(
                    "pocket_journal_partner.device._new_request_id",
                    side_effect=["start", "poll"],
                ):
                    with self.assertRaises(DeviceOperationTimeout) as raised:
                        SerialDeviceClient("/dev/cu.test", timeout=0.03).wipe_recordings()

        self.assertEqual(raised.exception.operation_id, 41)
        self.assertIn("operation 41", str(raised.exception))
        self.assertEqual(connection.open_count, 1)
        self.assertEqual(connection.close_count, 1)
        self.assertFalse(connection.is_open)

    def test_wipe_delayed_start_response_uses_one_descriptor_without_retransmit(self) -> None:
        start = FakeConnection([
            b"",
            b"",
            b'PJ_OK {"command":"PJ_WIPE_RECORDINGS","request_id":"start",'
            b'"recording_wipe":{"id":51,"state":"succeeded","audio_deleted":2}}\n',
        ])
        serial_module = QueuedSerialModule([start])

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch("pocket_journal_partner.device.time.sleep"):
                with patch("pocket_journal_partner.device.time.monotonic", new=SteppingClock()):
                    with patch("pocket_journal_partner.device._new_request_id", return_value="start"):
                        result = SerialDeviceClient("/dev/cu.test", timeout=10).wipe_recordings()

        self.assertEqual(result["operation_id"], 51)
        self.assertEqual(start.writes, [b"PJ_WIPE_RECORDINGS request_id=start\n"])
        self.assertEqual(start.open_count, 1)
        self.assertEqual(start.close_count, 1)
        self.assertFalse(start.is_open)
        self.assertEqual(len(serial_module.constructor_kwargs), 1)

    def test_wipe_lost_start_ack_retransmits_same_request_on_one_descriptor(self) -> None:
        connection = ResponsesAfterWriteConnection({
            2: (
                b'PJ_OK {"command":"PJ_WIPE_RECORDINGS","request_id":"start",'
                b'"attached":true,"recording_wipe":{"id":52,"state":"running"}}\n'
            ),
            4: (
                b'PJ_OK {"command":"PJ_STATUS","request_id":"poll",'
                b'"recording_wipe":{"id":52,"state":"succeeded","audio_deleted":2, '
                b'"transcripts_deleted":1,"notes_deleted":1}}\n'
            ),
        })
        serial_module = QueuedSerialModule([connection])

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch("pocket_journal_partner.device.time.sleep"):
                with patch("pocket_journal_partner.device.time.monotonic", new=SteppingClock()):
                    with patch(
                        "pocket_journal_partner.device._new_request_id",
                        side_effect=["start", "poll"],
                    ):
                        result = SerialDeviceClient("/dev/cu.test", timeout=10).wipe_recordings()

        self.assertEqual(result["operation_id"], 52)
        self.assertEqual(connection.writes[:2], 2 * [b"PJ_WIPE_RECORDINGS request_id=start\n"])
        self.assertEqual(connection.writes[2:], 2 * [b"PJ_STATUS request_id=poll\n"])
        self.assertEqual(connection.open_count, 1)
        self.assertEqual(connection.close_count, 1)
        self.assertFalse(connection.is_open)
        self.assertEqual(len(serial_module.constructor_kwargs), 1)

    def test_wipe_reports_accepted_operation_disappearing_from_status(self) -> None:
        connection = FakeConnection([
            b'PJ_OK {"command":"PJ_WIPE_RECORDINGS","request_id":"start",'
            b'"recording_wipe":{"id":61,"state":"running"}}\n',
            b'PJ_OK {"command":"PJ_STATUS","request_id":"poll",'
            b'"recording_wipe":{"id":0,"state":"idle"},"recording_wipe_recent":[]}\n',
        ])
        serial_module = QueuedSerialModule([connection])

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch("pocket_journal_partner.device.time.sleep"):
                with patch(
                    "pocket_journal_partner.device._new_request_id",
                    side_effect=["start", "poll"],
                ):
                    with self.assertRaises(DeviceOperationError) as raised:
                        SerialDeviceClient("/dev/cu.test", timeout=1).wipe_recordings()

        self.assertEqual(raised.exception.operation_id, 61)
        self.assertEqual(raised.exception.code, "operation_state_lost")
        self.assertFalse(raised.exception.retryable)
        self.assertIn("current idle id 0", str(raised.exception))
        self.assertIn("outcome is unknown", str(raised.exception))
        self.assertEqual(connection.open_count, 1)
        self.assertEqual(connection.close_count, 1)
        self.assertEqual(connection.writes, [
            b"PJ_WIPE_RECORDINGS request_id=start\n",
            b"PJ_STATUS request_id=poll\n",
        ])

    def test_wipe_preserves_and_reports_reset_evidence_after_start(self) -> None:
        connection = FakeConnection([
            b'PJ_OK {"command":"PJ_WIPE_RECORDINGS","request_id":"start",'
            b'"recording_wipe":{"id":62,"state":"running"}}\n',
            b"ESP-ROM:esp32s3-20210327\n",
            b"rst:0x15 (USB_UART_CHIP_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)\n",
        ])
        serial_module = QueuedSerialModule([connection])

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch("pocket_journal_partner.device.time.sleep"):
                with patch("pocket_journal_partner.device.time.monotonic", new=SteppingClock()):
                    with patch(
                        "pocket_journal_partner.device._new_request_id",
                        side_effect=["start", "poll"],
                    ):
                        with self.assertRaises(DeviceOperationError) as raised:
                            SerialDeviceClient("/dev/cu.test", timeout=10).wipe_recordings()

        self.assertEqual(raised.exception.operation_id, 62)
        self.assertEqual(raised.exception.code, "device_reset")
        self.assertFalse(raised.exception.retryable)
        self.assertIn("reset", str(raised.exception))
        self.assertFalse(connection.input_reset)
        self.assertEqual(connection.open_count, 1)
        self.assertEqual(connection.close_count, 1)

    def test_wipe_start_total_timeout_bounds_writes_and_releases_one_descriptor(self) -> None:
        start = FakeConnection()
        serial_module = QueuedSerialModule([start])
        clock = SteppingClock()

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch("pocket_journal_partner.device.time.sleep"):
                with patch("pocket_journal_partner.device.time.monotonic", new=clock):
                    with patch("pocket_journal_partner.device._new_request_id", return_value="start"):
                        with self.assertRaises(DeviceRequestTimeout):
                            SerialDeviceClient("/dev/cu.test", timeout=6).wipe_recordings()

        self.assertEqual(start.writes, 3 * [b"PJ_WIPE_RECORDINGS request_id=start\n"])
        self.assertGreaterEqual(clock.value, 6)
        self.assertLess(clock.value, 6.5)
        self.assertEqual(start.open_count, 1)
        self.assertEqual(start.close_count, 1)
        self.assertFalse(start.is_open)
        self.assertEqual(len(serial_module.constructor_kwargs), 1)

    def test_windows_uses_native_exclusive_handle_without_posix_option(self) -> None:
        connection = FakeConnection([b'PJ_OK {"device_id":"pj-test"}\n'])
        serial_module = FakeSerialModule(connection)

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch("pocket_journal_partner.device.os.name", "nt"):
                with patch("pocket_journal_partner.device.time.sleep"):
                    result = SerialDeviceClient("COM7", timeout=1).status()

        self.assertEqual(result, {"device_id": "pj-test"})
        self.assertNotIn("exclusive", serial_module.constructor_kwargs)
        self.assertEqual(connection.open_control_lines, (False, False))
        self.assertEqual(connection.close_count, 1)

    def test_keyboard_interrupt_releases_descriptor(self) -> None:
        connection = FakeConnection([KeyboardInterrupt()])
        serial_module = FakeSerialModule(connection)

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch("pocket_journal_partner.device.time.sleep"):
                with self.assertRaises(KeyboardInterrupt):
                    SerialDeviceClient("/dev/cu.test", timeout=1).status()

        self.assertEqual(connection.close_count, 1)
        self.assertFalse(connection.is_open)

    def test_serial_error_releases_an_open_descriptor(self) -> None:
        connection = FakeConnection([FakeSerialException("link lost")])
        serial_module = FakeSerialModule(connection)

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch("pocket_journal_partner.device.time.sleep"):
                with self.assertRaisesRegex(DeviceError, "link lost"):
                    SerialDeviceClient("/dev/cu.test", timeout=1).status()

        self.assertEqual(connection.close_count, 1)
        self.assertFalse(connection.is_open)

    def test_recovery_recognizes_a_healthy_application_without_resetting(self) -> None:
        connection = TracedConnection([b'PJ_OK {"device_id":"pj-test","token":"secret"}\n'])
        serial_module = QueuedSerialModule([connection])

        with patch.dict(sys.modules, {"serial": serial_module}):
            result = SerialDeviceClient("/dev/cu.test", timeout=1).recover_usb()

        self.assertEqual(result["initial_state"], "application")
        self.assertEqual(result["final_state"], "application")
        self.assertFalse(result["recovery_attempted"])
        self.assertFalse(result["recovered"])
        self.assertEqual(result["status"]["device_id"], "pj-test")
        self.assertEqual(connection.writes, [b"PJ_STATUS\n"])
        self.assertEqual(connection.close_count, 1)
        self.assertEqual(connection.closed_control_lines, (False, False))

    def test_recovery_resets_rom_download_mode_and_reprobes_application(self) -> None:
        initial = TracedConnection([
            b"rst:0x15 (USB_UART_CHIP_RESET),boot:0x0 (DOWNLOAD(USB/UART0))\n",
        ])
        final = TracedConnection([b'PJ_OK {"device_id":"pj-test","firmware_version":"v1"}\n'])
        serial_module = QueuedSerialModule([initial, final])
        client = SerialDeviceClient("/dev/cu.test", timeout=2)

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch.object(
                client,
                "_watchdog_reset_usb_serial_jtag",
                return_value="watchdog_reset_completed",
            ) as watchdog_reset:
                result = client.recover_usb()

        self.assertEqual(result["initial_state"], "rom_download")
        self.assertTrue(result["recovery_attempted"])
        self.assertTrue(result["recovered"])
        self.assertEqual(result["final_state"], "application")
        self.assertEqual(result["reset_result"], "watchdog_reset_completed")
        self.assertEqual(result["recovery_steps"], ["watchdog_reset_completed"])
        watchdog_reset.assert_called_once()
        self.assertEqual(watchdog_reset.call_args.kwargs["connect_mode"], "no-reset")
        self.assertGreater(watchdog_reset.call_args.kwargs["timeout"], 0)
        self.assertTrue(all(connection.close_count == 1 for connection in (initial, final)))

    def test_failed_watchdog_recovery_uses_exact_rts_hard_reset_fallback(self) -> None:
        initial = TracedConnection([b"waiting for download\n"])
        reset = TracedConnection()
        final = TracedConnection([b'PJ_OK {"device_id":"pj-test"}\n'])
        serial_module = QueuedSerialModule([initial, reset, final])
        client = SerialDeviceClient("/dev/cu.test", timeout=2)

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch.object(
                client,
                "_watchdog_reset_usb_serial_jtag",
                return_value="watchdog_reset_failed_exit_2",
            ):
                with patch("pocket_journal_partner.device.time.sleep"):
                    result = client.recover_usb()

        self.assertTrue(result["recovered"])
        self.assertEqual(result["reset_result"], "rts_hard_reset_completed")
        self.assertEqual(
            result["recovery_steps"],
            ["watchdog_reset_failed_exit_2", "rts_hard_reset_completed"],
        )
        asserted = reset.control_line_events.index(("rts", True))
        released = reset.control_line_events.index(("rts", False), asserted + 1)
        self.assertLess(asserted, released)
        self.assertNotIn(("dtr", True), reset.control_line_events)
        self.assertEqual(reset.closed_control_lines, (False, False))
        self.assertTrue(all(connection.close_count == 1 for connection in (initial, reset, final)))

    def test_unresponsive_probe_selects_usb_jtag_bootloader_reset(self) -> None:
        serial_module = QueuedSerialModule([])
        client = SerialDeviceClient("/dev/cu.test", timeout=2)
        probe_results = [
            {"state": "unresponsive", "evidence": "no_protocol_or_rom_response"},
            {"state": "application", "evidence": "PJ_OK", "status": {"device_id": "pj-test"}},
        ]

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch.object(client, "_probe_usb_state", side_effect=probe_results):
                with patch.object(
                    client,
                    "_watchdog_reset_usb_serial_jtag",
                    return_value="watchdog_reset_completed",
                ) as watchdog_reset:
                    result = client.recover_usb()

        self.assertTrue(result["recovered"])
        self.assertEqual(watchdog_reset.call_args.kwargs["connect_mode"], "usb-reset")

    def test_probe_only_reports_rom_without_toggling_reset(self) -> None:
        connection = TracedConnection([b"waiting for download\n"])
        serial_module = QueuedSerialModule([connection])

        with patch.dict(sys.modules, {"serial": serial_module}):
            result = SerialDeviceClient("/dev/cu.test", timeout=1).recover_usb(probe_only=True)

        self.assertEqual(result["final_state"], "rom_download")
        self.assertFalse(result["recovery_attempted"])
        self.assertNotIn(("rts", True), connection.control_line_events)
        self.assertIn("release AUX/BOOT", result["action"])
        self.assertEqual(connection.close_count, 1)

    def test_probe_timeout_is_bounded_and_releases_the_descriptor(self) -> None:
        connection = TracedConnection()
        serial_module = QueuedSerialModule([connection])

        with patch.dict(sys.modules, {"serial": serial_module}):
            result = SerialDeviceClient("/dev/cu.test", timeout=0.01).recover_usb(probe_only=True)

        self.assertEqual(result["final_state"], "unresponsive")
        self.assertIn("rerun without --probe-only", result["action"])
        self.assertEqual(connection.close_count, 1)
        self.assertFalse(connection.is_open)
        self.assertEqual(connection.closed_control_lines, (False, False))

    def test_recovery_interrupt_releases_every_open_descriptor(self) -> None:
        initial = TracedConnection([b"waiting for download\n"])
        final = TracedConnection([KeyboardInterrupt()])
        serial_module = QueuedSerialModule([initial, final])
        client = SerialDeviceClient("/dev/cu.test", timeout=2)

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch.object(
                client,
                "_watchdog_reset_usb_serial_jtag",
                return_value="watchdog_reset_completed",
            ):
                with self.assertRaises(KeyboardInterrupt):
                    client.recover_usb()

        self.assertTrue(all(connection.close_count == 1 for connection in (initial, final)))
        self.assertTrue(all(not connection.is_open for connection in (initial, final)))
        self.assertTrue(all(connection.closed_control_lines == (False, False) for connection in (initial, final)))

    def test_watchdog_recovery_runs_non_flashing_esptool_command(self) -> None:
        process = FakeProcess(returncode=0)
        client = SerialDeviceClient("/dev/cu.test", baudrate=230400, timeout=2)

        with patch("pocket_journal_partner.device.subprocess.Popen", return_value=process) as popen:
            result = client._watchdog_reset_usb_serial_jtag(connect_mode="usb-reset", timeout=1.5)

        self.assertEqual(result, "watchdog_reset_completed")
        command = popen.call_args.args[0]
        self.assertEqual(command[:3], [sys.executable, "-m", "esptool"])
        self.assertIn("usb-reset", command)
        self.assertIn("watchdog-reset", command)
        self.assertIn("--no-stub", command)
        self.assertEqual(command[-1], "read-mac")
        self.assertNotIn("write-flash", command)
        self.assertEqual(process.events, [("communicate", 1.5)])

    def test_watchdog_timeout_terminates_and_waits_for_child(self) -> None:
        process = FakeProcess([
            subprocess.TimeoutExpired("esptool", 0.2),
            ("", ""),
        ], returncode=None)
        client = SerialDeviceClient("/dev/cu.test", timeout=2)

        with patch("pocket_journal_partner.device.subprocess.Popen", return_value=process):
            result = client._watchdog_reset_usb_serial_jtag(connect_mode="usb-reset", timeout=0.2)

        self.assertEqual(result, "watchdog_reset_timed_out")
        self.assertEqual(process.events, [
            ("communicate", 0.2),
            ("poll",),
            ("terminate",),
            ("communicate", 0.5),
        ])

    def test_watchdog_timeout_kills_child_that_ignores_terminate(self) -> None:
        process = FakeProcess([
            subprocess.TimeoutExpired("esptool", 0.2),
            subprocess.TimeoutExpired("esptool", 0.5),
            ("", ""),
        ], returncode=None)
        client = SerialDeviceClient("/dev/cu.test", timeout=2)

        with patch("pocket_journal_partner.device.subprocess.Popen", return_value=process):
            result = client._watchdog_reset_usb_serial_jtag(connect_mode="usb-reset", timeout=0.2)

        self.assertEqual(result, "watchdog_reset_timed_out")
        self.assertEqual(process.events[-3:], [
            ("communicate", 0.5),
            ("kill",),
            ("communicate", None),
        ])

    def test_watchdog_interrupt_terminates_child_before_propagating(self) -> None:
        process = FakeProcess([KeyboardInterrupt(), ("", "")], returncode=None)
        client = SerialDeviceClient("/dev/cu.test", timeout=2)

        with patch("pocket_journal_partner.device.subprocess.Popen", return_value=process):
            with self.assertRaises(KeyboardInterrupt):
                client._watchdog_reset_usb_serial_jtag(connect_mode="usb-reset", timeout=1)

        self.assertIn(("terminate",), process.events)
        self.assertEqual(process.events[-1], ("communicate", 0.5))

    def test_child_cleanup_reaps_a_real_process(self) -> None:
        process = subprocess.Popen([
            sys.executable,
            "-c",
            "import time; time.sleep(60)",
        ])

        _stop_child_process(process)

        self.assertIsNotNone(process.returncode)
        self.assertIsNotNone(process.poll())

    @unittest.skipIf(os.name == "nt", "pseudo-terminals are POSIX-only")
    def test_application_probe_over_pty_releases_the_descriptor(self) -> None:
        import termios

        master_fd, slave_fd = os.openpty()
        slave_name = os.ttyname(slave_fd)
        tty.setraw(slave_fd)
        attributes = termios.tcgetattr(slave_fd)
        attributes[2] |= termios.HUPCL
        termios.tcsetattr(slave_fd, termios.TCSANOW, attributes)
        peer_result: list[bytes] = []

        def peer() -> None:
            request = os.read(master_fd, 128)
            peer_result.append(request)
            os.write(master_fd, b'PJ_OK {"device_id":"pj-pty"}\n')

        thread = threading.Thread(target=peer, daemon=True)
        thread.start()
        try:
            # PTYs do not implement modem-line ioctls. The mock tests above cover
            # DTR/RTS; this exercises the real pyserial read/write/close lifecycle.
            with patch("pocket_journal_partner.device._idle_serial_control_lines"):
                result = SerialDeviceClient(slave_name, timeout=1).recover_usb(probe_only=True)
            thread.join(timeout=1)
            self.assertFalse(thread.is_alive())
            self.assertEqual(result["final_state"], "application")
            self.assertEqual(result["status"]["device_id"], "pj-pty")
            self.assertEqual(peer_result, [b"PJ_STATUS\n"])
            self.assertEqual(termios.tcgetattr(slave_fd)[2] & termios.HUPCL, 0)

            import serial  # type: ignore

            reopened = serial.Serial(slave_name, timeout=0, exclusive=True)
            reopened.close()
        finally:
            os.close(slave_fd)
            os.close(master_fd)


if __name__ == "__main__":
    unittest.main()
