from __future__ import annotations

from unittest.mock import patch
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
import json
import unittest

from pocket_journal_partner import cli
from pocket_journal_partner.device import AudioItem, DeviceClient, DeviceError, SerialDeviceClient
from pocket_journal_partner.operations import DeviceSession, validate_status_compatibility


class OperationsTests(unittest.TestCase):
    def test_session_uses_stable_transport_envelope(self) -> None:
        serial = SerialDeviceClient("/dev/null")
        serial._request = lambda command: {"device_id": "pj-test"}  # type: ignore[method-assign]
        session = DeviceSession("pj-test", serial)

        self.assertEqual(session.transport, "usb")
        self.assertEqual(
            session.envelope(session.status()),
            {"device_id": "pj-test", "transport": "usb", "result": {"device_id": "pj-test"}},
        )

    def test_explicit_api_version_mismatch_is_rejected(self) -> None:
        validate_status_compatibility({"api_version": "v1.4"})
        with self.assertRaisesRegex(DeviceError, "unsupported device API version"):
            validate_status_compatibility({"api_version": "v2"})
        with self.assertRaisesRegex(DeviceError, "invalid API version"):
            validate_status_compatibility({"api_version": "future"})

    def test_advertised_unsupported_capability_is_rejected(self) -> None:
        client = DeviceClient("http://device.local", "token")
        client.status = lambda: {"api_version": 1, "capabilities": {"recordings.list": False}}  # type: ignore[method-assign]
        session = DeviceSession("pj-test", client)

        with self.assertRaisesRegex(DeviceError, "does not support capability"):
            session.list_recordings()

    def test_usb_supports_bounded_recording_list_capability(self) -> None:
        client = SerialDeviceClient("/dev/null")
        client.status = lambda: {"api_version": 1}  # type: ignore[method-assign]
        client.list_audio = lambda: [AudioItem("rec.wav", "rec.wav")]  # type: ignore[method-assign]

        items = DeviceSession("pj-test", client).list_recordings()

        self.assertEqual(items[0].audio_id, "rec.wav")

    def test_list_recordings_runs_status_preflight(self) -> None:
        client = DeviceClient("http://device.local", "token")
        calls = []
        client.status = lambda: calls.append("status") or {"firmware_version": "v0"}  # type: ignore[method-assign]
        client.list_audio = lambda: [AudioItem("rec.wav", "rec.wav")]  # type: ignore[method-assign]

        items = DeviceSession("pj-test", client).list_recordings()

        self.assertEqual(calls, ["status"])
        self.assertEqual(items[0].audio_id, "rec.wav")

    def test_status_cli_maps_arguments_and_emits_transport_envelope(self) -> None:
        serial = SerialDeviceClient("/dev/cu.test")
        serial.status = lambda: {"device_id": "pj-test", "firmware_version": "v0"}  # type: ignore[method-assign]
        stdout = StringIO()
        with patch("pocket_journal_partner.cli.resolve_serial_port", return_value="/dev/cu.test"):
            with patch("pocket_journal_partner.cli.SerialDeviceClient", return_value=serial):
                with redirect_stdout(stdout):
                    exit_code = cli.main(["device", "status", "--serial-port", "/dev/cu.test"])

        self.assertEqual(exit_code, 0)
        self.assertEqual(json.loads(stdout.getvalue()), {
            "device_id": "usb",
            "transport": "usb",
            "result": {"device_id": "pj-test", "firmware_version": "v0"},
        })

    def test_unsupported_cli_capability_has_stable_error_exit(self) -> None:
        client = DeviceClient("http://device.local", "token")
        client.status = lambda: {"capabilities": {"recordings.list": False}}  # type: ignore[method-assign]
        session = DeviceSession("pj-test", client)
        stderr = StringIO()
        with patch("pocket_journal_partner.cli._session_from_args", return_value=session):
            with redirect_stderr(stderr):
                exit_code = cli.main(["recordings", "list", "--device", "pj-test"])

        self.assertEqual(exit_code, 1)
        self.assertEqual(
            stderr.getvalue(),
            "pj: error: device firmware does not support capability: recordings.list\n",
        )


if __name__ == "__main__":
    unittest.main()
