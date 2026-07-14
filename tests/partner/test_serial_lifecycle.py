from __future__ import annotations

from unittest.mock import patch
import os
import sys
import threading
import tty
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
        reset = TracedConnection()
        final = TracedConnection([b'PJ_OK {"device_id":"pj-test","firmware_version":"v1"}\n'])
        serial_module = QueuedSerialModule([initial, reset, final])

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch("pocket_journal_partner.device.time.sleep"):
                result = SerialDeviceClient("/dev/cu.test", timeout=2).recover_usb()

        self.assertEqual(result["initial_state"], "rom_download")
        self.assertTrue(result["recovery_attempted"])
        self.assertTrue(result["recovered"])
        self.assertEqual(result["final_state"], "application")
        self.assertEqual(result["reset_result"], "rts_pulse_completed")
        self.assertIn(("rts", True), reset.control_line_events)
        self.assertEqual(reset.closed_control_lines, (False, False))
        self.assertTrue(all(connection.close_count == 1 for connection in (initial, reset, final)))

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
        reset = TracedConnection()
        final = TracedConnection([KeyboardInterrupt()])
        serial_module = QueuedSerialModule([initial, reset, final])

        with patch.dict(sys.modules, {"serial": serial_module}):
            with patch("pocket_journal_partner.device.time.sleep"):
                with self.assertRaises(KeyboardInterrupt):
                    SerialDeviceClient("/dev/cu.test", timeout=2).recover_usb()

        self.assertTrue(all(connection.close_count == 1 for connection in (initial, reset, final)))
        self.assertTrue(all(not connection.is_open for connection in (initial, reset, final)))
        self.assertTrue(all(connection.closed_control_lines == (False, False) for connection in (initial, reset, final)))

    @unittest.skipIf(os.name == "nt", "pseudo-terminals are POSIX-only")
    def test_application_probe_over_pty_releases_the_descriptor(self) -> None:
        master_fd, slave_fd = os.openpty()
        slave_name = os.ttyname(slave_fd)
        tty.setraw(slave_fd)
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

            import serial  # type: ignore

            reopened = serial.Serial(slave_name, timeout=0, exclusive=True)
            reopened.close()
        finally:
            os.close(slave_fd)
            os.close(master_fd)


if __name__ == "__main__":
    unittest.main()
