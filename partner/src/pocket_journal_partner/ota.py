from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable
from urllib import parse
import hashlib
import http.client
import json
import re
import struct
import time

from .device import DeviceClient, DeviceError, DeviceHTTPError, discover_mdns


OTA_CHUNK_BYTES = 64 * 1024
DEFAULT_MAX_IMAGE_BYTES = 2 * 1024 * 1024
MAX_SIGNATURE_BYTES = 128
_SIGNED_TEXT = re.compile(r"^[A-Za-z0-9._+\-]+$")
_STRICT_SEMVER = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
_MANIFEST_FIELDS = {
    "size", "sha256", "project", "board", "target", "version", "secure_version"
}
_ESP_TARGETS = {0x0009: "esp32s3"}


@dataclass(frozen=True)
class FirmwareImage:
    path: Path
    size: int
    sha256: str


@dataclass(frozen=True)
class FirmwareManifest:
    size: int
    sha256: str
    project: str
    board: str
    target: str
    version: str
    secure_version: int

    def canonical_bytes(self) -> bytes:
        return (
            "PJOTA1\n"
            f"sha256={self.sha256}\n"
            f"size={self.size}\n"
            f"project={self.project}\n"
            f"board={self.board}\n"
            f"target={self.target}\n"
            f"version={self.version}\n"
            f"secure_version={self.secure_version}\n"
        ).encode("ascii")


@dataclass(frozen=True)
class FirmwareBundle:
    image: FirmwareImage
    manifest: FirmwareManifest
    signature: bytes


def _unique_json_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for name, value in pairs:
        if name in result:
            raise ValueError(f"duplicate field {name}")
        result[name] = value
    return result


def inspect_firmware_image(path: Path, max_bytes: int = DEFAULT_MAX_IMAGE_BYTES) -> FirmwareImage:
    try:
        stat = path.stat()
    except OSError as exc:
        raise DeviceError(f"cannot read firmware image {path}: {exc}") from exc
    if not path.is_file():
        raise DeviceError(f"firmware image is not a regular file: {path}")
    if stat.st_size <= 0:
        raise DeviceError("firmware image is empty")
    if stat.st_size > max_bytes:
        raise DeviceError(f"firmware image exceeds the {max_bytes}-byte partner limit")

    digest = hashlib.sha256()
    try:
        with path.open("rb") as handle:
            while chunk := handle.read(OTA_CHUNK_BYTES):
                digest.update(chunk)
    except OSError as exc:
        raise DeviceError(f"cannot read firmware image {path}: {exc}") from exc
    return FirmwareImage(path=path, size=stat.st_size, sha256=digest.hexdigest())


def _manifest_text(value: Any, field: str) -> str:
    if not isinstance(value, str) or not 1 <= len(value) < 64 or _SIGNED_TEXT.fullmatch(value) is None:
        raise DeviceError(f"firmware manifest has invalid {field}")
    return value


def _manifest_integer(value: Any, field: str, *, positive: bool = False) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < (1 if positive else 0):
        raise DeviceError(f"firmware manifest has invalid {field}")
    return value


def _manifest_version(value: Any) -> str:
    version = _manifest_text(value, "version")
    if _STRICT_SEMVER.fullmatch(version) is None:
        raise DeviceError("firmware manifest version must be strict X.Y.Z semver")
    return version


def _read_signature(path: Path) -> bytes:
    try:
        raw = path.read_bytes()
    except OSError as exc:
        raise DeviceError(f"cannot read firmware signature {path}: {exc}") from exc
    stripped = raw.strip()
    if stripped and len(stripped) % 2 == 0:
        try:
            text = stripped.decode("ascii")
            if all(character in "0123456789abcdefABCDEF" for character in text):
                raw = bytes.fromhex(text)
        except UnicodeDecodeError:
            pass
    if not 1 <= len(raw) <= MAX_SIGNATURE_BYTES:
        raise DeviceError(f"firmware signature must contain 1-{MAX_SIGNATURE_BYTES} bytes")
    return raw


def _read_esp_descriptor(path: Path) -> tuple[str, str, str, int]:
    try:
        with path.open("rb") as handle:
            header = handle.read(32 + 256)
    except OSError as exc:
        raise DeviceError(f"cannot inspect ESP firmware image {path}: {exc}") from exc
    if len(header) < 32 + 256 or header[0] != 0xE9 or not 1 <= header[1] <= 16:
        raise DeviceError("firmware image is not a valid ESP application image")
    chip_id = struct.unpack_from("<H", header, 12)[0]
    target = _ESP_TARGETS.get(chip_id)
    if target is None:
        raise DeviceError(f"firmware image targets unsupported ESP chip id 0x{chip_id:04x}")
    if struct.unpack_from("<I", header, 32)[0] != 0xABCD5432:
        raise DeviceError("firmware image is missing its ESP application descriptor")
    secure_version = struct.unpack_from("<I", header, 36)[0]

    def text(offset: int, field: str) -> str:
        raw = header[offset : offset + 32].split(b"\0", 1)[0]
        try:
            value = raw.decode("ascii")
        except UnicodeDecodeError as exc:
            raise DeviceError(f"firmware image has invalid {field}") from exc
        return _manifest_text(value, field)

    return text(80, "project"), text(48, "version"), target, secure_version


