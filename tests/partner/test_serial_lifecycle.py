from __future__ import annotations

from unittest.mock import patch
import sys
import unittest

from pocket_journal_partner.device import DeviceError, SerialDeviceClient


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
        self.close_count = 0
        self.writes: list[bytes] = []
        self.input_reset = False

    def open(self) -> None:
        self.open_control_lines = (self.dtr, self.rts)
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
            "timeout": 0.2,
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


if __name__ == "__main__":
    unittest.main()
