from __future__ import annotations

from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Iterator
from urllib import error, parse, request
import glob
import hashlib
import json
import os
import secrets
import socket
import subprocess
import sys
import threading
import time
import unicodedata

from .companion_auth import MAX_ERROR_BYTES, normalize_error


class DeviceError(RuntimeError):
    pass


class DeviceHTTPError(DeviceError):
    def __init__(
        self,
        message: str,
        status_code: int,
        retryable: bool,
        *,
        code: str | None = None,
        device_error: str | None = None,
        operation: dict[str, Any] | None = None,
    ) -> None:
        super().__init__(message)
        self.status_code = status_code
        self.retryable = retryable
        self.code = code
        self.device_error = device_error
        self.operation = operation


class DeviceOperationError(DeviceError):
    def __init__(
        self,
        message: str,
        operation_id: int,
        *,
        code: str | None,
        retryable: bool,
    ) -> None:
        super().__init__(message)
        self.operation_id = operation_id
        self.code = code
        self.retryable = retryable


class DeviceOperationTimeout(DeviceOperationError):
    pass


class DeviceRequestTimeout(DeviceError):
    pass


class _SerialDeadlineExpired(TimeoutError):
    pass


class _SerialCommandRejected(DeviceError):
    def __init__(self, command: str, code: str | None) -> None:
        suffix = f" ({code})" if code is not None else ""
        super().__init__(f"USB command failed: device rejected {command}{suffix}")
        self.command = command
        self.code = code


class SerialPortNotFound(DeviceError):
    pass


class SerialPortAmbiguous(DeviceError):
    pass


DEFAULT_SERIAL_PORT = "/dev/cu.usbmodem1101"
SERIAL_PORT_PATTERNS = (
    "/dev/cu.usbmodem*",
    "/dev/cu.usbserial*",
    "/dev/ttyACM*",
    "/dev/cu.SLAB_USBtoUART*",
    "/dev/cu.wchusbserial*",
)

_SERIAL_PORT_LOCKS_GUARD = threading.Lock()
_SERIAL_PORT_LOCKS: dict[str, threading.Lock] = {}


def _serial_port_lock(port: str) -> threading.Lock:
    with _SERIAL_PORT_LOCKS_GUARD:
        lock = _SERIAL_PORT_LOCKS.get(port)
        if lock is None:
            lock = threading.Lock()
            _SERIAL_PORT_LOCKS[port] = lock
        return lock

_USB_ROM_DOWNLOAD_MARKERS = (
    "waiting for download",
    "(download(usb/uart0))",
    "boot:0x0",
)

_WIPE_STATES = {"idle", "queued", "running", "succeeded", "failed"}
_WIPE_START_RETRY_INTERVAL_SECONDS = 1.5
_WIPE_START_MAX_ATTEMPTS = 3
_USB_RESET_MARKERS = ("esp-rom:", "rst:0x")
USB_TRANSFER_CHUNK_BYTES = 256
USB_MAX_AUDIO_READ_CHUNK_BYTES = 1024
USB_TRANSCRIPT_CHUNK_BYTES = 192
USB_MAX_TEXT_BYTES = 160
USB_MAX_AUDIO_ID_BYTES = 95
USB_MAX_AUDIO_ITEMS = 10_000
USB_MAX_TRANSCRIPT_BYTES = 64 * 1024
USB_SERIAL_LINE_BYTES = 768
USB_MAX_SETTINGS_BODY_BYTES = 256
_USB_READ_RETRY_INTERVAL_SECONDS = 1.0
_USB_READ_MAX_ATTEMPTS = 2
_USB_SERIAL_CANCEL_GRACE_SECONDS = 0.25

_SETTINGS_INTEGER_RANGES = {
    "volume": (0, 10),
    "alarm_hour": (0, 23),
    "alarm_minute": (0, 59),
    "timer_seconds": (30, 86400),
    "interval_seconds": (30, 86400),
    "transcript_font_size": (2, 3),
}


def _validated_settings_response(response: dict[str, Any]) -> dict[str, Any]:
    if not isinstance(response, dict):
        raise DeviceError("device returned an invalid settings payload")
    generation = response.get("generation")
    if (
        isinstance(generation, bool)
        or not isinstance(generation, int)
        or not 0 <= generation <= 0xFFFFFFFF
    ):
        raise DeviceError("device returned an invalid settings generation")
    for key, (minimum, maximum) in _SETTINGS_INTEGER_RANGES.items():
        value = response.get(key)
        if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
            raise DeviceError(f"device returned an invalid setting: {key}")
    if response.get("theme") not in {"light", "dark"}:
        raise DeviceError("device returned an invalid setting: theme")
    if response.get("temperature_unit") not in {"c", "f"}:
        raise DeviceError("device returned an invalid setting: temperature_unit")
    for key in ("alarm_enabled", "clock_24h"):
        if not isinstance(response.get(key), bool):
            raise DeviceError(f"device returned an invalid setting: {key}")
    return response


@dataclass
class _SerialEvidence:
    device_reset: bool = False
    rom_download: bool = False

    def observe(self, raw: bytes) -> None:
        line = raw.decode("utf-8", errors="replace").strip().lower()
        if any(marker in line for marker in _USB_ROM_DOWNLOAD_MARKERS):
            self.rom_download = True
        if any(marker in line for marker in _USB_RESET_MARKERS):
            self.device_reset = True

    def clear(self) -> None:
        self.device_reset = False
        self.rom_download = False


def _serial_response(
    raw: bytes,
    expected_command: str,
    request_id: str | None,
) -> tuple[bool, dict[str, Any]] | None:
    line = raw.decode("utf-8", errors="replace").strip()
    ok = line.startswith("PJ_OK ")
    failed = line.startswith("PJ_ERR ")
    if not ok and not failed:
        return None
    encoded = line[6:] if ok else line[7:]
    try:
        payload = json.loads(encoded)
    except json.JSONDecodeError as exc:
        raise DeviceError("USB command failed: device returned invalid JSON") from exc
    if not isinstance(payload, dict):
        raise DeviceError("USB command failed: device returned invalid JSON")
    response_command = payload.get("command")
    if request_id is not None:
        if response_command != expected_command or payload.get("request_id") != request_id:
            return None
    elif response_command is not None and response_command != expected_command:
        return None
    return ok, payload


def _safe_code(value: Any) -> str | None:
    if not isinstance(value, str) or not 1 <= len(value) <= 64:
        return None
    if any(not (ch.islower() or ch.isdigit() or ch in "._-") for ch in value):
        return None
    return value


def _safe_device_error(value: Any) -> str | None:
    if not isinstance(value, str) or not 1 <= len(value) <= 160:
        return None
    if any(ord(ch) < 0x20 or ord(ch) > 0x7E or ch in {'"', "'", "\\"} for ch in value):
        return None
    lowered = value.lower()
    if any(term in lowered for term in ("authorization", "bearer", "credential", "password", "secret", "ssid", "token")):
        return None
    return value


def _decode_recording_wipe(source: Any) -> dict[str, Any] | None:
    if not isinstance(source, dict):
        return None
    operation_id = source.get("id")
    state = source.get("state")
    if isinstance(operation_id, bool) or not isinstance(operation_id, int) or operation_id <= 0:
        return None
    if state not in _WIPE_STATES:
        return None
    operation: dict[str, Any] = {"id": operation_id, "state": state}
    for field in ("audio_deleted", "transcripts_deleted", "notes_deleted"):
        value = source.get(field, 0)
        operation[field] = value if isinstance(value, int) and not isinstance(value, bool) and value >= 0 else 0
    operation["code"] = _safe_code(source.get("code"))
    operation["retryable"] = source.get("retryable") is True
    return operation


def _recording_wipe_operation(
    payload: Any,
    operation_id: int | None = None,
) -> dict[str, Any] | None:
    if not isinstance(payload, dict):
        return None
    sources = [payload.get("recording_wipe")]
    if operation_id is not None and isinstance(payload.get("recording_wipe_recent"), list):
        sources.extend(payload["recording_wipe_recent"])
    for source in sources:
        operation = _decode_recording_wipe(source)
        if operation is not None and (operation_id is None or operation["id"] == operation_id):
            return operation
    return None


def _recording_wipe_result(operation: dict[str, Any]) -> dict[str, Any]:
    return {
        "operation_id": operation["id"],
        "state": operation["state"],
        "deleted": operation["audio_deleted"],
        "audio_deleted": operation["audio_deleted"],
        "transcripts_deleted": operation["transcripts_deleted"],
        "notes_deleted": operation["notes_deleted"],
    }


def _new_request_id() -> str:
    return secrets.token_hex(8)


def _usb_hex_text(value: Any, field: str, *, required: bool = True) -> str | None:
    if value is None and not required:
        return None
    if not isinstance(value, str) or len(value) > USB_MAX_TEXT_BYTES * 2:
        raise DeviceError(f"USB command failed: invalid {field}")
    if len(value) % 2 or any(character not in "0123456789abcdef" for character in value):
        raise DeviceError(f"USB command failed: invalid {field}")
    try:
        raw = bytes.fromhex(value)
        decoded = raw.decode("utf-8")
    except (ValueError, UnicodeDecodeError) as exc:
        raise DeviceError(f"USB command failed: invalid {field}") from exc
    if any(unicodedata.category(character) == "Cc" for character in decoded):
        raise DeviceError(f"USB command failed: invalid {field}")
    if not decoded:
        if required:
            raise DeviceError(f"USB command failed: invalid {field}")
        return None
    return decoded


