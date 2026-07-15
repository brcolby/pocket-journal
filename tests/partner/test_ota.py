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
    FirmwareBundle,
    FirmwareImage,
    FirmwareManifest,
    inspect_firmware_bundle,
    inspect_firmware_image,
    ota_preflight,
    stream_firmware_image,
    wait_for_ota_result,
)


UPLOAD_ID = "0123456789abcdef0123456789abcdef"
P256_P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
P256_A = P256_P - 3
P256_N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
P256_G = (
    0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
    0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5,
)


def p256_add(left, right):
    if left is None:
        return right
    if right is None:
        return left
    x1, y1 = left
    x2, y2 = right
    if x1 == x2 and (y1 + y2) % P256_P == 0:
        return None
    if left == right:
        slope = (3 * x1 * x1 + P256_A) * pow(2 * y1, -1, P256_P)
    else:
        slope = (y2 - y1) * pow(x2 - x1, -1, P256_P)
    slope %= P256_P
    x3 = (slope * slope - x1 - x2) % P256_P
    return x3, (slope * (x1 - x3) - y1) % P256_P


def p256_multiply(scalar, point):
    result = None
    addend = point
    while scalar:
        if scalar & 1:
            result = p256_add(result, addend)
        addend = p256_add(addend, addend)
        scalar >>= 1
    return result


def decode_der_ecdsa(signature: bytes) -> tuple[int, int]:
    if len(signature) < 8 or signature[0] != 0x30 or signature[1] != len(signature) - 2:
        raise ValueError("invalid signature sequence")
    cursor = 2
    values = []
    for _ in range(2):
        if cursor + 2 > len(signature) or signature[cursor] != 0x02:
            raise ValueError("invalid signature integer")
        size = signature[cursor + 1]
        value = signature[cursor + 2 : cursor + 2 + size]
        if len(value) != size:
            raise ValueError("truncated signature integer")
        values.append(int.from_bytes(value, "big"))
        cursor += 2 + size
    if cursor != len(signature):
        raise ValueError("trailing signature data")
    return values[0], values[1]


def verify_p256_sha256(message: bytes, signature: bytes, public_key) -> bool:
    r, s = decode_der_ecdsa(signature)
    if not 1 <= r < P256_N or not 1 <= s < P256_N:
        return False
    inverse = pow(s, -1, P256_N)
    digest = int.from_bytes(hashlib.sha256(message).digest(), "big")
    point = p256_add(
        p256_multiply((digest * inverse) % P256_N, P256_G),
        p256_multiply((r * inverse) % P256_N, public_key),
    )
    return point is not None and point[0] % P256_N == r


def esp_image(payload: bytes = b"") -> bytes:
    image = bytearray(32 + 256)
    image[0] = 0xE9
    image[1] = 1
    image[12:14] = (0x0009).to_bytes(2, "little")
    image[32:36] = (0xABCD5432).to_bytes(4, "little")
    image[36:40] = (1).to_bytes(4, "little")
    image[48:80] = b"2.0.0".ljust(32, b"\0")
    image[80:112] = b"pocket_journal".ljust(32, b"\0")
    return bytes(image) + payload


def write_bundle(path: Path, payload: bytes = b"image") -> FirmwareBundle:
    payload = esp_image(payload)
    path.write_bytes(payload)
    digest = hashlib.sha256(payload).hexdigest()
    path.with_suffix(path.suffix + ".manifest.json").write_text(json.dumps({
        "size": len(payload),
        "sha256": digest,
        "project": "pocket_journal",
        "board": "waveshare-esp32-s3-touch-epaper-1.54-v2",
        "target": "esp32s3",
        "version": "2.0.0",
        "secure_version": 1,
    }), encoding="utf-8")
    path.with_suffix(path.suffix + ".sig").write_bytes(b"\x30\x02\x01\x01")
    return inspect_firmware_bundle(path)