def inspect_firmware_bundle(
    image_path: Path,
    *,
    manifest_path: Path | None = None,
    signature_path: Path | None = None,
    max_bytes: int = DEFAULT_MAX_IMAGE_BYTES,
) -> FirmwareBundle:
    image = inspect_firmware_image(image_path, max_bytes=max_bytes)
    manifest_file = manifest_path or image_path.with_suffix(image_path.suffix + ".manifest.json")
    signature_file = signature_path or image_path.with_suffix(image_path.suffix + ".sig")
    try:
        payload = json.loads(
            manifest_file.read_text(encoding="utf-8"),
            object_pairs_hook=_unique_json_object,
        )
    except OSError as exc:
        raise DeviceError(f"cannot read firmware manifest {manifest_file}: {exc}") from exc
    except (UnicodeDecodeError, ValueError) as exc:
        raise DeviceError(f"firmware manifest is not valid JSON: {manifest_file}") from exc
    if not isinstance(payload, dict):
        raise DeviceError("firmware manifest must be a JSON object")
    unknown = set(payload) - _MANIFEST_FIELDS
    if unknown:
        raise DeviceError(f"firmware manifest has unsupported fields: {', '.join(sorted(unknown))}")
    digest = payload.get("sha256")
    if not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None:
        raise DeviceError("firmware manifest has invalid sha256")
    manifest = FirmwareManifest(
        size=_manifest_integer(payload.get("size"), "size", positive=True),
        sha256=digest,
        project=_manifest_text(payload.get("project"), "project"),
        board=_manifest_text(payload.get("board"), "board"),
        target=_manifest_text(payload.get("target"), "target"),
        version=_manifest_version(payload.get("version")),
        secure_version=_manifest_integer(payload.get("secure_version"), "secure_version"),
    )
    if manifest.size != image.size:
        raise DeviceError(
            f"firmware manifest size {manifest.size} does not match image size {image.size}"
        )
    if manifest.sha256 != image.sha256:
        raise DeviceError("firmware manifest sha256 does not match image")
    image_project, image_version, image_target, image_secure_version = _read_esp_descriptor(
        image_path
    )
    if image_project != manifest.project:
        raise DeviceError("firmware manifest project does not match the ESP image")
    if image_version != manifest.version:
        raise DeviceError("firmware manifest version does not match the ESP image")
    if image_target != manifest.target:
        raise DeviceError("firmware manifest target does not match the ESP image")
    if image_secure_version != manifest.secure_version:
        raise DeviceError("firmware manifest secure_version does not match the ESP image")
    return FirmwareBundle(image=image, manifest=manifest, signature=_read_signature(signature_file))


def _decode_response(response: http.client.HTTPResponse, method: str, path: str) -> Any:
    payload = response.read()
    if not 200 <= response.status < 300:
        retryable = response.status in {408, 409, 425, 429} or 500 <= response.status < 600
        code = None
        if len(payload) <= 4096:
            try:
                error_payload = json.loads(payload.decode("utf-8"))
                candidate = error_payload.get("code") if isinstance(error_payload, dict) else None
                if isinstance(candidate, str) and re.fullmatch(r"[a-z0-9._-]{1,64}", candidate):
                    code = candidate
            except (UnicodeDecodeError, json.JSONDecodeError):
                pass
        detail = f"HTTP {response.status}" + (f" ({code})" if code else "")
        raise DeviceHTTPError(
            f"{method} {path} failed: {detail}", response.status, retryable,
            code=code,
        )
    if not payload:
        return None
    try:
        return json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise DeviceError(f"{method} {path} failed: device returned invalid JSON") from exc