def _usb_encode_text(value: str, field: str) -> str:
    try:
        raw = value.encode("utf-8")
    except (AttributeError, UnicodeEncodeError) as exc:
        raise DeviceError(f"{field} must be valid UTF-8 text for USB-C transfer") from exc
    if (
        not raw
        or len(raw) > USB_MAX_TEXT_BYTES
        or any(unicodedata.category(character) == "Cc" for character in value)
    ):
        raise DeviceError(
            f"{field} must contain between 1 and {USB_MAX_TEXT_BYTES} UTF-8 bytes for USB-C transfer"
        )
    return raw.hex()


def _usb_encode_audio_id(value: str) -> str:
    encoded = _usb_encode_text(value, "audio id")
    if (
        len(encoded) > USB_MAX_AUDIO_ID_BYTES * 2
        or ".." in value
        or "/" in value
        or "\\" in value
        or not value.lower().endswith(".wav")
    ):
        raise DeviceError(
            "audio id must be a plain .wav filename of at most "
            f"{USB_MAX_AUDIO_ID_BYTES} UTF-8 bytes for USB-C transfer"
        )
    return encoded


def _usb_uint(value: Any, field: str, *, positive: bool = False) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise DeviceError(f"USB command failed: invalid {field}")
    if value < (1 if positive else 0):
        raise DeviceError(f"USB command failed: invalid {field}")
    return value


def _usb_audio_read_chunk_capability(response: dict[str, Any]) -> int:
    field = "audio_read_max_bytes"
    if field not in response:
        return USB_TRANSFER_CHUNK_BYTES
    value = response[field]
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or not 1 <= value <= USB_MAX_AUDIO_READ_CHUNK_BYTES
    ):
        raise DeviceError("USB command failed: invalid audio read chunk capability")
    return value


def _validated_companion_sync_response(response: dict[str, Any]) -> dict[str, Any]:
    states = {
        "idle", "pending", "discovering", "requesting", "running",
        "succeeded", "failed", "offline", "auth_failed", "protocol_failed",
    }
    transports = {"none", "lan", "usb"}
    for field in (
        "requested_generation", "acknowledged_generation", "active_generation",
        "claim_generation", "total", "pending", "transferred", "failed",
    ):
        _usb_uint(response.get(field), f"sync {field}")
    requested = response["requested_generation"]
    acknowledged = response["acknowledged_generation"]
    active = response["active_generation"]
    claim = response["claim_generation"]
    total = response["total"]
    pending = response["pending"]
    transferred = response["transferred"]
    failed = response["failed"]
    if transferred > total or failed > total - transferred or pending != total - transferred - failed:
        raise DeviceError("USB command failed: inconsistent sync progress counts")
    for field in ("requested_ms", "active_requested_ms", "claim_requested_ms"):
        value = response.get(field)
        if (
            isinstance(value, bool) or not isinstance(value, int)
            or not 0 <= value <= (1 << 53) - 1
        ):
            raise DeviceError(f"USB command failed: invalid sync {field}")
    if acknowledged > requested or (active and not acknowledged < active <= requested):
        raise DeviceError("USB command failed: invalid sync generations")
    request_pending = response.get("request_pending")
    online = response.get("online")
    if not isinstance(request_pending, bool) or not isinstance(online, bool):
        raise DeviceError("USB command failed: invalid sync state flags")
    if request_pending != (requested > acknowledged):
        raise DeviceError("USB command failed: inconsistent sync pending state")
    expected_claim = active if request_pending and active else requested if request_pending else 0
    if claim != expected_claim:
        raise DeviceError("USB command failed: inconsistent sync claim generation")
    expected_requested_ms = (
        response["active_requested_ms"] if active else response["requested_ms"]
    ) if request_pending else response["requested_ms"]
    if response["claim_requested_ms"] != expected_requested_ms:
        raise DeviceError("USB command failed: inconsistent sync request timestamp")
    if response.get("state") not in states or response.get("transport") not in transports:
        raise DeviceError("USB command failed: invalid sync phase or transport")
    operation_id = response.get("operation_id")
    if not isinstance(operation_id, str) or len(operation_id) > 64 or any(
        not (character.isascii() and (character.isalnum() or character in "_-"))
        for character in operation_id
    ):
        raise DeviceError("USB command failed: invalid sync operation id")
    if request_pending and not operation_id:
        raise DeviceError("USB command failed: missing sync operation id")
    error = response.get("error")
    if (
        not isinstance(error, str) or len(error.encode("utf-8")) > MAX_ERROR_BYTES
        or normalize_error(error, "") != error
    ):
        raise DeviceError("USB command failed: invalid sync error text")
    replayed = response.get("replayed")
    if replayed is not None and not isinstance(replayed, bool):
        raise DeviceError("USB command failed: invalid sync replay state")
    return response


def _usb_sha256(value: Any, field: str, *, required: bool = False) -> str | None:
    if value is None and not required:
        return None
    if (
        not isinstance(value, str)
        or len(value) != 64
        or any(character not in "0123456789abcdef" for character in value)
    ):
        raise DeviceError(f"USB command failed: invalid {field}")
    return value


def _looks_like_usb_serial_port(device: str) -> bool:
    return (
        "/usb" in device.lower()
        or "usbmodem" in device
        or "usbserial" in device
        or "ttyACM" in device
        or "SLAB_USBtoUART" in device
        or "wchusbserial" in device
    )


@dataclass
class AudioItem:
    audio_id: str
    filename: str
    label: str | None = None
    size: int | None = None
    data_bytes: int | None = None
    source_sha256: str | None = None
    created_at: str | None = None
    duration_ms: int | None = None
    synced: bool = False
    transcript_uploaded: bool = False
    transcript_path: str | None = None

    def __post_init__(self) -> None:
        if (
            not isinstance(self.source_sha256, str)
            or len(self.source_sha256) != 64
            or any(ch not in "0123456789abcdef" for ch in self.source_sha256)
        ):
            self.source_sha256 = None