class FakeResponse:
    status = 200

    def read(self) -> bytes:
        return json.dumps({
            "state": "pending_reboot",
            "upload_id": UPLOAD_ID,
            "sha256": hashlib.sha256(b"x" * 150_000).hexdigest(),
        }).encode("utf-8")


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

    def test_bundle_binds_image_manifest_and_signature(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "app.bin"
            bundle = write_bundle(path, b"signed image")
            self.assertEqual(bundle.manifest.sha256, bundle.image.sha256)
            self.assertEqual(
                bundle.manifest.canonical_bytes(),
                (
                    "PJOTA1\n"
                    f"sha256={bundle.image.sha256}\n"
                    f"size={bundle.image.size}\nproject=pocket_journal\n"
                    "board=waveshare-esp32-s3-touch-epaper-1.54-v2\n"
                    "target=esp32s3\nversion=2.0.0\nsecure_version=1\n"
                ).encode("ascii"),
            )
            path.write_bytes(b"changed")
            with self.assertRaisesRegex(DeviceError, "size|sha256"):
                inspect_firmware_bundle(path)

    def test_manifest_signature_known_vector_and_tamper_rejection(self) -> None:
        manifest = FirmwareManifest(
            1703936,
            "0123456789abcdef" * 4,
            "pocket_journal",
            "waveshare-esp32-s3-touch-epaper-1.54-v2",
            "esp32s3",
            "2.0.0",
            1,
        )
        signature = bytes.fromhex(
            "304502201cce6bb5eea3fca9f3836139a8f4a27e7fd0c4303e918a0d6bfe7e0d"
            "8343687a02210080b29c5d09145a73efeaaaf020cde3934b7f9926db8459391f61"
            "3f1cc4b76bdd"
        )
        self.assertTrue(verify_p256_sha256(manifest.canonical_bytes(), signature, P256_G))
        tampered = manifest.canonical_bytes().replace(b"version=2.0.0", b"version=2.0.1")
        self.assertFalse(verify_p256_sha256(tampered, signature, P256_G))

    def test_bundle_fails_closed_without_signature(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "app.bin"
            bundle = write_bundle(path)
            self.assertTrue(bundle.signature)
            path.with_suffix(path.suffix + ".sig").unlink()
            with self.assertRaisesRegex(DeviceError, "cannot read firmware signature"):
                inspect_firmware_bundle(path)

    def test_bundle_rejects_manifest_that_disagrees_with_esp_descriptor(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "app.bin"
            write_bundle(path)
            manifest_path = path.with_suffix(path.suffix + ".manifest.json")
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["version"] = "3.0.0"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(DeviceError, "version does not match"):
                inspect_firmware_bundle(path)

    def test_bundle_requires_strict_semver_candidate(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "app.bin"
            write_bundle(path)
            manifest_path = path.with_suffix(path.suffix + ".manifest.json")
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            for version in ("dev-hash", "v2.0.0", "02.0.0"):
                manifest["version"] = version
                manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
                with self.assertRaisesRegex(DeviceError, "strict X.Y.Z semver"):
                    inspect_firmware_bundle(path)

    def test_bundle_rejects_duplicate_manifest_fields(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "app.bin"
            write_bundle(path)
            manifest_path = path.with_suffix(path.suffix + ".manifest.json")
            manifest_path.write_text('{"size":1,"size":2}', encoding="utf-8")
            with self.assertRaisesRegex(DeviceError, "not valid JSON"):
                inspect_firmware_bundle(path)

    def test_stream_upload_is_bounded_authenticated_and_reports_progress(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "app.bin"
            payload = b"x" * 150_000
            path.write_bytes(payload)
            image = inspect_firmware_image(path)
            progress: list[tuple[int, int]] = []
            client = DeviceClient("http://device.local:8080/prefix", "secret", timeout=7)
            with patch("pocket_journal_partner.ota.http.client.HTTPConnection", FakeConnection):
                result = stream_firmware_image(
                    client,
                    image,
                    upload_id=UPLOAD_ID,
                    progress=lambda sent, total: progress.append((sent, total)),
                )

        connection = FakeConnection.instances[0]
        self.assertEqual(connection.init, ("device.local", 8080, 7))
        self.assertEqual(connection.request, ("POST", "/prefix/v1/ota"))
        self.assertEqual(connection.headers["Authorization"], "Bearer secret")
        self.assertEqual(connection.headers["X-PJ-Image-SHA256"], image.sha256)
        self.assertEqual(connection.headers["X-PJ-Upload-ID"], UPLOAD_ID)
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
                    stream_firmware_image(
                        client, inspect_firmware_image(path), upload_id=UPLOAD_ID
                    )
        self.assertNotIn("not-printed", str(raised.exception))

    def test_upload_busy_after_preflight_is_actionable(self) -> None:
        class BusyResponse(FakeResponse):
            status = 409

            def read(self) -> bytes:
                return b'{"error":"OTA upload rejected","code":"ota_busy"}'

        class BusyConnection(FakeConnection):
            def getresponse(self) -> BusyResponse:
                return BusyResponse()

        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "app.bin"
            path.write_bytes(b"x" * 1024)
            with patch("pocket_journal_partner.ota.http.client.HTTPConnection", BusyConnection):
                with self.assertRaisesRegex(DeviceError, "ota_busy") as raised:
                    stream_firmware_image(
                        DeviceClient("http://device.local", "token"),
                        inspect_firmware_image(path),
                        upload_id=UPLOAD_ID,
                    )
        self.assertEqual(getattr(raised.exception, "code", None), "ota_busy")

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
                    stream_firmware_image(
                        DeviceClient("http://device.local", "token"),
                        image,
                        upload_id=UPLOAD_ID,
                    )

    def test_preflight_rejection_is_actionable(self) -> None:
        client = DeviceClient("http://device.local", "token")
        client._request = lambda *args: {"accepted": False, "reason": "project mismatch"}  # type: ignore[method-assign]
        bundle = FirmwareBundle(
            FirmwareImage(Path("app.bin"), 3, "a" * 64),
            FirmwareManifest(
                3, "a" * 64, "pocket_journal", "board", "esp32s3", "2.0.0", 1
            ),
            b"signature",
        )
        with self.assertRaisesRegex(DeviceError, "project mismatch"):
            ota_preflight(client, bundle)

    def test_preflight_sends_every_signed_field_and_requires_upload_id(self) -> None:
        client = DeviceClient("http://device.local", "token")
        captured: dict[str, object] = {}
        bundle = FirmwareBundle(
            FirmwareImage(Path("app.bin"), 3, "a" * 64),
            FirmwareManifest(
                3, "a" * 64, "pocket_journal", "board", "esp32s3", "2.0.0", 7
            ),
            b"signature",
        )

        def request(method, path, body):
            captured.update(body)
            return {"accepted": True, "upload_id": UPLOAD_ID}

        client._request = request  # type: ignore[method-assign]
        self.assertEqual(ota_preflight(client, bundle)["upload_id"], UPLOAD_ID)
        self.assertEqual(captured["secure_version"], 7)
        self.assertEqual(captured["signature"], b"signature".hex())
        client._request = lambda *args: {"accepted": True, "upload_id": "bad"}  # type: ignore[method-assign]
        with self.assertRaisesRegex(DeviceError, "invalid upload id"):
            ota_preflight(client, bundle)

    def test_wait_reconnects_using_exact_discovered_device_id(self) -> None:
        old = DeviceClient("http://old.local", "token")
        calls: list[str] = []

        def status(client: DeviceClient):
            calls.append(client.base_url)
            if client.base_url == "http://old.local":
                raise DeviceError("offline")
            return {"device_id": "pj-test", "state": "confirmed", "version": "v2"}

        discovered = [{"device_id": "other", "base_url": "http://wrong.local"},
                      {"device_id": "pj-test", "base_url": "http://new.local"}]
        with patch("pocket_journal_partner.ota.ota_status", status):
            client, outcome = wait_for_ota_result(
                old, "pj-test", timeout=2, interval=0, discover=lambda: discovered, sleep=lambda _: None
            )
        self.assertEqual(client.base_url, "http://new.local")
        self.assertEqual(outcome["state"], "confirmed")
        self.assertEqual(calls, ["http://old.local", "http://new.local"])

    def test_wait_rejects_status_without_exact_device_id(self) -> None:
        client = DeviceClient("http://device.local", "token")
        outcomes = iter([
            {"state": "confirmed"},
            {"device_id": "pj-test", "state": "confirmed"},
        ])
        with patch("pocket_journal_partner.ota.ota_status", side_effect=lambda _: next(outcomes)):
            _, outcome = wait_for_ota_result(
                client,
                "pj-test",
                timeout=2,
                interval=0,
                discover=lambda: [],
                sleep=lambda _: None,
            )
        self.assertEqual(outcome["device_id"], "pj-test")

    def test_cli_requires_yes_noninteractively_before_upload(self) -> None:
        client = DeviceClient("http://device.local", "token")
        client.status = lambda: {"capabilities": {"ota.write": True}}  # type: ignore[method-assign]
        session = DeviceSession("pj-test", client)
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "app.bin"
            write_bundle(path)
            with patch("pocket_journal_partner.cli._lan_session_from_args", return_value=session), \
                 patch("pocket_journal_partner.cli.ota_preflight", return_value={"accepted": True, "upload_id": UPLOAD_ID, "device_id": "pj-test", "target_version": "2.0.0"}), \
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
            bundle = write_bundle(path)

            def upload(_client, image, *, upload_id, progress):
                self.assertEqual(upload_id, UPLOAD_ID)
                progress(image.size, image.size)
                return {"state": "pending_reboot"}

            stdout, stderr = StringIO(), StringIO()
            with patch("pocket_journal_partner.cli._lan_session_from_args", return_value=session), \
                 patch("pocket_journal_partner.cli.ota_preflight", return_value={"accepted": True, "upload_id": UPLOAD_ID, "device_id": "pj-test", "target_version": "2.0.0"}), \
                 patch("pocket_journal_partner.cli.stream_firmware_image", side_effect=upload), \
                 patch("pocket_journal_partner.cli.wait_for_ota_result", return_value=(client, {"state": "confirmed", "running_version": "2.0.0", "target_sha256": bundle.image.sha256, "target_partition_matches": True, "boot_outcome": "confirmed"})), \
                 redirect_stdout(stdout), redirect_stderr(stderr):
                result = cli.main(["firmware", "update", "--device", "pj-test", "--file", str(path), "--yes"])
        self.assertEqual(result, 0)
        self.assertEqual(json.loads(stdout.getvalue())["result"]["outcome"]["state"], "confirmed")
        self.assertIn("100%", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
