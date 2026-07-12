from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch
import hashlib
import json
import unittest

from pocket_journal_partner import cli
from pocket_journal_partner.device import DeviceClient, DeviceError
from pocket_journal_partner.operations import DeviceSession
from pocket_journal_partner.ota import (
    FirmwareImage,
    inspect_firmware_image,
    ota_preflight,
    stream_firmware_image,
    wait_for_ota_result,
)


class FakeResponse:
    status = 200

    def read(self) -> bytes:
        return b'{"state":"pending_reboot"}'


class FakeConnection:
    instances: list["FakeConnection"] = []

    def __init__(self, host, port, timeout) -> None:
        self.init = (host, port, timeout)
        self.headers: dict[str, str] = {}
        self.sent: list[bytes] = []
        self.request: tuple[str, str] | None = None
        self.closed = False
        self.instances.append(self)

    def putrequest(self, method: str, path: str) -> None:
        self.request = (method, path)

    def putheader(self, name: str, value: str) -> None:
        self.headers[name] = value

    def endheaders(self) -> None:
        pass

    def send(self, data: bytes) -> None:
        self.sent.append(data)

    def getresponse(self) -> FakeResponse:
        return FakeResponse()

    def close(self) -> None:
        self.closed = True


class OtaTests(unittest.TestCase):
    def setUp(self) -> None:
        FakeConnection.instances.clear()

    def test_inspect_image_hashes_incrementally_and_rejects_invalid_files(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            payload = b"firmware" * 20_000
            image_path = root / "app.bin"
            image_path.write_bytes(payload)
            image = inspect_firmware_image(image_path)
            self.assertEqual(image.size, len(payload))
            self.assertEqual(image.sha256, hashlib.sha256(payload).hexdigest())
            (root / "empty.bin").write_bytes(b"")
            with self.assertRaisesRegex(DeviceError, "empty"):
                inspect_firmware_image(root / "empty.bin")

    def test_stream_upload_is_bounded_authenticated_and_reports_progress(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "app.bin"
            payload = b"x" * 150_000
            path.write_bytes(payload)
            image = inspect_firmware_image(path)
            progress: list[tuple[int, int]] = []
            client = DeviceClient("http://device.local:8080/prefix", "secret", timeout=7)
            with patch("pocket_journal_partner.ota.http.client.HTTPConnection", FakeConnection):
                result = stream_firmware_image(client, image, progress=lambda sent, total: progress.append((sent, total)))

        connection = FakeConnection.instances[0]
        self.assertEqual(connection.init, ("device.local", 8080, 7))
        self.assertEqual(connection.request, ("POST", "/prefix/v1/ota"))
        self.assertEqual(connection.headers["Authorization"], "Bearer secret")
        self.assertEqual(connection.headers["X-PJ-Image-SHA256"], image.sha256)
        self.assertEqual(connection.headers["X-PJ-Activate"], "true")
        self.assertTrue(all(len(chunk) <= 64 * 1024 for chunk in connection.sent))
        self.assertEqual(b"".join(connection.sent), payload)
        self.assertEqual(progress[-1], (len(payload), len(payload)))
        self.assertEqual(result["state"], "pending_reboot")
        self.assertTrue(connection.closed)

    def test_interrupted_upload_does_not_read_response(self) -> None:
        class Interrupted(FakeConnection):
            def send(self, data: bytes) -> None:
                super().send(data)
                raise OSError("link down")

            def getresponse(self):
                raise AssertionError("response must not be read after interruption")

        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "app.bin"
            path.write_bytes(b"x" * 100_000)
            client = DeviceClient("http://device.local", "not-printed")
            with patch("pocket_journal_partner.ota.http.client.HTTPConnection", Interrupted):
                with self.assertRaisesRegex(DeviceError, "after 0 of 100000 bytes") as raised:
                    stream_firmware_image(client, inspect_firmware_image(path))
        self.assertNotIn("not-printed", str(raised.exception))

    def test_changed_image_is_rejected_before_response(self) -> None:
        class NoResponse(FakeConnection):
            def getresponse(self):
                raise AssertionError("response must not be accepted for a changed image")

        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "app.bin"
            path.write_bytes(b"first")
            image = inspect_firmware_image(path)
            path.write_bytes(b"other")
            with patch("pocket_journal_partner.ota.http.client.HTTPConnection", NoResponse):
                with self.assertRaisesRegex(DeviceError, "changed during upload"):
                    stream_firmware_image(DeviceClient("http://device.local", "token"), image)

    def test_preflight_rejection_is_actionable(self) -> None:
        client = DeviceClient("http://device.local", "token")
        client._request = lambda *args: {"accepted": False, "reason": "project mismatch"}  # type: ignore[method-assign]
        with self.assertRaisesRegex(DeviceError, "project mismatch"):
            ota_preflight(client, FirmwareImage(Path("app.bin"), 3, "a" * 64))

    def test_wait_reconnects_using_exact_discovered_device_id(self) -> None:
        old = DeviceClient("http://old.local", "token")
        calls: list[str] = []

        def status(client: DeviceClient):
            calls.append(client.base_url)
            if client.base_url == "http://old.local":
                raise DeviceError("offline")
            return {"state": "confirmed", "version": "v2"}

        discovered = [{"device_id": "other", "base_url": "http://wrong.local"},
                      {"device_id": "pj-test", "base_url": "http://new.local"}]
        with patch("pocket_journal_partner.ota.ota_status", status):
            client, outcome = wait_for_ota_result(
                old, "pj-test", timeout=2, interval=0, discover=lambda: discovered, sleep=lambda _: None
            )
        self.assertEqual(client.base_url, "http://new.local")
        self.assertEqual(outcome["state"], "confirmed")
        self.assertEqual(calls, ["http://old.local", "http://new.local"])

    def test_cli_requires_yes_noninteractively_before_upload(self) -> None:
        client = DeviceClient("http://device.local", "token")
        client.status = lambda: {"capabilities": {"ota.write": True}}  # type: ignore[method-assign]
        session = DeviceSession("pj-test", client)
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "app.bin"
            path.write_bytes(b"image")
            with patch("pocket_journal_partner.cli._lan_session_from_args", return_value=session), \
                 patch("pocket_journal_partner.cli.ota_preflight", return_value={"accepted": True}), \
                 patch("pocket_journal_partner.cli.sys.stdin.isatty", return_value=False), \
                 patch("pocket_journal_partner.cli.stream_firmware_image") as upload:
                stderr = StringIO()
                with redirect_stderr(stderr):
                    result = cli.main(["firmware", "update", "--device", "pj-test", "--file", str(path)])
        self.assertEqual(result, 1)
        upload.assert_not_called()
        self.assertIn("without --yes", stderr.getvalue())

    def test_cli_update_keeps_progress_on_stderr_and_json_on_stdout(self) -> None:
        client = DeviceClient("http://device.local", "token")
        client.status = lambda: {"capabilities": {"ota.write": True}}  # type: ignore[method-assign]
        session = DeviceSession("pj-test", client)
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "app.bin"
            path.write_bytes(b"image")

            def upload(_client, image, progress):
                progress(image.size, image.size)
                return {"state": "pending_reboot"}

            stdout, stderr = StringIO(), StringIO()
            with patch("pocket_journal_partner.cli._lan_session_from_args", return_value=session), \
                 patch("pocket_journal_partner.cli.ota_preflight", return_value={"accepted": True}), \
                 patch("pocket_journal_partner.cli.stream_firmware_image", side_effect=upload), \
                 patch("pocket_journal_partner.cli.wait_for_ota_result", return_value=(client, {"state": "confirmed"})), \
                 redirect_stdout(stdout), redirect_stderr(stderr):
                result = cli.main(["firmware", "update", "--device", "pj-test", "--file", str(path), "--yes"])
        self.assertEqual(result, 0)
        self.assertEqual(json.loads(stdout.getvalue())["result"]["outcome"]["state"], "confirmed")
        self.assertIn("100%", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