class DeviceClient:
    def __init__(self, base_url: str, token: str, timeout: float = 20.0) -> None:
        parsed = parse.urlparse(base_url)
        if parsed.scheme not in {"http", "https"} or not parsed.netloc:
            raise DeviceError("device base URL must be an http:// or https:// URL with a host")
        if parsed.username is not None or parsed.password is not None:
            raise DeviceError("device base URL must not contain credentials")
        if not token:
            raise DeviceError("device bearer token is missing; provision the device again")
        self.base_url = base_url.rstrip("/")
        self.token = token
        self.timeout = timeout

    def _url(self, path: str) -> str:
        return parse.urljoin(self.base_url + "/", path.lstrip("/"))

    def _request(
        self,
        method: str,
        path: str,
        body: Any | None = None,
        *,
        timeout: float | None = None,
    ) -> Any:
        data = None
        headers = {"Authorization": f"Bearer {self.token}"}
        if body is not None:
            data = json.dumps(
                body,
                ensure_ascii=False,
                separators=(",", ":"),
                sort_keys=True,
                allow_nan=False,
            ).encode("utf-8")
            headers["Content-Type"] = "application/json"
        req = request.Request(self._url(path), data=data, method=method, headers=headers)
        request_timeout = self.timeout if timeout is None else timeout
        try:
            with request.urlopen(req, timeout=request_timeout) as response:
                payload = response.read()
                if not payload:
                    return None
                content_type = response.headers.get("Content-Type", "")
                if "application/json" in content_type:
                    try:
                        return json.loads(payload.decode("utf-8"))
                    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                        raise DeviceError(f"{method} {path} failed: device returned invalid JSON") from exc
                return payload
        except error.HTTPError as exc:
            code = None
            device_error = None
            operation = None
            response_retryable = None
            try:
                if exc.fp is None:
                    raise AttributeError("HTTP error has no response body")
                raw_error = exc.read(4097)
                if len(raw_error) <= 4096:
                    parsed_error = json.loads(raw_error.decode("utf-8"))
                    if isinstance(parsed_error, dict):
                        code = _safe_code(parsed_error.get("code"))
                        device_error = _safe_device_error(parsed_error.get("error"))
                        operation = _recording_wipe_operation(parsed_error)
                        if isinstance(parsed_error.get("retryable"), bool):
                            response_retryable = parsed_error["retryable"]
            except (AttributeError, KeyError, OSError, ValueError, UnicodeDecodeError, json.JSONDecodeError):
                pass
            if exc.code == 401:
                detail = "authentication failed; verify the paired device token"
            elif exc.code == 404:
                detail = "capability is not supported by this firmware"
            elif exc.code == 409:
                detail = "device is busy; stop recording or playback and retry"
            else:
                detail = f"HTTP {exc.code}"
            if device_error is not None:
                detail = f"{detail}; device reported: {device_error}"
            retryable = exc.code in {408, 409, 425, 429} or 500 <= exc.code < 600
            if response_retryable is not None:
                retryable = response_retryable
            raise DeviceHTTPError(
                f"{method} {path} failed: {detail}",
                exc.code,
                retryable,
                code=code,
                device_error=device_error,
                operation=operation,
            ) from exc
        except error.URLError as exc:
            if isinstance(exc.reason, (TimeoutError, socket.timeout)):
                raise DeviceRequestTimeout(f"{method} {path} timed out") from exc
            raise DeviceError(f"{method} {path} failed: {exc.reason}") from exc
        except (TimeoutError, socket.timeout) as exc:
            raise DeviceRequestTimeout(f"{method} {path} timed out") from exc

    def status(self) -> dict[str, Any]:
        return self._request("GET", "/v1/status")

    def get_settings(self) -> dict[str, Any]:
        return _validated_settings_response(self._request("GET", "/v1/settings"))

    def put_settings(self, settings: dict[str, Any]) -> dict[str, Any]:
        current = self.get_settings()
        payload = dict(settings)
        payload["expected_generation"] = current["generation"]
        response = self._request("PUT", "/v1/settings", payload)
        return _validated_settings_response(response)

    def get_time(self) -> dict[str, Any]:
        return self._request("GET", "/v1/time")

    def put_time(
        self,
        hour: int,
        minute: int,
        month: int,
        day: int,
        year: int | None = None,
        utc_offset_minutes: int | None = None,
        second: int | None = None,
    ) -> dict[str, Any] | None:
        payload = {
            "hour": hour,
            "minute": minute,
            "month": month,
            "day": day,
        }
        if year is not None:
            payload["year"] = year
        if utc_offset_minutes is not None:
            if (
                isinstance(utc_offset_minutes, bool)
                or not isinstance(utc_offset_minutes, int)
                or not -840 <= utc_offset_minutes <= 840
            ):
                raise DeviceError("UTC offset must be between -840 and 840 minutes")
            payload["utc_offset_minutes"] = utc_offset_minutes
        if second is not None:
            if isinstance(second, bool) or not isinstance(second, int) or not 0 <= second <= 59:
                raise DeviceError("second must be between 0 and 59")
            payload["second"] = second
        return self._request("PUT", "/v1/time", payload)

    def list_audio(self) -> list[AudioItem]:
        payload = self._request("GET", "/v1/audio")
        if not isinstance(payload, dict) or not isinstance(payload.get("audio", []), list):
            raise DeviceError("GET /v1/audio failed: device returned an invalid recording list")
        try:
            return [AudioItem(**item) for item in payload.get("audio", [])]
        except (TypeError, KeyError) as exc:
            raise DeviceError("GET /v1/audio failed: device returned invalid recording metadata") from exc

    def wipe_recordings(self) -> dict[str, Any] | None:
        deadline = time.monotonic() + self.timeout
        response = self._request("DELETE", "/v1/audio")
        operation = _recording_wipe_operation(response)
        if operation is None:
            if isinstance(response, dict) and isinstance(response.get("deleted"), int):
                return response
            raise DeviceError("DELETE /v1/audio failed: device returned an invalid wipe operation")

        while True:
            if operation["state"] == "succeeded":
                return _recording_wipe_result(operation)
            if operation["state"] == "failed":
                code = operation["code"]
                suffix = f" ({code})" if code is not None else ""
                raise DeviceOperationError(
                    f"recording wipe operation {operation['id']} failed{suffix}",
                    operation["id"],
                    code=code,
                    retryable=operation["retryable"],
                )
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise DeviceOperationTimeout(
                    f"recording wipe operation {operation['id']} timed out; it may still be running",
                    operation["id"],
                    code="operation_timeout",
                    retryable=True,
                )
            try:
                status = self._request(
                    "GET",
                    "/v1/status",
                    timeout=min(1.0, remaining),
                )
            except DeviceRequestTimeout:
                continue
            current = _recording_wipe_operation(status, operation["id"])
            if current is not None:
                operation = current
            remaining = deadline - time.monotonic()
            if remaining > 0 and operation["state"] not in {"succeeded", "failed"}:
                time.sleep(min(0.1, remaining))

    def download_audio(self, item: AudioItem, target_dir: Path) -> Path:
        target_dir.mkdir(parents=True, exist_ok=True)
        data = self._request("GET", f"/v1/audio/{parse.quote(item.audio_id, safe='')}")
        if not isinstance(data, bytes):
            raise DeviceError(f"GET recording {item.audio_id!r} failed: device returned non-audio content")
        filename = Path(item.filename).name
        if not filename or filename in {".", ".."}:
            raise DeviceError(f"device returned an invalid filename for audio {item.audio_id!r}")
        path = target_dir / filename
        partial = path.with_name(f".{path.name}.part")
        try:
            partial.write_bytes(data)
            partial.replace(path)
        finally:
            if partial.exists():
                partial.unlink()
        return path

    def upload_transcript(self, audio_id: str, transcript: dict[str, Any]) -> None:
        self._request("PUT", f"/v1/transcripts/{parse.quote(audio_id, safe='')}", transcript)

