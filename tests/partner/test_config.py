from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
from contextlib import redirect_stderr
from contextlib import redirect_stdout
from io import BytesIO, StringIO
import json
import stat
from urllib import error
from unittest.mock import patch
import unittest

from pocket_journal_partner import cli
from pocket_journal_partner.config import DeviceProfile, PartnerConfig
from pocket_journal_partner.device import (
    AudioItem,
    DeviceClient,
    DeviceError,
    DeviceHTTPError,
    DeviceOperationError,
    SerialDeviceClient,
    resolve_serial_port,
)


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
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)

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

    def test_http_client_serializes_compact_utf8_json(self) -> None:
        captured: list[bytes] = []

        class Response:
            headers = {"Content-Type": "application/json"}

            def __enter__(self):
                return self

            def __exit__(self, exc_type, exc, tb) -> None:
                _ = (exc_type, exc, tb)

            def read(self) -> bytes:
                return b"{}"

        def fake_urlopen(req, timeout):
            _ = timeout
            captured.append(req.data)
            return Response()

        client = DeviceClient("http://127.0.0.1", "token")
        with patch("pocket_journal_partner.device.request.urlopen", fake_urlopen):
            client.upload_transcript("note.wav", {"text": "caf\u00e9"})

        self.assertEqual(captured, ['{"text":"caf\u00e9"}'.encode("utf-8")])
        self.assertNotIn(b"\\u00e9", captured[0])

    def test_http_client_rejects_unsafe_connection_configuration(self) -> None:
        for base_url, token in (
            ("device.local", "token"),
            ("ftp://device.local", "token"),
            ("http://user:password@device.local", "token"),
            ("http://device.local", ""),
        ):
            with self.subTest(base_url=base_url, token=token), self.assertRaises(DeviceError):
                DeviceClient(base_url, token)

    def test_http_errors_are_actionable_and_never_include_token(self) -> None:
        client = DeviceClient("http://device.local", "super-secret-token")
        for code, expected in (
            (401, "authentication failed"),
            (404, "capability is not supported"),
            (409, "device is busy"),
            (500, "HTTP 500"),
        ):
            with self.subTest(code=code):
                failure = error.HTTPError(client._url("/v1/status"), code, "error", {}, None)
                with patch("pocket_journal_partner.device.request.urlopen", side_effect=failure):
                    with self.assertRaises(DeviceError) as raised:
                        client.status()
                failure.close()
                self.assertIn(expected, str(raised.exception))
                self.assertNotIn("super-secret-token", str(raised.exception))

    def test_http_errors_expose_retry_classification(self) -> None:
        client = DeviceClient("http://device.local", "token")
        for code, retryable in ((401, False), (404, False), (409, True), (429, True), (500, True)):
            with self.subTest(code=code):
                failure = error.HTTPError(client._url("/v1/status"), code, "error", {}, None)
                with patch("pocket_journal_partner.device.request.urlopen", side_effect=failure):
                    with self.assertRaises(DeviceHTTPError) as raised:
                        client.status()
                failure.close()
                self.assertEqual(raised.exception.status_code, code)
                self.assertEqual(raised.exception.retryable, retryable)

    def test_http_errors_preserve_only_validated_device_details(self) -> None:
        client = DeviceClient("http://device.local", "token")
        body = BytesIO(b'{"error":"recording wipe incomplete","code":"wipe_incomplete","retryable":true}')
        failure = error.HTTPError(client._url("/v1/audio"), 500, "error", {}, body)
        with patch("pocket_journal_partner.device.request.urlopen", side_effect=failure):
            with self.assertRaises(DeviceHTTPError) as raised:
                client.wipe_recordings()
        self.assertEqual(raised.exception.code, "wipe_incomplete")
        self.assertEqual(raised.exception.device_error, "recording wipe incomplete")
        self.assertIn("recording wipe incomplete", str(raised.exception))
        failure.close()

        sensitive = BytesIO(b'{"error":"password is super-secret-token","code":"storage_busy"}')
        failure = error.HTTPError(client._url("/v1/audio"), 409, "error", {}, sensitive)
        with patch("pocket_journal_partner.device.request.urlopen", side_effect=failure):
            with self.assertRaises(DeviceHTTPError) as raised:
                client.wipe_recordings()
        self.assertIsNone(raised.exception.device_error)
        self.assertNotIn("super-secret-token", str(raised.exception))
        failure.close()

    def test_provision_output_does_not_expose_generated_token_or_password(self) -> None:
        with TemporaryDirectory() as tmp:
            stdout = StringIO()
            with redirect_stdout(stdout):
                exit_code = cli.main([
                    "provision",
                    "--ssid", "Lab",
                    "--password", "very-secret-password",
                    "--ble",
                    "--mock",
                    "--data-dir", tmp,
                ])
            output = stdout.getvalue()
            stored = json.loads((Path(tmp) / "config.json").read_text(encoding="utf-8"))

        self.assertEqual(exit_code, 0)
        self.assertTrue(stored["devices"])
        token = next(iter(stored["devices"].values()))["token"]
        self.assertNotIn(token, output)
        self.assertNotIn("very-secret-password", output)
        self.assertNotIn("token", json.loads(output))

    def test_provision_defaults_to_auto_detected_usb_serial(self) -> None:
        ssid = "Example Guest"
        password = "sample.pass!"
        with TemporaryDirectory() as tmp:
            with patch("pocket_journal_partner.cli.resolve_serial_port", return_value="/dev/cu.usbmodem-test") as resolve:
                with patch("pocket_journal_partner.cli.SerialDeviceClient") as serial_client:
                    serial_client.return_value.provision_wifi.return_value = {"device_id": "pj-usb-test"}
                    with redirect_stdout(StringIO()):
                        exit_code = cli.main([
                            "provision",
                            "--ssid", ssid,
                            "--password", password,
                            "--data-dir", tmp,
                        ])

        self.assertEqual(exit_code, 0)
        resolve.assert_called_once_with(None)
        serial_client.assert_called_once_with("/dev/cu.usbmodem-test", baudrate=115200, timeout=6.0)
        provision_args = serial_client.return_value.provision_wifi.call_args.args
        self.assertEqual(provision_args[:2], (ssid, password))
        self.assertTrue(provision_args[2])
        serialized = (
            f"PJ_WIFI_HEX {ssid.encode('utf-8').hex()} {password.encode('utf-8').hex()} "
            f"{provision_args[2].encode('utf-8').hex()}\n"
        )
        self.assertLess(len(serialized.encode("ascii")), 128)

    def test_ble_specific_options_require_explicit_ble_transport(self) -> None:
        stderr = StringIO()
        with redirect_stderr(stderr):
            exit_code = cli.main([
                "provision",
                "--ssid", "Lab",
                "--password", "secret",
                "--ble-name", "PJ-TEST",
            ])

        self.assertEqual(exit_code, 1)
        self.assertEqual(stderr.getvalue(), "pj: error: --ble-name requires --ble\n")

    def test_audio_paths_escape_ids_and_contain_device_filenames(self) -> None:
        calls = []
        client = DeviceClient("http://device.local", "token")
        client._request = lambda method, path, body=None: calls.append((method, path, body)) or b"RIFF"  # type: ignore[method-assign]
        with TemporaryDirectory() as tmp:
            target = Path(tmp)
            result = client.download_audio(
                AudioItem("folder/rec.wav", "../rec.wav"),
                target,
            )
        self.assertEqual(result.name, "rec.wav")
        self.assertEqual(calls[0][1], "/v1/audio/folder%2Frec.wav")

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
                    "source_sha256": "a" * 64,
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
        self.assertEqual(audio[0].source_sha256, "a" * 64)
        self.assertTrue(audio[0].synced)
        self.assertEqual(audio[0].duration_ms, 1)
        self.assertEqual(client.wipe_recordings(), {"deleted": 1})

        self.assertEqual(calls[0], ("GET", "/v1/audio", None))
        self.assertEqual(calls[1], ("DELETE", "/v1/audio", None))

    def test_http_wipe_polls_matching_operation_to_completion(self) -> None:
        calls = []
        responses = iter([
            {
                "accepted": True,
                "recording_wipe": {
                    "id": 17,
                    "state": "queued",
                    "audio_deleted": 0,
                    "transcripts_deleted": 0,
                    "notes_deleted": 0,
                    "code": None,
                    "retryable": False,
                },
            },
            {
                "recording_wipe": {
                    "id": 99,
                    "state": "running",
                    "audio_deleted": 99,
                    "transcripts_deleted": 0,
                    "notes_deleted": 0,
                    "code": None,
                    "retryable": False,
                },
                "recording_wipe_recent": [{
                    "id": 17,
                    "state": "succeeded",
                    "audio_deleted": 3,
                    "transcripts_deleted": 2,
                    "notes_deleted": 1,
                    "code": None,
                    "retryable": False,
                }],
            },
        ])
        client = DeviceClient("http://device.local", "token", timeout=1)

        def fake_request(method, path, body=None, **kwargs):
            calls.append((method, path, body, kwargs))
            return next(responses)

        client._request = fake_request  # type: ignore[method-assign]
        with patch("pocket_journal_partner.device.time.sleep"):
            result = client.wipe_recordings()

        self.assertEqual(result, {
            "operation_id": 17,
            "state": "succeeded",
            "deleted": 3,
            "audio_deleted": 3,
            "transcripts_deleted": 2,
            "notes_deleted": 1,
        })
        self.assertEqual([call[:2] for call in calls], [
            ("DELETE", "/v1/audio"),
            ("GET", "/v1/status"),
        ])

    def test_http_wipe_failure_exposes_operation_code(self) -> None:
        client = DeviceClient("http://device.local", "token")
        client._request = lambda method, path, body=None: {  # type: ignore[method-assign]
            "recording_wipe": {
                "id": 18,
                "state": "failed",
                "audio_deleted": 1,
                "transcripts_deleted": 0,
                "notes_deleted": 0,
                "code": "wipe_incomplete",
                "retryable": True,
            }
        }
        with self.assertRaises(DeviceOperationError) as raised:
            client.wipe_recordings()
        self.assertEqual(raised.exception.operation_id, 18)
        self.assertEqual(raised.exception.code, "wipe_incomplete")
        self.assertTrue(raised.exception.retryable)

    def test_audio_source_digest_is_optional_for_older_or_failed_device_hashing(self) -> None:
        client = DeviceClient("http://127.0.0.1", "token")
        client._request = lambda method, path: {  # type: ignore[method-assign]
            "audio": [
                {"audio_id": "old.wav", "filename": "old.wav"},
                {
                    "audio_id": "invalid.wav",
                    "filename": "invalid.wav",
                    "source_sha256": "NOT-A-DIGEST",
                },
            ]
        }

        audio = client.list_audio()
        self.assertIsNone(audio[0].source_sha256)
        self.assertIsNone(audio[1].source_sha256)

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

    def test_explicit_lan_connection_does_not_require_stored_profile(self) -> None:
        parser = cli.build_parser()
        args = parser.parse_args([
            "device", "status",
            "--device", "pj-explicit",
            "--base-url", "http://device.local",
            "--token", "override-token",
        ])

        device_id, client = cli._control_client_from_args(args)

        self.assertEqual(device_id, "pj-explicit")
        self.assertIsInstance(client, DeviceClient)
        self.assertEqual(client.base_url, "http://device.local")
        self.assertEqual(client.token, "override-token")

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
