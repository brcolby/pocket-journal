from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
from contextlib import redirect_stderr
from io import StringIO
from unittest.mock import patch
import unittest

from pocket_journal_partner import cli
from pocket_journal_partner.config import DeviceProfile, PartnerConfig
from pocket_journal_partner.device import DeviceClient, DeviceError, SerialDeviceClient, resolve_serial_port


class ConfigTests(unittest.TestCase):
    def test_round_trip(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.json"
            config = PartnerConfig(devices={
                "pj-test": DeviceProfile(
                    device_id="pj-test",
                    base_url="http://127.0.0.1",
                    token="token",
                    ble_name="PJ-TEST",
                )
            })
            config.save(path)
            loaded = PartnerConfig.load(path)
            self.assertEqual(loaded.devices["pj-test"].token, "token")

    def test_time_endpoint_payload(self) -> None:
        calls = []
        client = DeviceClient("http://127.0.0.1", "token")
        client._request = lambda method, path, body=None: calls.append((method, path, body)) or {"updated": True}  # type: ignore[method-assign]

        self.assertEqual(client.get_time(), {"updated": True})
        self.assertEqual(client.put_time(14, 5, 6, 19), {"updated": True})

        self.assertEqual(calls[0], ("GET", "/v1/time", None))
        self.assertEqual(calls[1], ("PUT", "/v1/time", {"hour": 14, "minute": 5, "month": 6, "day": 19}))

    def test_http_client_sends_bearer_authorization(self) -> None:
        captured = []

        class Response:
            headers = {"Content-Type": "application/json"}

            def __enter__(self):
                return self

            def __exit__(self, exc_type, exc, tb) -> None:
                _ = (exc_type, exc, tb)

            def read(self) -> bytes:
                return b"{}"

        def fake_urlopen(req, timeout):
            captured.append((req.get_header("Authorization"), timeout))
            return Response()

        client = DeviceClient("http://127.0.0.1", "pairing-token", timeout=3.0)
        with patch("pocket_journal_partner.device.request.urlopen", fake_urlopen):
            client.status()

        self.assertEqual(captured, [("Bearer pairing-token", 3.0)])

    def test_audio_payloads_and_wipe_endpoint(self) -> None:
        calls = []
        client = DeviceClient("http://127.0.0.1", "token")

        def fake_request(method, path, body=None):
            calls.append((method, path, body))
            if method == "GET":
                return {"audio": [{
                    "audio_id": "rec.wav",
                    "filename": "rec.wav",
                    "label": "REC",
                    "size": 88,
                    "data_bytes": 44,
                    "created_at": "2026-07-11T09:34:00",
                    "duration_ms": 1,
                    "synced": True,
                    "transcript_uploaded": True,
                    "transcript_path": "/sdcard/pj/transcripts/rec.wav.json",
                }]}
            return {"deleted": 1}

        client._request = fake_request  # type: ignore[method-assign]

        audio = client.list_audio()
        self.assertEqual(audio[0].data_bytes, 44)
        self.assertTrue(audio[0].synced)
        self.assertEqual(audio[0].duration_ms, 1)
        self.assertEqual(client.wipe_recordings(), {"deleted": 1})

        self.assertEqual(calls[0], ("GET", "/v1/audio", None))
        self.assertEqual(calls[1], ("DELETE", "/v1/audio", None))

    def test_serial_wifi_provisioning_command(self) -> None:
        calls = []
        client = SerialDeviceClient("/dev/null")
        client._request = lambda command: calls.append(command) or {"device_id": "pj-test"}  # type: ignore[method-assign]

        self.assertEqual(client.provision_wifi("Lab WiFi", "p@ ss", "token_1"), {"device_id": "pj-test"})

        self.assertEqual(calls, [
            "PJ_WIFI_HEX 4c61622057694669 7040207373 746f6b656e5f31",
        ])

    def test_serial_audio_tone_command(self) -> None:
        calls = []
        client = SerialDeviceClient("/dev/null")
        client._request = lambda command: calls.append(command) or {"tone_ms": 1500}  # type: ignore[method-assign]

        self.assertEqual(client.audio_tone(), {"tone_ms": 1500})
        self.assertEqual(client.audio_tone(1), {"tone_ms": 1500})
        self.assertEqual(client.audio_tone(dout_gpio=45), {"tone_ms": 1500})
        self.assertEqual(client.audio_tone(0, dout_gpio=45), {"tone_ms": 1500})
        self.assertEqual(client.audio_tone(audio_power_level=0, codec_gpio44=0x58, codec_gp45=0xFF), {"tone_ms": 1500})

        self.assertEqual(calls, [
            "PJ_AUDIO_TONE",
            "PJ_AUDIO_TONE pa=1",
            "PJ_AUDIO_TONE dout=45",
            "PJ_AUDIO_TONE pa=0 dout=45",
            "PJ_AUDIO_TONE pwr=0 gpio44=0x58 gp45=0xff",
        ])

    def test_serial_mic_check_command(self) -> None:
        calls = []
        client = SerialDeviceClient("/dev/null")
        client._request = lambda command: calls.append(command) or {"peak": 512, "silent": False}  # type: ignore[method-assign]

        self.assertEqual(client.mic_check(), {"peak": 512, "silent": False})
        self.assertEqual(client.mic_check(2000), {"peak": 512, "silent": False})
        self.assertEqual(client.mic_check(1000, 30), {"peak": 512, "silent": False})

        self.assertEqual(calls, [
            "PJ_MIC_CHECK",
            "PJ_MIC_CHECK ms=2000",
            "PJ_MIC_CHECK ms=1000 gain_db=30",
        ])

    def test_serial_port_resolution_prefers_default(self) -> None:
        with patch("pocket_journal_partner.device.discover_serial_ports", return_value=[
            "/dev/cu.usbmodem2101",
            "/dev/cu.usbmodem1101",
        ]):
            self.assertEqual(resolve_serial_port(), "/dev/cu.usbmodem1101")

    def test_serial_port_resolution_uses_single_candidate(self) -> None:
        with patch("pocket_journal_partner.device.discover_serial_ports", return_value=["/dev/cu.usbmodem2101"]):
            self.assertEqual(resolve_serial_port(), "/dev/cu.usbmodem2101")

    def test_control_client_autodetects_usb_without_device(self) -> None:
        parser = cli.build_parser()
        args = parser.parse_args(["device", "sync-time"])
        with patch("pocket_journal_partner.cli.resolve_serial_port", return_value="/dev/cu.usbmodem1101"):
            device_id, client = cli._control_client_from_args(args)

        self.assertEqual(device_id, "usb")
        self.assertIsInstance(client, SerialDeviceClient)
        self.assertEqual(client.port, "/dev/cu.usbmodem1101")

    def test_cli_device_error_is_user_facing(self) -> None:
        original = cli.cmd_discover

        def fail_discover(args):
            _ = args
            raise DeviceError("USB command timed out")

        cli.cmd_discover = fail_discover
        try:
            stderr = StringIO()
            with redirect_stderr(stderr):
                self.assertEqual(cli.main(["discover"]), 1)
            self.assertEqual(stderr.getvalue(), "pj: error: USB command timed out\n")
        finally:
            cli.cmd_discover = original


if __name__ == "__main__":
    unittest.main()
