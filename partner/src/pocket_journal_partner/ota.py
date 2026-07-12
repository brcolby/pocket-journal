from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable
from urllib import parse
import hashlib
import http.client
import json
import time

from .device import DeviceClient, DeviceError, DeviceHTTPError, discover_mdns


OTA_CHUNK_BYTES = 64 * 1024
DEFAULT_MAX_IMAGE_BYTES = 16 * 1024 * 1024


@dataclass(frozen=True)
class FirmwareImage:
    path: Path
    size: int
    sha256: str


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


def _decode_response(response: http.client.HTTPResponse, method: str, path: str) -> Any:
    payload = response.read()
    if not 200 <= response.status < 300:
        retryable = response.status in {408, 409, 425, 429} or 500 <= response.status < 600
        raise DeviceHTTPError(
            f"{method} {path} failed: HTTP {response.status}", response.status, retryable
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
    activate: bool = True,
    progress: Callable[[int, int], None] | None = None,
    chunk_bytes: int = OTA_CHUNK_BYTES,
) -> dict[str, Any]:
    if chunk_bytes <= 0 or chunk_bytes > OTA_CHUNK_BYTES:
        raise ValueError(f"chunk_bytes must be between 1 and {OTA_CHUNK_BYTES}")
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
    return result


def ota_preflight(client: DeviceClient, image: FirmwareImage) -> dict[str, Any]:
    result = client._request("POST", "/v1/ota/preflight", {
        "size": image.size,
        "sha256": image.sha256,
    })
    if not isinstance(result, dict):
        raise DeviceError("POST /v1/ota/preflight failed: device returned an invalid result")
    if result.get("accepted") is not True:
        reason = result.get("reason") or "device rejected firmware image"
        raise DeviceError(f"OTA preflight rejected: {reason}")
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
