from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib import error, parse, request
import glob
import json
import os
import time


class DeviceError(RuntimeError):
    pass


class DeviceHTTPError(DeviceError):
    def __init__(self, message: str, status_code: int, retryable: bool) -> None:
        super().__init__(message)
        self.status_code = status_code
        self.retryable = retryable


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

    def _request(self, method: str, path: str, body: Any | None = None) -> Any:
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
        try:
            with request.urlopen(req, timeout=self.timeout) as response:
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
            if exc.code == 401:
                detail = "authentication failed; verify the paired device token"
            elif exc.code == 404:
                detail = "capability is not supported by this firmware"
            elif exc.code == 409:
                detail = "device is busy; stop recording or playback and retry"
            else:
                detail = f"HTTP {exc.code}"
            retryable = exc.code in {408, 409, 425, 429} or 500 <= exc.code < 600
            raise DeviceHTTPError(
                f"{method} {path} failed: {detail}", exc.code, retryable
            ) from exc
        except error.URLError as exc:
            raise DeviceError(f"{method} {path} failed: {exc.reason}") from exc

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
        return self._request("DELETE", "/v1/audio")

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

    def _request(self, command: str) -> dict[str, Any]:
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise DeviceError("USB support requires pyserial; reinstall the partner CLI") from exc

        connection = None
        try:
            # Configure the control lines before opening the descriptor. Opening
            # with a port argument lets pyserial apply its default DTR/RTS state
            # first, which can reset an ESP32-S3 or select its ROM downloader.
            serial_options: dict[str, Any] = {
                "port": None,
                "baudrate": self.baudrate,
                "timeout": 0.2,
                "write_timeout": 2.0,
                "dsrdtr": False,
                "rtscts": False,
            }
            if os.name != "nt":
                serial_options["exclusive"] = True
            connection = serial.Serial(
                **serial_options,
            )
            connection.port = self.port
            connection.dtr = False
            connection.rts = False
            connection.open()
            connection.dtr = False
            connection.rts = False
            time.sleep(0.1)
            connection.reset_input_buffer()
            connection.write(f"{command}\n".encode("ascii"))
            connection.flush()
            deadline = time.monotonic() + self.timeout
            while time.monotonic() < deadline:
                raw = connection.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if line.startswith("PJ_OK "):
                    try:
                        return json.loads(line[6:])
                    except json.JSONDecodeError as exc:
                        raise DeviceError("USB command failed: device returned invalid JSON") from exc
                if line.startswith("PJ_ERR "):
                    try:
                        payload = json.loads(line[7:])
                        message = payload.get("error", line[7:])
                    except json.JSONDecodeError:
                        message = line[7:]
                    raise DeviceError(f"USB command failed: {message}")
        except serial.SerialException as exc:
            raise DeviceError(f"USB serial connection failed on {self.port}: {exc}") from exc
        finally:
            if connection is not None:
                try:
                    connection.dtr = False
                    connection.rts = False
                except (serial.SerialException, OSError):
                    pass
                try:
                    if connection.is_open:
                        connection.close()
                except (serial.SerialException, OSError):
                    pass
        raise DeviceError(
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
        return self._request("PJ_WIPE_RECORDINGS")

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