def stream_firmware_image(
    client: DeviceClient,
    image: FirmwareImage,
    *,
    upload_id: str,
    activate: bool = True,
    progress: Callable[[int, int], None] | None = None,
    chunk_bytes: int = OTA_CHUNK_BYTES,
) -> dict[str, Any]:
    if chunk_bytes <= 0 or chunk_bytes > OTA_CHUNK_BYTES:
        raise ValueError(f"chunk_bytes must be between 1 and {OTA_CHUNK_BYTES}")
    if re.fullmatch(r"[0-9a-f]{32}", upload_id) is None:
        raise DeviceError("device returned an invalid OTA upload id")
    parsed = parse.urlparse(client.base_url)
    connection_type = http.client.HTTPSConnection if parsed.scheme == "https" else http.client.HTTPConnection
    connection = connection_type(parsed.hostname, parsed.port, timeout=client.timeout)
    prefix = parsed.path.rstrip("/")
    path = f"{prefix}/v1/ota" or "/v1/ota"
    headers = {
        "Authorization": f"Bearer {client.token}",
        "Content-Type": "application/octet-stream",
        "Content-Length": str(image.size),
        "X-PJ-Image-SHA256": image.sha256,
        "X-PJ-Upload-ID": upload_id,
        "X-PJ-Activate": "true" if activate else "false",
    }
    sent = 0
    uploaded_digest = hashlib.sha256()
    try:
        connection.putrequest("POST", path)
        for name, value in headers.items():
            connection.putheader(name, value)
        connection.endheaders()
        with image.path.open("rb") as handle:
            while sent < image.size:
                chunk = handle.read(min(chunk_bytes, image.size - sent))
                if not chunk:
                    raise DeviceError("firmware image changed or was truncated during upload")
                connection.send(chunk)
                sent += len(chunk)
                uploaded_digest.update(chunk)
                if progress is not None:
                    progress(sent, image.size)
            if handle.read(1) or uploaded_digest.hexdigest() != image.sha256:
                raise DeviceError("firmware image changed during upload")
        result = _decode_response(connection.getresponse(), "POST", "/v1/ota")
    except DeviceError:
        raise
    except (OSError, http.client.HTTPException) as exc:
        raise DeviceError(f"POST /v1/ota failed after {sent} of {image.size} bytes: {exc}") from exc
    finally:
        connection.close()
    if not isinstance(result, dict):
        raise DeviceError("POST /v1/ota failed: device returned an invalid result")
    if (
        result.get("state") != "pending_reboot"
        or result.get("upload_id") != upload_id
        or result.get("sha256") != image.sha256
    ):
        raise DeviceError("POST /v1/ota failed: device did not confirm the exact uploaded image")
    return result


def ota_preflight(client: DeviceClient, bundle: FirmwareBundle) -> dict[str, Any]:
    manifest = bundle.manifest
    result = client._request("POST", "/v1/ota/preflight", {
        "size": manifest.size,
        "sha256": manifest.sha256,
        "project": manifest.project,
        "board": manifest.board,
        "target": manifest.target,
        "version": manifest.version,
        "secure_version": manifest.secure_version,
        "signature": bundle.signature.hex(),
    })
    if not isinstance(result, dict):
        raise DeviceError("POST /v1/ota/preflight failed: device returned an invalid result")
    if result.get("accepted") is not True:
        reason = result.get("reason") or "device rejected firmware image"
        raise DeviceError(f"OTA preflight rejected: {reason}")
    if re.fullmatch(r"[0-9a-f]{32}", str(result.get("upload_id", ""))) is None:
        raise DeviceError("OTA preflight failed: device returned an invalid upload id")
    return result


def ota_status(client: DeviceClient) -> dict[str, Any]:
    result = client._request("GET", "/v1/ota")
    if not isinstance(result, dict):
        raise DeviceError("GET /v1/ota failed: device returned an invalid status")
    return result


def wait_for_ota_result(
    client: DeviceClient,
    device_id: str,
    *,
    timeout: float,
    interval: float = 1.0,
    discover: Callable[[], list[dict[str, str]]] = discover_mdns,
    sleep: Callable[[float], None] = time.sleep,
) -> tuple[DeviceClient, dict[str, Any]]:
    deadline = time.monotonic() + timeout
    current = client
    last_error: DeviceError | None = None
    while time.monotonic() < deadline:
        try:
            status = ota_status(current)
            reported_device = status.get("device_id")
            if reported_device != device_id:
                raise DeviceError(
                    f"OTA status came from {reported_device}, expected exact device {device_id}"
                )
            state = status.get("state")
            if state in {"confirmed", "rolled_back", "failed"}:
                return current, status
        except DeviceError as exc:
            last_error = exc
        for found in discover():
            if found.get("device_id") == device_id and found.get("base_url"):
                current = DeviceClient(str(found["base_url"]), client.token, client.timeout)
                break
        sleep(interval)
    detail = f": {last_error}" if last_error is not None else ""
    raise DeviceError(f"timed out waiting for OTA result after {timeout:g} seconds{detail}")
