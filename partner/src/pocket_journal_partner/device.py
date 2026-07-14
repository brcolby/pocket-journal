from __future__ import annotations

from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterator
from urllib import error, parse, request
import glob
import json
import os
import secrets
import socket
import subprocess
import sys
import time


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

_USB_ROM_DOWNLOAD_MARKERS = (
    "waiting for download",
    "(download(usb/uart0))",
    "boot:0x0",
)

_WIPE_STATES = {"idle", "queued", "running", "succeeded", "failed"}


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
        return self._request("GET", "/v1/settings")

    def put_settings(self, settings: dict[str, Any]) -> dict[str, Any] | None:
        return self._request("PUT", "/v1/settings", settings)

    def get_time(self) -> dict[str, Any]:
        return self._request("GET", "/v1/time")

    def put_time(self, hour: int, minute: int, month: int, day: int, year: int | None = None) -> dict[str, Any] | None:
        payload = {
            "hour": hour,
            "minute": minute,
            "month": month,
            "day": day,
        }
        if year is not None:
            payload["year"] = year
        return self._request("PUT", "/v1/time", payload)

    def get_home_design(self) -> dict[str, Any]:
        return self._request("GET", "/v1/home")

    def put_home_design(self, design: dict[str, Any]) -> dict[str, Any] | None:
        return self._request("PUT", "/v1/home", design)

    def get_static_art(self) -> dict[str, Any]:
        return self._request("GET", "/v1/static-art")

    def put_static_art(self, art: dict[str, Any]) -> dict[str, Any] | None:
        return self._request("PUT", "/v1/static-art", art)

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

    def upload_calendar_today(self, payload: dict[str, Any]) -> None:
        self._request("PUT", "/v1/calendar/today", payload)


class SerialDeviceClient:
    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 6.0) -> None:
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout

    @contextmanager
    def _connection(self, serial_module: Any, *, read_timeout: float = 0.2) -> Iterator[Any]:
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
            _idle_serial_control_lines(connection)
            connection.open()
            _idle_serial_control_lines(connection)
            yield connection
        finally:
            if connection is not None:
                try:
                    _idle_serial_control_lines(connection)
                except (serial_module.SerialException, OSError):
                    pass
                try:
                    if connection.is_open:
                        connection.close()
                except (serial_module.SerialException, OSError):
                    pass

    def _request(
        self,
        command: str,
        *,
        timeout: float | None = None,
        request_id: str | None = None,
    ) -> dict[str, Any]:
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise DeviceError("USB support requires pyserial; reinstall the partner CLI") from exc

        request_timeout = self.timeout if timeout is None else timeout
        expected_command = command.split(" ", 1)[0]
        wire_command = command if request_id is None else f"{command} request_id={request_id}"
        try:
            with self._connection(serial) as connection:
                time.sleep(0.1)
                connection.reset_input_buffer()
                connection.write(f"{wire_command}\n".encode("ascii"))
                connection.flush()
                deadline = time.monotonic() + request_timeout
                while time.monotonic() < deadline:
                    raw = connection.readline()
                    if not raw:
                        continue
                    line = raw.decode("utf-8", errors="replace").strip()
                    ok = line.startswith("PJ_OK ")
                    failed = line.startswith("PJ_ERR ")
                    if not ok and not failed:
                        continue
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
                            continue
                    elif response_command is not None and response_command != expected_command:
                        continue
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
                    suffix = f" ({code})" if code is not None else ""
                    raise DeviceError(f"USB command failed: device rejected {expected_command}{suffix}")
        except (serial.SerialException, OSError) as exc:
            raise DeviceError(f"USB serial connection failed on {self.port}: {exc}") from exc
        raise DeviceRequestTimeout(
            f"USB command timed out on {self.port}; the port was released. Close any serial monitor and verify "
            "the firmware console uses USB Serial/JTAG. If the board says 'waiting for download', reset or "
            "power-cycle it with AUX/BOOT released"
        )

    def status(self) -> dict[str, Any]:
        return self._request("PJ_STATUS")

    def put_time(self, hour: int, minute: int, month: int, day: int, year: int | None = None) -> dict[str, Any]:
        if year is None:
            raise DeviceError("USB time sync requires a year")
        return self._request(f"PJ_TIME {year} {month} {day} {hour} {minute}")

    def wipe_recordings(self) -> dict[str, Any]:
        deadline = time.monotonic() + self.timeout
        start_request_id = _new_request_id()
        while True:
            remaining = max(0.0, deadline - time.monotonic())
            try:
                response = self._request(
                    "PJ_WIPE_RECORDINGS",
                    timeout=min(0.5, remaining),
                    request_id=start_request_id,
                )
                break
            except DeviceRequestTimeout:
                if time.monotonic() >= deadline:
                    raise
        operation = _recording_wipe_operation(response)
        if operation is None:
            if isinstance(response.get("deleted"), int):
                return response
            raise DeviceError("USB command failed: device returned an invalid wipe operation")

        while True:
            if operation["state"] == "succeeded":
                return _recording_wipe_result(operation)
            if operation["state"] == "failed":
                code = operation["code"]
                suffix = f" ({code})" if code is not None else ""
                raise DeviceOperationError(
                    f"USB recording wipe operation {operation['id']} failed{suffix}",
                    operation["id"],
                    code=code,
                    retryable=operation["retryable"],
                )
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise DeviceOperationTimeout(
                    f"USB recording wipe operation {operation['id']} timed out; it may still be running",
                    operation["id"],
                    code="operation_timeout",
                    retryable=True,
                )
            try:
                status = self._request(
                    "PJ_STATUS",
                    timeout=min(0.75, remaining),
                    request_id=_new_request_id(),
                )
            except DeviceRequestTimeout:
                continue
            current = _recording_wipe_operation(status, operation["id"])
            if current is not None:
                operation = current
            remaining = deadline - time.monotonic()
            if remaining > 0 and operation["state"] not in {"succeeded", "failed"}:
                time.sleep(min(0.1, remaining))

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
        with self._connection(serial_module, read_timeout=0.1) as connection:
            connection.write(b"PJ_STATUS\n")
            connection.flush()
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
            with self._connection(serial_module, read_timeout=0.1) as connection:
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