class SerialDeviceClient:
    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 6.0) -> None:
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self._request_lock = _serial_port_lock(port)
        self._audio_read_chunk_bytes = USB_TRANSFER_CHUNK_BYTES
        self._closed = threading.Event()
        self._active_connection_lock = threading.Lock()
        self._active_connection: Any | None = None

    def close(self) -> None:
        """Cancel active I/O and permanently release this client's descriptor."""
        self._closed.set()
        with self._active_connection_lock:
            connection = self._active_connection
        if connection is None:
            return
        self._cancel_connection(connection)

    @staticmethod
    def _cancel_connection(connection: Any) -> None:
        for method_name in ("cancel_read", "cancel_write"):
            method = getattr(connection, method_name, None)
            if callable(method):
                try:
                    method()
                except Exception:
                    pass
        try:
            # Calling close unconditionally also gives drivers that support
            # cancelling an in-progress open a chance to release it.
            connection.close()
        except Exception:
            pass

    def _run_serial_primitive(
        self,
        connection: Any,
        operation: Callable[[], Any],
        *,
        deadline: float,
    ) -> Any:
        """Bound driver calls that do not honor pyserial read/write timeouts."""
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            self._cancel_connection(connection)
            raise _SerialDeadlineExpired

        completed = threading.Event()
        abandoned = threading.Event()
        result: list[Any] = []
        failure: list[BaseException] = []

        def invoke() -> None:
            try:
                result.append(operation())
            except BaseException as exc:
                failure.append(exc)
            finally:
                if abandoned.is_set() and getattr(connection, "is_open", True):
                    self._cancel_connection(connection)
                completed.set()

        worker = threading.Thread(
            target=invoke,
            name="pj-usb-serial-primitive",
            daemon=True,
        )
        worker.start()
        if not completed.wait(remaining):
            abandoned.set()
            self._cancel_connection(connection)
            completed.wait(_USB_SERIAL_CANCEL_GRACE_SECONDS)
            raise _SerialDeadlineExpired
        if failure:
            raise failure[0]
        return result[0] if result else None

    def _ensure_open(self) -> None:
        if self._closed.is_set():
            raise DeviceError(f"USB serial client for {self.port} is closed")

    @contextmanager
    def _connection(
        self,
        serial_module: Any,
        *,
        read_timeout: float = 0.2,
        idle_on_close: bool = False,
        deadline: float | None = None,
    ) -> Iterator[Any]:
        connection = None
        try:
            # Configure the control lines before opening the descriptor. Opening
            # with a port argument lets pyserial apply its default DTR/RTS state
            # first, which can reset an ESP32-S3 or select its ROM downloader.
            serial_options: dict[str, Any] = {
                "port": None,
                "baudrate": self.baudrate,
                "timeout": read_timeout,
                "write_timeout": 2.0,
                "dsrdtr": False,
                "rtscts": False,
            }
            if os.name != "nt":
                serial_options["exclusive"] = True
            connection = serial_module.Serial(**serial_options)
            connection.port = self.port
            with self._active_connection_lock:
                self._ensure_open()
                self._active_connection = connection
            # Own the object before open starts. POSIX pyserial opens with
            # O_NONBLOCK; other drivers can now be cancelled through their
            # public cancel/close methods while open is in progress.
            _idle_serial_control_lines(connection)
            self._ensure_open()
            if deadline is None:
                _open_serial_without_modem_control(connection)
            else:
                self._run_serial_primitive(
                    connection,
                    lambda: _open_serial_without_modem_control(connection),
                    deadline=deadline,
                )
            if self._closed.is_set():
                raise DeviceError(
                    f"USB serial transfer on {self.port} was cancelled"
                )
            _disable_hangup_on_close(connection)
            yield connection
        finally:
            if connection is not None:
                with self._active_connection_lock:
                    if self._active_connection is connection:
                        self._active_connection = None
                if idle_on_close:
                    try:
                        _idle_serial_control_lines(connection)
                    except (serial_module.SerialException, OSError):
                        pass
                try:
                    if connection.is_open:
                        connection.close()
                except (serial_module.SerialException, OSError):
                    pass

    def _request_on_connection(
        self,
        connection: Any,
        command: str,
        *,
        deadline: float,
        request_id: str | None = None,
        retry_interval: float | None = None,
        max_attempts: int = 1,
        evidence: _SerialEvidence | None = None,
    ) -> dict[str, Any] | None:
        expected_command = command.split(" ", 1)[0]
        wire_command = command if request_id is None else f"{command} request_id={request_id}"
        wire_payload = f"{wire_command}\n".encode("ascii")
        attempts = 0
        next_retry_at: float | None = None
        remaining = deadline - time.monotonic()
        if remaining > 0:
            connection.write_timeout = max(0.001, min(2.0, remaining))
            connection.write(wire_payload)
            # POSIX Serial.flush() calls tcdrain(), which has no timeout and can
            # retain the descriptor indefinitely when USB re-enumerates.
            attempts = 1
            if retry_interval is not None:
                next_retry_at = time.monotonic() + retry_interval

        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            connection.timeout = min(0.2, remaining)
            raw = connection.readline()
            if raw and evidence is not None:
                evidence.observe(raw)
            decoded = _serial_response(raw, expected_command, request_id) if raw else None
            if decoded is not None:
                ok, payload = decoded
                if ok:
                    return payload

                operation = _recording_wipe_operation(payload)
                code = _safe_code(payload.get("code"))
                retryable = payload.get("retryable") is True
                if operation is not None:
                    code = operation["code"] or code
                    retryable = operation["retryable"]
                    suffix = f" ({code})" if code is not None else ""
                    raise DeviceOperationError(
                        f"USB recording wipe operation {operation['id']} failed{suffix}",
                        operation["id"],
                        code=code,
                        retryable=retryable,
                    )
                raise _SerialCommandRejected(expected_command, code)

            now = time.monotonic()
            if (
                next_retry_at is not None
                and attempts < max_attempts
                and now >= next_retry_at
                and now < deadline
            ):
                connection.write_timeout = max(0.001, min(2.0, deadline - now))
                connection.write(wire_payload)
                attempts += 1
                assert retry_interval is not None
                next_retry_at = now + retry_interval

    def _request_timeout(self, evidence: _SerialEvidence) -> DeviceRequestTimeout:
        if evidence.rom_download:
            return DeviceRequestTimeout(
                f"USB command timed out on {self.port}; the port was released after the device reported "
                "that it entered the ROM downloader. Reset or power-cycle it with AUX/BOOT released"
            )
        if evidence.device_reset:
            return DeviceRequestTimeout(
                f"USB command timed out on {self.port}; the port was released after reset evidence was "
                "observed. Wait for the application to boot, then retry once"
            )
        return DeviceRequestTimeout(
            f"USB command timed out on {self.port}; the port was released. Close any serial monitor and verify "
            "the firmware console uses USB Serial/JTAG. If the board says 'waiting for download', reset or "
            "power-cycle it with AUX/BOOT released"
        )

    def _request(
        self,
        command: str,
        *,
        timeout: float | None = None,
        deadline: float | None = None,
        request_id: str | None = None,
        retry_interval: float | None = None,
        max_attempts: int = 1,
    ) -> dict[str, Any]:
        self._ensure_open()
        with self._request_lock:
            self._ensure_open()
            return self._request_unlocked(
                command,
                timeout=timeout,
                deadline=deadline,
                request_id=request_id,
                retry_interval=retry_interval,
                max_attempts=max_attempts,
            )

    def _request_unlocked(
        self,
        command: str,
        *,
        timeout: float | None = None,
        deadline: float | None = None,
        request_id: str | None = None,
        retry_interval: float | None = None,
        max_attempts: int = 1,
    ) -> dict[str, Any]:
        self._ensure_open()
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise DeviceError("USB support requires pyserial; reinstall the partner CLI") from exc

        if timeout is not None and deadline is not None:
            raise ValueError("timeout and deadline are mutually exclusive")
        if deadline is None:
            request_timeout = max(0.0, self.timeout if timeout is None else timeout)
            deadline = time.monotonic() + request_timeout
        else:
            request_timeout = max(0.0, deadline - time.monotonic())
        if max_attempts < 1:
            raise ValueError("max_attempts must be at least one")
        if max_attempts > 1 and (
            request_id is None or retry_interval is None or retry_interval <= 0
        ):
            raise ValueError("serial retries require a request ID and positive retry interval")
        evidence = _SerialEvidence()
        try:
            with self._connection(
                serial,
                read_timeout=min(0.2, request_timeout),
                deadline=deadline,
            ) as connection:
                remaining = deadline - time.monotonic()
                if remaining > 0:
                    time.sleep(min(0.1, remaining))
                self._run_serial_primitive(
                    connection,
                    connection.reset_input_buffer,
                    deadline=deadline,
                )
                response = self._request_on_connection(
                    connection,
                    command,
                    deadline=deadline,
                    request_id=request_id,
                    retry_interval=retry_interval,
                    max_attempts=max_attempts,
                    evidence=evidence,
                )
                if response is not None:
                    return response
        except _SerialDeadlineExpired:
            raise self._request_timeout(evidence)
        except (serial.SerialException, OSError) as exc:
            if self._closed.is_set():
                raise DeviceError(
                    f"USB serial transfer on {self.port} was cancelled"
                ) from exc
            raise DeviceError(f"USB serial connection failed on {self.port}: {exc}") from exc
        raise self._request_timeout(evidence)

    @contextmanager
    def _serial_request_sequence(
        self,
    ) -> Iterator[Callable[..., dict[str, Any]]]:
        """Yield requests sharing one bounded, exclusively held serial descriptor."""
        self._ensure_open()
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise DeviceError("USB support requires pyserial; reinstall the partner CLI") from exc

        with self._request_lock:
            self._ensure_open()
            request_body_started = False
            startup_deadline = time.monotonic() + max(0.0, self.timeout)
            try:
                with self._connection(
                    serial,
                    read_timeout=min(0.2, max(0.0, self.timeout)),
                    deadline=startup_deadline,
                ) as connection:
                    remaining = startup_deadline - time.monotonic()
                    if remaining > 0:
                        time.sleep(min(0.1, remaining))
                    self._ensure_open()
                    self._run_serial_primitive(
                        connection,
                        connection.reset_input_buffer,
                        deadline=startup_deadline,
                    )

                    def request_command(
                        command: str,
                        *,
                        request_id: str | None = None,
                        retry_interval: float | None = None,
                        max_attempts: int = 1,
                    ) -> dict[str, Any]:
                        self._ensure_open()
                        if max_attempts < 1:
                            raise ValueError("max_attempts must be at least one")
                        if max_attempts > 1 and (
                            request_id is None
                            or retry_interval is None
                            or retry_interval <= 0
                        ):
                            raise ValueError(
                                "serial retries require a request ID and positive retry interval"
                            )
                        evidence = _SerialEvidence()
                        deadline = time.monotonic() + max(0.0, self.timeout)
                        try:
                            response = self._request_on_connection(
                                connection,
                                command,
                                deadline=deadline,
                                request_id=request_id,
                                retry_interval=retry_interval,
                                max_attempts=max_attempts,
                                evidence=evidence,
                            )
                        except (serial.SerialException, OSError) as exc:
                            if self._closed.is_set():
                                raise DeviceError(
                                    f"USB serial transfer on {self.port} was cancelled"
                                ) from exc
                            raise DeviceError(
                                f"USB serial connection failed on {self.port}: {exc}"
                            ) from exc
                        if response is None:
                            raise self._request_timeout(evidence)
                        return response

                    request_body_started = True
                    yield request_command
            except _SerialDeadlineExpired:
                raise self._request_timeout(_SerialEvidence())
            except (serial.SerialException, OSError) as exc:
                if request_body_started and not self._closed.is_set():
                    raise
                if self._closed.is_set():
                    raise DeviceError(
                        f"USB serial transfer on {self.port} was cancelled"
                    ) from exc
                raise DeviceError(f"USB serial connection failed on {self.port}: {exc}") from exc

    def status(self) -> dict[str, Any]:
        return self._request("PJ_STATUS")

    def companion_sync_status(self) -> dict[str, Any]:
        response = self._request(
            "PJ_SYNC_STATUS",
            request_id=_new_request_id(),
            retry_interval=_USB_READ_RETRY_INTERVAL_SECONDS,
            max_attempts=_USB_READ_MAX_ATTEMPTS,
        )
        return _validated_companion_sync_response(response)

    def companion_sync_claim(self, generation: int, operation_id: str) -> dict[str, Any]:
        if isinstance(generation, bool) or not 1 <= generation <= 0xFFFFFFFF:
            raise ValueError("sync generation must be between 1 and 4294967295")
        if not isinstance(operation_id, str) or not 1 <= len(operation_id) <= 64 or any(
            not (character.isascii() and (character.isalnum() or character in "_-"))
            for character in operation_id
        ):
            raise ValueError("invalid sync operation id")
        response = self._request(
            f"PJ_SYNC_CLAIM generation={generation} operation_id={operation_id}",
            request_id=_new_request_id(),
            retry_interval=_USB_READ_RETRY_INTERVAL_SECONDS,
            max_attempts=_USB_READ_MAX_ATTEMPTS,
        )
        result = response.get("claim_result")
        if result not in {"started", "attached", "busy", "stale"}:
            raise DeviceError("USB command failed: invalid sync claim result")
        return _validated_companion_sync_response(response)

    def companion_sync_progress(
        self,
        generation: int,
        operation_id: str,
        state: str,
        total: int,
        pending: int,
        transferred: int,
        failed: int,
        error: str = "",
    ) -> dict[str, Any]:
        if state not in {"running", "succeeded", "failed"}:
            raise ValueError("invalid companion sync progress state")
        if isinstance(generation, bool) or not 1 <= generation <= 0xFFFFFFFF:
            raise ValueError("sync generation must be between 1 and 4294967295")
        if not isinstance(operation_id, str) or not 1 <= len(operation_id) <= 64:
            raise ValueError("invalid sync operation id")
        for value in (total, pending, transferred, failed):
            if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0x7FFFFFFF:
                raise ValueError("sync progress counts must be nonnegative 32-bit integers")
        if transferred > total or failed > total - transferred or pending != total - transferred - failed:
            raise ValueError("sync progress counts are inconsistent")
        command_name = {
            "running": "PJ_SYNC_PROGRESS",
            "succeeded": "PJ_SYNC_COMPLETE",
            "failed": "PJ_SYNC_FAIL",
        }[state]
        command = (
            f"{command_name} generation={generation} operation_id={operation_id} "
            f"total={total} pending={pending} transferred={transferred} failed={failed}"
        )
        if state == "failed":
            error = normalize_error(error, "Sync failed")
            encoded_error = error.encode("utf-8")
            command += f" error_hex={encoded_error.hex()}"
        response = self._request(
            command,
            request_id=_new_request_id(),
            retry_interval=_USB_READ_RETRY_INTERVAL_SECONDS,
            max_attempts=_USB_READ_MAX_ATTEMPTS,
        )
        return _validated_companion_sync_response(response)

    def get_settings(self) -> dict[str, Any]:
        response = self._request(
            "PJ_SETTINGS_GET",
            request_id=_new_request_id(),
            retry_interval=_USB_READ_RETRY_INTERVAL_SECONDS,
            max_attempts=_USB_READ_MAX_ATTEMPTS,
        )
        return _validated_settings_response(response)

    def put_settings(self, settings: dict[str, Any]) -> dict[str, Any]:
        current = self.get_settings()
        payload = json.dumps(
            settings, ensure_ascii=True, separators=(",", ":"), sort_keys=True,
        ).encode("ascii")
        if not payload or len(payload) > USB_MAX_SETTINGS_BODY_BYTES:
            raise DeviceError(
                f"settings update exceeds the {USB_MAX_SETTINGS_BODY_BYTES}-byte USB limit"
            )
        request_id = _new_request_id()
        command = (
            f"PJ_SETTINGS_SET expected_generation={current['generation']} "
            f"payload_hex={payload.hex()}"
        )
        if len(f"{command} request_id={request_id}\n".encode("ascii")) >= USB_SERIAL_LINE_BYTES:
            raise DeviceError("settings update exceeds the USB command-line limit")
        response = self._request(
            command,
            request_id=request_id,
            retry_interval=_USB_READ_RETRY_INTERVAL_SECONDS,
            max_attempts=_USB_READ_MAX_ATTEMPTS,
        )
        return _validated_settings_response(response)

    def list_audio(self) -> list[AudioItem]:
        items: list[AudioItem] = []
        seen_ids: set[str] = set()
        cursor = 0
        snapshot = 0
        audio_read_chunk_bytes: int | None = None
        while True:
            response = self._request(
                f"PJ_AUDIO_LIST cursor={cursor} snapshot={snapshot}",
                request_id=_new_request_id(),
                retry_interval=_USB_READ_RETRY_INTERVAL_SECONDS,
                max_attempts=_USB_READ_MAX_ATTEMPTS,
            )
            response_cursor = _usb_uint(response.get("cursor"), "audio list cursor")
            response_snapshot = _usb_uint(
                response.get("snapshot"), "audio list snapshot", positive=True
            )
            response_chunk_bytes = _usb_audio_read_chunk_capability(response)
            if audio_read_chunk_bytes is None:
                audio_read_chunk_bytes = response_chunk_bytes
            elif response_chunk_bytes != audio_read_chunk_bytes:
                raise DeviceError(
                    "USB command failed: audio read chunk capability changed during transfer"
                )
            if response_cursor != cursor:
                raise DeviceError("USB command failed: audio list cursor did not match request")
            if snapshot not in {0, response_snapshot}:
                raise DeviceError("USB command failed: audio list snapshot changed during transfer")
            snapshot = response_snapshot
            done = response.get("done")
            if not isinstance(done, bool):
                raise DeviceError("USB command failed: invalid audio list completion state")
            raw_item = response.get("item")
            next_cursor = _usb_uint(response.get("next_cursor"), "next audio list cursor")
            if raw_item is None:
                if not done or next_cursor != cursor:
                    raise DeviceError("USB command failed: audio list item is missing")
                assert audio_read_chunk_bytes is not None
                self._audio_read_chunk_bytes = audio_read_chunk_bytes
                return items
            if not isinstance(raw_item, dict):
                raise DeviceError("USB command failed: audio list item is missing")
            audio_id = _usb_hex_text(raw_item.get("audio_id_hex"), "audio id")
            filename = _usb_hex_text(raw_item.get("filename_hex"), "audio filename")
            assert audio_id is not None and filename is not None
            if audio_id in seen_ids:
                raise DeviceError("USB command failed: audio list returned a duplicate id")
            if Path(filename).name != filename or filename in {".", ".."}:
                raise DeviceError("USB command failed: invalid audio filename")
            if audio_id != filename:
                raise DeviceError("USB command failed: audio id did not match filename")
            _usb_encode_audio_id(audio_id)
            seen_ids.add(audio_id)
            if len(items) >= USB_MAX_AUDIO_ITEMS:
                raise DeviceError(
                    f"USB command failed: audio list exceeds {USB_MAX_AUDIO_ITEMS} items"
                )

            def optional_uint(field: str) -> int | None:
                value = raw_item.get(field)
                return None if value is None else _usb_uint(value, field)

            label = _usb_hex_text(raw_item.get("label_hex"), "audio label", required=False)
            created_at = _usb_hex_text(
                raw_item.get("created_at_hex"), "audio creation time", required=False
            )
            transcript_path = _usb_hex_text(
                raw_item.get("transcript_path_hex"), "transcript path", required=False
            )
            source_sha256 = _usb_sha256(
                raw_item.get("source_sha256"), "audio source digest"
            )
            synced = raw_item.get("synced", False)
            transcript_uploaded = raw_item.get("transcript_uploaded", False)
            if not isinstance(synced, bool) or not isinstance(transcript_uploaded, bool):
                raise DeviceError("USB command failed: invalid audio sync state")
            items.append(AudioItem(
                audio_id=audio_id,
                filename=filename,
                label=label,
                size=optional_uint("size"),
                data_bytes=optional_uint("data_bytes"),
                source_sha256=source_sha256,
                created_at=created_at,
                duration_ms=optional_uint("duration_ms"),
                synced=synced,
                transcript_uploaded=transcript_uploaded,
                transcript_path=transcript_path,
            ))
            if next_cursor != cursor + 1:
                raise DeviceError("USB command failed: audio list cursor did not advance")
            if done:
                assert audio_read_chunk_bytes is not None
                self._audio_read_chunk_bytes = audio_read_chunk_bytes
                return items
            cursor = next_cursor

    def download_audio(self, item: AudioItem, target_dir: Path) -> Path:
        id_hex = _usb_encode_audio_id(item.audio_id)
        filename = Path(item.filename).name
        if not filename or filename in {".", ".."} or filename != item.filename:
            raise DeviceError(f"device returned an invalid filename for audio {item.audio_id!r}")
        target_dir.mkdir(parents=True, exist_ok=True)
        path = target_dir / filename
        partial = path.with_name(f".{path.name}.part")
        offset = 0
        total_bytes: int | None = None
        source_sha256 = item.source_sha256
        digest = hashlib.sha256()
        chunk_bytes = self._audio_read_chunk_bytes
        try:
            with partial.open("wb") as handle:
                with self._serial_request_sequence() as serial_request:
                    while True:
                        command = (
                            f"PJ_AUDIO_READ id_hex={id_hex} offset={offset} "
                            f"max_bytes={chunk_bytes}"
                        )
                        if source_sha256 is not None:
                            command += f" source_sha256={source_sha256}"
                        try:
                            response = serial_request(
                                command,
                                request_id=_new_request_id(),
                                retry_interval=_USB_READ_RETRY_INTERVAL_SECONDS,
                                max_attempts=_USB_READ_MAX_ATTEMPTS,
                            )
                        except _SerialCommandRejected as exc:
                            if (
                                offset == 0
                                and chunk_bytes > USB_TRANSFER_CHUNK_BYTES
                                and exc.command == "PJ_AUDIO_READ"
                                and exc.code == "invalid_request"
                            ):
                                chunk_bytes = USB_TRANSFER_CHUNK_BYTES
                                self._audio_read_chunk_bytes = chunk_bytes
                                continue
                            raise
                        if response.get("id_hex") != id_hex:
                            raise DeviceError("USB command failed: audio read id did not match request")
                        if _usb_uint(response.get("offset"), "audio read offset") != offset:
                            raise DeviceError("USB command failed: audio read offset did not match request")
                        response_total = _usb_uint(response.get("total_bytes"), "audio total bytes")
                        if total_bytes is None:
                            total_bytes = response_total
                            if item.size is not None and total_bytes != item.size:
                                raise DeviceError(
                                    f"USB audio size mismatch: expected {item.size} bytes, got {total_bytes}"
                                )
                        elif response_total != total_bytes:
                            raise DeviceError("USB command failed: audio size changed during transfer")
                        response_digest = _usb_sha256(
                            response.get("source_sha256"), "audio source digest", required=True
                        )
                        assert response_digest is not None
                        if source_sha256 is None:
                            source_sha256 = response_digest
                        elif response_digest is not None and response_digest != source_sha256:
                            raise DeviceError("USB command failed: audio digest changed during transfer")
                        data_hex = response.get("data_hex")
                        if (
                            not isinstance(data_hex, str)
                            or len(data_hex) > chunk_bytes * 2
                            or len(data_hex) % 2
                            or any(character not in "0123456789abcdef" for character in data_hex)
                        ):
                            raise DeviceError("USB command failed: invalid audio data chunk")
                        try:
                            data = bytes.fromhex(data_hex)
                        except ValueError as exc:
                            raise DeviceError("USB command failed: invalid audio data chunk") from exc
                        eof = response.get("eof")
                        if not isinstance(eof, bool):
                            raise DeviceError("USB command failed: invalid audio EOF state")
                        if not data and not eof:
                            raise DeviceError("USB command failed: empty audio chunk before EOF")
                        if offset + len(data) > total_bytes:
                            raise DeviceError("USB command failed: audio chunk exceeds declared size")
                        if eof != (offset + len(data) == total_bytes):
                            raise DeviceError("USB command failed: inconsistent audio EOF state")
                        handle.write(data)
                        digest.update(data)
                        offset += len(data)
                        if eof:
                            break
                handle.flush()
                os.fsync(handle.fileno())
            if source_sha256 is not None and digest.hexdigest() != source_sha256:
                raise DeviceError("USB audio digest verification failed")
            partial.replace(path)
            return path
        finally:
            if partial.exists():
                partial.unlink()

    def upload_transcript(self, audio_id: str, transcript: dict[str, Any]) -> None:
        id_hex = _usb_encode_audio_id(audio_id)
        try:
            payload = json.dumps(
                transcript,
                ensure_ascii=False,
                separators=(",", ":"),
                sort_keys=True,
                allow_nan=False,
            ).encode("utf-8")
        except (TypeError, ValueError) as exc:
            raise DeviceError("transcript is not valid JSON") from exc
        if not payload or len(payload) > USB_MAX_TRANSCRIPT_BYTES:
            raise DeviceError(
                f"transcript must contain between 1 and {USB_MAX_TRANSCRIPT_BYTES} bytes"
            )
        payload_sha256 = hashlib.sha256(payload).hexdigest()
        begin = self._request(
            f"PJ_TRANSCRIPT_BEGIN id_hex={id_hex} bytes={len(payload)} sha256={payload_sha256}",
            request_id=_new_request_id(),
            retry_interval=_USB_READ_RETRY_INTERVAL_SECONDS,
            max_attempts=_USB_READ_MAX_ATTEMPTS,
        )
        upload_id = _usb_uint(begin.get("upload_id"), "transcript upload id", positive=True)
        try:
            if begin.get("accepted") is not True or _usb_uint(begin.get("offset"), "upload offset") != 0:
                raise DeviceError("USB command failed: transcript upload was not accepted")
            offset = 0
            while offset < len(payload):
                chunk = payload[offset : offset + USB_TRANSCRIPT_CHUNK_BYTES]
                response = self._request(
                    f"PJ_TRANSCRIPT_WRITE upload_id={upload_id} offset={offset} data_hex={chunk.hex()}",
                    request_id=_new_request_id(),
                    retry_interval=_USB_READ_RETRY_INTERVAL_SECONDS,
                    max_attempts=_USB_READ_MAX_ATTEMPTS,
                )
                if _usb_uint(response.get("upload_id"), "transcript upload id", positive=True) != upload_id:
                    raise DeviceError("USB command failed: transcript upload id changed")
                next_offset = _usb_uint(response.get("next_offset"), "transcript next offset")
                if next_offset != offset + len(chunk):
                    raise DeviceError("USB command failed: transcript upload offset did not advance")
                offset = next_offset
            committed = self._request(
                f"PJ_TRANSCRIPT_COMMIT upload_id={upload_id} sha256={payload_sha256}",
                request_id=_new_request_id(),
                retry_interval=_USB_READ_RETRY_INTERVAL_SECONDS,
                max_attempts=_USB_READ_MAX_ATTEMPTS,
            )
            if (
                _usb_uint(committed.get("upload_id"), "transcript upload id", positive=True) != upload_id
                or committed.get("committed") is not True
                or _usb_uint(committed.get("bytes"), "transcript byte count") != len(payload)
            ):
                raise DeviceError("USB command failed: transcript commit was not confirmed")
        except (Exception, KeyboardInterrupt):
            try:
                self._request(
                    f"PJ_TRANSCRIPT_ABORT upload_id={upload_id}",
                    request_id=_new_request_id(),
                    retry_interval=_USB_READ_RETRY_INTERVAL_SECONDS,
                    max_attempts=_USB_READ_MAX_ATTEMPTS,
                )
            except Exception:
                pass
            raise

    def reset_interval(self) -> dict[str, Any]:
        response = self._request(
            "PJ_INTERVAL_RESET",
            request_id=_new_request_id(),
        )
        if response.get("reset") is not True or response.get("persisted") is not True:
            raise DeviceError("USB command failed: interval reset was not confirmed")
        return response

    def put_time(
        self,
        hour: int,
        minute: int,
        month: int,
        day: int,
        year: int | None = None,
        utc_offset_minutes: int | None = None,
        second: int | None = None,
    ) -> dict[str, Any]:
        if year is None:
            raise DeviceError("USB time sync requires a year")
        command = f"PJ_TIME {year} {month} {day} {hour} {minute}"
        if utc_offset_minutes is not None:
            if (
                isinstance(utc_offset_minutes, bool)
                or not isinstance(utc_offset_minutes, int)
                or not -840 <= utc_offset_minutes <= 840
            ):
                raise DeviceError("UTC offset must be between -840 and 840 minutes")
            command += f" {utc_offset_minutes}"
        if second is not None:
            if utc_offset_minutes is None:
                raise DeviceError("USB second precision requires a UTC offset")
            if isinstance(second, bool) or not isinstance(second, int) or not 0 <= second <= 59:
                raise DeviceError("second must be between 0 and 59")
            command += f" {second}"
        return self._request(command)

    def _wipe_operation_lost(
        self,
        operation_id: int,
        status: dict[str, Any],
    ) -> DeviceOperationError:
        current = status.get("recording_wipe")
        current_description = "missing"
        if isinstance(current, dict):
            current_id = current.get("id")
            current_state = current.get("state")
            if isinstance(current_id, int) and not isinstance(current_id, bool):
                current_description = f"{current_state or 'unknown'} id {current_id}"
        recent = status.get("recording_wipe_recent")
        retained = len(recent) if isinstance(recent, list) else 0
        return DeviceOperationError(
            f"USB recording wipe operation {operation_id} disappeared from device status "
            f"(current {current_description}, {retained} retained result(s)); the device may have reset or "
            "lost operation state. The outcome is unknown and the destructive command was not retried",
            operation_id,
            code="operation_state_lost",
            retryable=False,
        )

    def _wipe_runtime_evidence(
        self,
        operation_id: int,
        evidence: _SerialEvidence,
    ) -> DeviceOperationError | None:
        if evidence.rom_download:
            return DeviceOperationError(
                f"USB recording wipe operation {operation_id} lost its application connection when the device "
                "entered the ROM downloader; the outcome is unknown and the destructive command was not retried",
                operation_id,
                code="rom_download",
                retryable=False,
            )
        if evidence.device_reset:
            return DeviceOperationError(
                f"USB recording wipe operation {operation_id} observed a device reset before a terminal result; "
                "the outcome is unknown and the destructive command was not retried",
                operation_id,
                code="device_reset",
                retryable=False,
            )
        return None

    def wipe_recordings(self) -> dict[str, Any]:
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise DeviceError("USB support requires pyserial; reinstall the partner CLI") from exc

        deadline = time.monotonic() + self.timeout
        start_request_id = _new_request_id()
        evidence = _SerialEvidence()
        operation_id: int | None = None
        try:
            with self._connection(
                serial,
                read_timeout=min(0.2, self.timeout),
                deadline=deadline,
            ) as connection:
                remaining = deadline - time.monotonic()
                if remaining > 0:
                    time.sleep(min(0.1, remaining))
                # Every command is request-tagged, so stale frames can be rejected
                # without discarding reset or ROM evidence from the shared stream.
                response = self._request_on_connection(
                    connection,
                    "PJ_WIPE_RECORDINGS",
                    deadline=deadline,
                    request_id=start_request_id,
                    retry_interval=_WIPE_START_RETRY_INTERVAL_SECONDS,
                    max_attempts=_WIPE_START_MAX_ATTEMPTS,
                    evidence=evidence,
                )
                if response is None:
                    raise self._request_timeout(evidence)
                operation = _recording_wipe_operation(response)
                if operation is None:
                    if isinstance(response.get("deleted"), int):
                        return response
                    raise DeviceError("USB command failed: device returned an invalid wipe operation")

                operation_id = operation["id"]
                evidence.clear()
                poll_request_id = _new_request_id()
                while True:
                    if operation["state"] == "succeeded":
                        return _recording_wipe_result(operation)
                    if operation["state"] == "failed":
                        code = operation["code"]
                        suffix = f" ({code})" if code is not None else ""
                        raise DeviceOperationError(
                            f"USB recording wipe operation {operation_id} failed{suffix}",
                            operation_id,
                            code=code,
                            retryable=operation["retryable"],
                        )
                    runtime_error = self._wipe_runtime_evidence(operation_id, evidence)
                    if runtime_error is not None:
                        raise runtime_error
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise DeviceOperationTimeout(
                            f"USB recording wipe operation {operation_id} timed out; it may still be running",
                            operation_id,
                            code="operation_timeout",
                            retryable=True,
                        )
                    status = self._request_on_connection(
                        connection,
                        "PJ_STATUS",
                        deadline=min(deadline, time.monotonic() + min(0.75, remaining)),
                        request_id=poll_request_id,
                        evidence=evidence,
                    )
                    runtime_error = self._wipe_runtime_evidence(operation_id, evidence)
                    if runtime_error is not None:
                        raise runtime_error
                    if status is None:
                        continue
                    current = _recording_wipe_operation(status, operation_id)
                    if current is None:
                        raise self._wipe_operation_lost(operation_id, status)
                    operation = current
                    remaining = deadline - time.monotonic()
                    if remaining > 0 and operation["state"] not in {"succeeded", "failed"}:
                        time.sleep(min(0.1, remaining))
        except _SerialDeadlineExpired:
            if operation_id is not None:
                raise DeviceOperationTimeout(
                    f"USB recording wipe operation {operation_id} timed out; it may still be running",
                    operation_id,
                    code="operation_timeout",
                    retryable=True,
                )
            raise self._request_timeout(evidence)
        except (serial.SerialException, OSError) as exc:
            if operation_id is not None:
                raise DeviceOperationError(
                    f"USB recording wipe operation {operation_id} lost its serial connection before a terminal "
                    "result; the outcome is unknown and the destructive command was not retried",
                    operation_id,
                    code="transport_lost",
                    retryable=False,
                ) from exc
            raise DeviceError(f"USB serial connection failed on {self.port}: {exc}") from exc

    def audio_tone(
        self,
        pa_level: int | None = None,
        dout_gpio: int | None = None,
        audio_power_level: int | None = None,
        codec_gpio44: int | None = None,
        codec_gp45: int | None = None,
    ) -> dict[str, Any]:
        if pa_level is None and dout_gpio is None and audio_power_level is None and codec_gpio44 is None and codec_gp45 is None:
            return self._request("PJ_AUDIO_TONE")
        if pa_level is not None and pa_level not in (0, 1):
            raise DeviceError("pa_level must be 0 or 1")
        if dout_gpio is not None and dout_gpio < 0:
            raise DeviceError("dout_gpio must be >= 0")
        if audio_power_level is not None and audio_power_level not in (0, 1):
            raise DeviceError("audio_power_level must be 0 or 1")
        if codec_gpio44 is not None and not 0 <= codec_gpio44 <= 0xFF:
            raise DeviceError("codec_gpio44 must be 0..255")
        if codec_gp45 is not None and not 0 <= codec_gp45 <= 0xFF:
            raise DeviceError("codec_gp45 must be 0..255")
        args = []
        if pa_level is not None:
            args.append(f"pa={pa_level}")
        if dout_gpio is not None:
            args.append(f"dout={dout_gpio}")
        if audio_power_level is not None:
            args.append(f"pwr={audio_power_level}")
        if codec_gpio44 is not None:
            args.append(f"gpio44=0x{codec_gpio44:02x}")
        if codec_gp45 is not None:
            args.append(f"gp45=0x{codec_gp45:02x}")
        return self._request("PJ_AUDIO_TONE " + " ".join(args))

    def mic_check(self, duration_ms: int | None = None, gain_db: int | None = None) -> dict[str, Any]:
        args = []
        if duration_ms is not None:
            if duration_ms <= 0:
                raise DeviceError("duration_ms must be > 0")
            args.append(f"ms={duration_ms}")
        if gain_db is not None:
            if not 0 <= gain_db <= 42:
                raise DeviceError("gain_db must be 0..42")
            args.append(f"gain_db={gain_db}")
        return self._request("PJ_MIC_CHECK" + ((" " + " ".join(args)) if args else ""))

    def provision_wifi(self, ssid: str, password: str, token: str) -> dict[str, Any]:
        ssid_hex = ssid.encode("utf-8").hex()
        password_hex = password.encode("utf-8").hex()
        token_hex = token.encode("utf-8").hex()
        return self._request(f"PJ_WIFI_HEX {ssid_hex} {password_hex} {token_hex}")

    def recover_usb(self, *, probe_only: bool = False) -> dict[str, Any]:
        """Probe and, when necessary, reset an ESP32-S3 USB Serial/JTAG link."""
        if self.timeout <= 0:
            raise DeviceError("USB recovery timeout must be greater than zero")
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise DeviceError("USB support requires pyserial; reinstall the partner CLI") from exc

        started = time.monotonic()
        deadline = started + self.timeout
        initial_deadline = min(deadline, started + min(1.0, max(0.2, self.timeout / 3)))
        try:
            initial = self._probe_usb_state(serial, initial_deadline)
        except (serial.SerialException, OSError) as exc:
            raise DeviceError(
                f"USB recovery could not open {self.port}: {exc}. Close any serial monitor and retry"
            ) from exc

        report: dict[str, Any] = {
            "port": self.port,
            "initial_state": initial["state"],
            "initial_evidence": initial["evidence"],
            "recovery_attempted": False,
            "recovered": False,
        }
        if initial.get("status") is not None:
            report["status"] = initial["status"]
        if initial["state"] == "application":
            report.update({
                "final_state": "application",
                "action": "none; Pocket Journal firmware answered PJ_STATUS",
            })
            return report
        if probe_only:
            report.update({
                "final_state": initial["state"],
                "action": _usb_recovery_action(initial["state"], attempted=False),
            })
            return report

        report["recovery_attempted"] = True
        connect_mode = "no-reset" if initial["state"] == "rom_download" else "usb-reset"
        remaining = max(0.0, deadline - time.monotonic())
        watchdog_timeout = min(4.0, max(0.1, remaining * 0.6))
        watchdog_result = self._watchdog_reset_usb_serial_jtag(
            connect_mode=connect_mode,
            timeout=watchdog_timeout,
        )
        recovery_steps = [watchdog_result]
        if watchdog_result != "watchdog_reset_completed" and time.monotonic() < deadline:
            recovery_steps.append(self._hard_reset_usb_serial_jtag(serial))
        report["reset_result"] = recovery_steps[-1]
        report["recovery_steps"] = recovery_steps

        final: dict[str, Any] = {
            "state": "port_unavailable",
            "evidence": "serial_port_did_not_reopen",
        }
        while time.monotonic() < deadline:
            attempt_deadline = min(deadline, time.monotonic() + 0.75)
            try:
                final = self._probe_usb_state(serial, attempt_deadline)
            except (serial.SerialException, OSError):
                time.sleep(min(0.1, max(0.0, deadline - time.monotonic())))
                continue
            if final["state"] == "application":
                break
            if final["state"] == "rom_download":
                break
            time.sleep(min(0.1, max(0.0, deadline - time.monotonic())))

        report.update({
            "final_state": final["state"],
            "final_evidence": final["evidence"],
            "recovered": final["state"] == "application",
            "action": _usb_recovery_action(final["state"], attempted=True),
        })
        if final.get("status") is not None:
            report["status"] = final["status"]
        return report

    def _probe_usb_state(self, serial_module: Any, deadline: float) -> dict[str, Any]:
        try:
            with self._connection(
                serial_module,
                read_timeout=0.1,
                deadline=deadline,
            ) as connection:
                remaining = deadline - time.monotonic()
                if remaining > 0:
                    connection.write_timeout = max(0.001, min(2.0, remaining))
                    connection.write(b"PJ_STATUS\n")
                while time.monotonic() < deadline:
                    raw = connection.readline()
                    if not raw:
                        continue
                    line = raw.decode("utf-8", errors="replace").strip()
                    lowered = line.lower()
                    if line.startswith("PJ_OK "):
                        try:
                            status = json.loads(line[6:])
                        except json.JSONDecodeError:
                            return {"state": "application", "evidence": "PJ_OK_INVALID_JSON"}
                        return {"state": "application", "evidence": "PJ_OK", "status": status}
                    if line.startswith("PJ_ERR "):
                        return {"state": "application", "evidence": "PJ_ERR"}
                    if any(marker in lowered for marker in _USB_ROM_DOWNLOAD_MARKERS):
                        return {"state": "rom_download", "evidence": "rom_boot_log"}
        except _SerialDeadlineExpired:
            pass
        return {"state": "unresponsive", "evidence": "no_protocol_or_rom_response"}

    def _watchdog_reset_usb_serial_jtag(self, *, connect_mode: str, timeout: float) -> str:
        """Use esptool's ROM protocol to reset without relying on the RTS line."""
        command = [
            sys.executable,
            "-m",
            "esptool",
            "--chip",
            "esp32s3",
            "--port",
            self.port,
            "--baud",
            str(self.baudrate),
            "--before",
            connect_mode,
            "--after",
            "watchdog-reset",
            "--no-stub",
            "--connect-attempts",
            "1",
            "--silent",
            "read-mac",
        ]
        process = None
        try:
            process = subprocess.Popen(
                command,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            process.communicate(timeout=max(0.1, timeout))
        except subprocess.TimeoutExpired:
            if process is not None:
                _stop_child_process(process)
            return "watchdog_reset_timed_out"
        except OSError:
            if process is not None:
                _stop_child_process(process)
            return "watchdog_reset_unavailable"
        except BaseException:
            if process is not None:
                _stop_child_process(process)
            raise

        if process.returncode == 0:
            return "watchdog_reset_completed"
        return f"watchdog_reset_failed_exit_{process.returncode}"

    def _hard_reset_usb_serial_jtag(self, serial_module: Any) -> str:
        reset_asserted = False
        try:
            with self._connection(
                serial_module,
                read_timeout=0.1,
                idle_on_close=True,
            ) as connection:
                # ESP32-S3 passes uses_usb_otg=False for USB-Serial/JTAG, so its
                # esptool hard reset is a short RTS pulse with DTR left idle.
                connection.dtr = False
                connection.rts = True
                reset_asserted = True
                time.sleep(0.1)
                connection.rts = False
                connection.dtr = False
                time.sleep(0.1)
        except (serial_module.SerialException, OSError) as exc:
            if not reset_asserted:
                raise DeviceError(f"USB recovery could not start reset on {self.port}: {exc}") from exc
            return "device_reenumerated_during_reset"
        return "rts_hard_reset_completed"


def _idle_serial_control_lines(connection: Any) -> None:
    connection.dtr = False
    connection.rts = False


def _open_serial_without_modem_control(connection: Any) -> bool:
    if os.name == "nt":
        connection.open()
        return False
    method_names = ("_update_dtr_state", "_update_rts_state")
    if any(not callable(getattr(connection, name, None)) for name in method_names):
        connection.open()
        return False

    # Pyserial has no public preserve-lines mode: disabling its two open-time
    # updates normally requires enabling DSR/DTR and RTS/CTS flow control.
    instance_attributes = getattr(connection, "__dict__", {})
    previous = {
        name: instance_attributes.get(name)
        for name in method_names
        if name in instance_attributes
    }
    try:
        for name in method_names:
            setattr(connection, name, lambda: None)
        connection.open()
    except (AttributeError, TypeError) as exc:
        raise DeviceError("USB serial could not preserve modem-control state during POSIX open") from exc
    finally:
        for name in method_names:
            if name in previous:
                setattr(connection, name, previous[name])
            else:
                try:
                    delattr(connection, name)
                except AttributeError:
                    pass
    return True


def _disable_hangup_on_close(connection: Any) -> bool:
    if os.name == "nt":
        return False
    fd = getattr(connection, "fd", None)
    if not isinstance(fd, int):
        return False
    try:
        import termios
    except ImportError:
        return False
    hupcl = getattr(termios, "HUPCL", None)
    if not isinstance(hupcl, int):
        return False
    try:
        attributes = termios.tcgetattr(fd)
        cflag = attributes[2]
        if not isinstance(cflag, int):
            raise TypeError("termios cflag is not an integer")
        if cflag & hupcl:
            attributes[2] = cflag & ~hupcl
            termios.tcsetattr(fd, termios.TCSANOW, attributes)
    except (AttributeError, IndexError, OSError, TypeError) as exc:
        raise DeviceError("USB serial could not disable POSIX hangup-on-close") from exc
    return True


def _stop_child_process(process: Any) -> None:
    if process.poll() is None:
        try:
            process.terminate()
        except ProcessLookupError:
            pass
    try:
        process.communicate(timeout=0.5)
    except subprocess.TimeoutExpired:
        try:
            process.kill()
        except ProcessLookupError:
            pass
        process.communicate()


def _usb_recovery_action(state: str, *, attempted: bool) -> str:
    if state == "application":
        return "normal application boot restored; retry the original USB-C command"
    if state == "rom_download":
        prefix = "reset completed but " if attempted else ""
        return prefix + "ROM download mode is active; release AUX/BOOT, then retry recovery or power-cycle"
    if state == "port_unavailable":
        return "USB serial did not return before timeout; release AUX/BOOT and power-cycle, then rerun recovery"
    if attempted:
        return "reset completed but firmware did not answer; verify the flashed image and USB Serial/JTAG console"
    return "firmware did not answer; rerun without --probe-only to attempt a bounded reset"


def discover_serial_ports() -> list[str]:
    ports: set[str] = set()
    try:
        from serial.tools import list_ports  # type: ignore
    except ImportError:
        pass
    else:
        for port in list_ports.comports():
            device = getattr(port, "device", None)
            if device and (getattr(port, "vid", None) is not None or _looks_like_usb_serial_port(str(device))):
                ports.add(str(device))

    for pattern in SERIAL_PORT_PATTERNS:
        ports.update(glob.glob(pattern))

    def sort_key(port: str) -> tuple[int, str]:
        if port == DEFAULT_SERIAL_PORT:
            return (0, port)
        if "usbmodem" in port:
            return (1, port)
        return (2, port)

    return sorted(ports, key=sort_key)


def resolve_serial_port(port: str | None = None) -> str:
    if port:
        return port
    ports = discover_serial_ports()
    if DEFAULT_SERIAL_PORT in ports:
        return DEFAULT_SERIAL_PORT
    if len(ports) == 1:
        return ports[0]
    if not ports:
        raise SerialPortNotFound(
            f"no USB serial port found; pass --serial-port (default is {DEFAULT_SERIAL_PORT})"
        )
    raise SerialPortAmbiguous("multiple USB serial ports found; pass --serial-port: " + ", ".join(ports))


def discover_mdns() -> list[dict[str, str]]:
    try:
        from zeroconf import ServiceBrowser, ServiceListener, Zeroconf  # type: ignore
    except ImportError:
        return []

    class Listener(ServiceListener):  # type: ignore[misc]
        def __init__(self) -> None:
            self.devices: list[dict[str, str]] = []

        def add_service(self, zeroconf: Zeroconf, service_type: str, name: str) -> None:  # type: ignore[no-untyped-def]
            info = zeroconf.get_service_info(service_type, name)
            if info is None:
                return
            addresses = [f"{addr[0]}.{addr[1]}.{addr[2]}.{addr[3]}" for addr in info.addresses if len(addr) == 4]
            if not addresses:
                return
            properties = {
                key.decode("utf-8", errors="replace"): value.decode("utf-8", errors="replace")
                for key, value in info.properties.items()
            }
            self.devices.append({
                "name": name.rstrip("."),
                "host": info.server.rstrip("."),
                "base_url": f"http://{addresses[0]}:{info.port}",
                "device_id": properties.get("device_id", name.split(".", 1)[0]),
            })

        def update_service(self, zeroconf: Zeroconf, service_type: str, name: str) -> None:  # type: ignore[no-untyped-def]
            self.add_service(zeroconf, service_type, name)

        def remove_service(self, zeroconf: Zeroconf, service_type: str, name: str) -> None:  # type: ignore[no-untyped-def]
            _ = zeroconf
            _ = service_type
            _ = name

    import time

    listener = Listener()
    zeroconf = Zeroconf()
    try:
        ServiceBrowser(zeroconf, "_pocket-journal._tcp.local.", listener)
        time.sleep(2)
        unique = {
            (device["device_id"], device["base_url"]): device
            for device in listener.devices
        }
        return sorted(unique.values(), key=lambda device: (device["device_id"], device["base_url"]))
    finally:
        zeroconf.close()
