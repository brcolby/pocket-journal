from __future__ import annotations

from datetime import date, datetime
from pathlib import Path
import argparse
import asyncio
import json
import secrets
import sys

from .ble import provision_wifi
from .calendar import calendar_payload_for_day
from .config import DeviceProfile, load_config, save_config
from .device import DeviceClient, DeviceError, SerialDeviceClient, discover_mdns, resolve_serial_port
from .home_design import normalize_home_design
from .operations import DeviceSession
from .ota import inspect_firmware_image, ota_preflight, ota_status, stream_firmware_image, wait_for_ota_result
from .storage import PartnerStore
from .sync import sync_device_audio
from .transcription import FakeTranscriptionBackend, backend_from_name


USB_PROVISIONING_TOKEN_BYTES = 16


def _print_json(payload) -> None:
    print(json.dumps(payload, indent=2, sort_keys=True))


def cmd_provision(args: argparse.Namespace) -> int:
    data_dir = Path(args.data_dir) if args.data_dir else None
    base_url = args.base_url or ""
    if args.ble_name and not args.ble:
        raise DeviceError("--ble-name requires --ble")
    if args.mock and not args.ble:
        raise DeviceError("--mock requires --ble")

    if not args.ble:
        # Hex encoding doubles this value on the legacy serial protocol. 128 bits
        # keeps ordinary provisioning commands below small USB console boundaries.
        token = secrets.token_urlsafe(USB_PROVISIONING_TOKEN_BYTES)
        client = SerialDeviceClient(resolve_serial_port(args.serial_port), baudrate=args.serial_baud, timeout=args.timeout)
        response = client.provision_wifi(args.ssid, args.password, token)
        device_id = str(response.get("device_id") or "pj-usb")
        profile = DeviceProfile(
            device_id=device_id,
            ble_name="",
            token=token,
            base_url=base_url,
        )
    else:
        try:
            provisioned = asyncio.run(provision_wifi(args.ble_name, args.ssid, args.password, mock=args.mock))
        except RuntimeError as exc:
            raise DeviceError(str(exc)) from exc
        profile = DeviceProfile(
            device_id=provisioned.device_id,
            ble_name=provisioned.ble_name,
            token=provisioned.token,
            base_url=base_url,
        )

    config = load_config(data_dir)
    config.devices[profile.device_id] = profile
    save_config(config, data_dir)
    _print_json({
        "device_id": profile.device_id,
        "ble_name": profile.ble_name,
        "base_url": profile.base_url,
        "provisioned": True,
    })
    return 0


def cmd_discover(args: argparse.Namespace) -> int:
    _ = args
    _print_json({"devices": discover_mdns()})
    return 0


def _client_from_args(args: argparse.Namespace) -> tuple[str, DeviceClient]:
    data_dir = Path(args.data_dir) if getattr(args, "data_dir", None) else None
    profile = load_config(data_dir).devices.get(args.device)
    base_url = args.base_url or (profile.base_url if profile else "")
    if not base_url:
        raise SystemExit("device base URL is required; pass --base-url or store it in config")
    token = getattr(args, "token", None) or (profile.token if profile else "")
    if not token:
        raise SystemExit("device bearer token is required; pass --token or provision the device")
    return args.device, DeviceClient(base_url, token)


def _control_client_from_args(args: argparse.Namespace):
    serial_port = getattr(args, "serial_port", None)
    if serial_port or not getattr(args, "device", None):
        resolved_port = resolve_serial_port(serial_port)
        return getattr(args, "device", None) or "usb", SerialDeviceClient(
            resolved_port,
            baudrate=getattr(args, "serial_baud", 115200),
            timeout=getattr(args, "timeout", 6.0),
        )
    return _client_from_args(args)


def _session_from_args(args: argparse.Namespace) -> DeviceSession:
    device_id, client = _control_client_from_args(args)
    return DeviceSession(device_id, client)


def _lan_session_from_args(args: argparse.Namespace) -> DeviceSession:
    device_id, client = _client_from_args(args)
    return DeviceSession(device_id, client)


def cmd_sync(args: argparse.Namespace) -> int:
    store = PartnerStore(Path(args.data_dir) if args.data_dir else None)
    backend = backend_from_name(args.backend)
    session = _lan_session_from_args(args)
    session.require("audio.sync")
    upload_transcripts = not isinstance(backend, FakeTranscriptionBackend)
    results = sync_device_audio(
        session.device_id,
        session.client,
        store,
        backend,  # type: ignore[arg-type]
        upload_transcripts=upload_transcripts,
        reprocess_synced=getattr(args, "reprocess", False),
    )
    uploaded_count = sum(result.get("status") == "uploaded" for result in results)
    failed_count = sum(result.get("status") == "failed" for result in results)
    transcribed_count = sum(result.get("status") == "transcribed" for result in results)
    _print_json(session.envelope({
        "results": results,
        "count": len(results),
        "synced": results,
        "uploaded_count": uploaded_count,
        "failed_count": failed_count,
        "transcribed_count": transcribed_count,
        "dry_run": not upload_transcripts,
    }))
    return 1 if failed_count else 0


def cmd_calendar_sync(args: argparse.Namespace) -> int:
    if args.fixture:
        events = json.loads(Path(args.fixture).read_text(encoding="utf-8"))
    else:
        from .calendar import fetch_google_events_for_day

        events = fetch_google_events_for_day(date.today())
    payload = calendar_payload_for_day(date.today(), events)
    session = _lan_session_from_args(args)
    session.require("calendar.write")
    session.client.upload_calendar_today(payload)  # type: ignore[union-attr]
    _print_json(session.envelope({"uploaded": len(payload["events"]), "date": payload["date"]}))
    return 0


def cmd_settings_get(args: argparse.Namespace) -> int:
    session = _lan_session_from_args(args)
    session.require("settings.read")
    _print_json(session.envelope(session.client.get_settings()))  # type: ignore[union-attr]
    return 0


def _parse_settings_assignments(assignments: list[str]) -> dict[str, object]:
    integer_keys = {
        "volume",
        "alarm_hour",
        "alarm_minute",
        "timer_seconds",
        "interval_seconds",
        "transcript_font_size",
    }
    supported_keys = integer_keys | {
        "theme", "alarm_enabled", "clock_24h", "temperature_unit"
    }
    settings: dict[str, object] = {}
    for assignment in assignments:
        if "=" not in assignment:
            raise SystemExit(f"expected key=value assignment, got {assignment}")
        key, value = assignment.split("=", 1)
        if key not in supported_keys:
            raise SystemExit(f"unsupported setting: {key}")
        if key in integer_keys:
            try:
                settings[key] = int(value)
            except ValueError as exc:
                raise SystemExit(f"expected integer for {key}, got {value}") from exc
        elif key in {"alarm_enabled", "clock_24h"}:
            if value.lower() not in {"true", "false"}:
                raise SystemExit(f"expected true or false for {key}, got {value}")
            settings[key] = value.lower() == "true"
        elif key == "theme":
            if value not in {"light", "dark"}:
                raise SystemExit(f"expected light or dark for theme, got {value}")
            settings[key] = value
        else:
            if value not in {"c", "f"}:
                raise SystemExit(f"expected c or f for temperature_unit, got {value}")
            settings[key] = value
    ranges = {
        "volume": (0, 10),
        "alarm_hour": (0, 23),
        "alarm_minute": (0, 59),
        "timer_seconds": (30, 86400),
        "interval_seconds": (60, 86400),
        "transcript_font_size": (2, 3),
    }
    for key, (minimum, maximum) in ranges.items():
        if key in settings and not minimum <= int(settings[key]) <= maximum:
            raise SystemExit(f"{key} must be between {minimum} and {maximum}")
    return settings


def cmd_settings_set(args: argparse.Namespace) -> int:
    settings = _parse_settings_assignments(args.assignments)
    session = _lan_session_from_args(args)
    session.require("settings.write")
    response = session.client.put_settings(settings)  # type: ignore[union-attr]
    _print_json(session.envelope(response or {"updated": settings}))
    return 0


def cmd_device_sync_time(args: argparse.Namespace) -> int:
    session = _session_from_args(args)
    session.require("time.write")
    now = datetime.now().astimezone()
    response = session.client.put_time(now.hour, now.minute, now.month, now.day, now.year)
    _print_json(session.envelope(response or {
            "hour": now.hour,
            "minute": now.minute,
            "year": now.year,
            "month": now.month,
            "day": now.day,
        }))
    return 0


def cmd_device_status(args: argparse.Namespace) -> int:
    session = _session_from_args(args)
    _print_json(session.envelope(session.status()))
    return 0


def cmd_firmware_status(args: argparse.Namespace) -> int:
    session = _lan_session_from_args(args)
    session.require("ota.read")
    _print_json(session.envelope(ota_status(session.client)))  # type: ignore[arg-type]
    return 0


def cmd_firmware_update(args: argparse.Namespace) -> int:
    session = _lan_session_from_args(args)
    session.require("ota.write", destructive=True)
    image = inspect_firmware_image(Path(args.file))
    preflight = ota_preflight(session.client, image)  # type: ignore[arg-type]
    if not args.yes:
        if not sys.stdin.isatty():
            raise DeviceError("refusing firmware update without --yes in a non-interactive session")
        answer = input(
            f"Update {session.device_id} with {image.path.name} ({image.size} bytes)? [y/N] "
        )
        if answer.strip().lower() not in {"y", "yes"}:
            raise DeviceError("firmware update cancelled")

    last_percent = -1
    def report_progress(sent: int, total: int) -> None:
        nonlocal last_percent
        percent = sent * 100 // total
        if percent != last_percent:
            print(f"Uploading firmware: {percent}%", file=sys.stderr)
            last_percent = percent

    upload = stream_firmware_image(session.client, image, progress=report_progress)  # type: ignore[arg-type]
    _, outcome = wait_for_ota_result(
        session.client,  # type: ignore[arg-type]
        session.device_id,
        timeout=args.reconnect_timeout,
    )
    _print_json(session.envelope({
        "image": {"path": str(image.path), "size": image.size, "sha256": image.sha256},
        "preflight": preflight,
        "upload": upload,
        "outcome": outcome,
    }))
    return 0 if outcome.get("state") == "confirmed" else 1


def cmd_recordings_wipe(args: argparse.Namespace) -> int:
    if not args.yes:
        raise SystemExit("refusing to wipe recordings without --yes")
    session = _session_from_args(args)
    session.require("recordings.delete", destructive=True)
    response = session.client.wipe_recordings()
    _print_json(session.envelope(response or {"deleted": None}))
    return 0


def _audio_item_payload(item) -> dict[str, object]:
    return {
        key: value
        for key, value in item.__dict__.items()
        if value is not None
    }


def cmd_recordings_list(args: argparse.Namespace) -> int:
    session = _lan_session_from_args(args)
    items = [_audio_item_payload(item) for item in session.list_recordings()]
    _print_json(session.envelope({"recordings": items, "count": len(items)}))
    return 0


def cmd_recordings_download(args: argparse.Namespace) -> int:
    session = _lan_session_from_args(args)
    items = session.list_recordings()
    item = next((candidate for candidate in items if candidate.audio_id == args.audio_id), None)
    if item is None:
        raise DeviceError(f"recording not found: {args.audio_id}")
    if not isinstance(session.client, DeviceClient):
        raise DeviceError("recordings.download is not supported over USB-C; use LAN/Wi-Fi")
    target_dir = Path(args.output_dir) if args.output_dir else Path.cwd()
    path = session.client.download_audio(item, target_dir)
    _print_json(session.envelope({"audio_id": item.audio_id, "path": str(path)}))
    return 0


def cmd_device_tone(args: argparse.Namespace) -> int:
    resolved_port = resolve_serial_port(args.serial_port)
    client = SerialDeviceClient(resolved_port, baudrate=args.serial_baud, timeout=args.timeout)
    response = client.audio_tone(
        args.pa_level,
        args.dout_gpio,
        args.audio_power_level,
        args.codec_gpio44,
        args.codec_gp45,
    )
    _print_json({
        "device_id": args.device or "usb",
        "transport": "usb",
        "result": response,
    })
    return 0


def cmd_device_mic_check(args: argparse.Namespace) -> int:
    resolved_port = resolve_serial_port(args.serial_port)
    client = SerialDeviceClient(resolved_port, baudrate=args.serial_baud, timeout=args.timeout)
    response = client.mic_check(args.duration_ms, args.gain_db)
    _print_json({
        "device_id": args.device or "usb",
        "transport": "usb",
        "result": response,
    })
    return 0


def cmd_home_get(args: argparse.Namespace) -> int:
    session = _lan_session_from_args(args)
    session.require("home.read")
    _print_json(session.envelope(session.client.get_home_design()))  # type: ignore[union-attr]
    return 0


def _load_home_design(path: str) -> dict:
    payload = json.loads(Path(path).read_text(encoding="utf-8"))
    try:
        return normalize_home_design(payload)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc


def cmd_home_set(args: argparse.Namespace) -> int:
    design = _load_home_design(args.file)
    session = _lan_session_from_args(args)
    session.require("home.write")
    response = session.client.put_home_design(design)  # type: ignore[union-attr]
    _print_json(session.envelope(response or {"updated": design}))
    return 0


def _load_static_art(path: str) -> dict:
    source = Path(path)
    if source.suffix.lower() == ".json":
        payload = json.loads(source.read_text(encoding="utf-8"))
        return _normalize_static_art_payload(payload)
    if source.suffix.lower() == ".pbm":
        return _static_art_from_pbm(source)
    return _static_art_from_pillow(source)


def _normalize_static_art_payload(payload: dict) -> dict:
    if payload.get("width") != 200 or payload.get("height") != 200:
        raise SystemExit("static art must be exactly 200x200")
    if payload.get("encoding") != "rows":
        raise SystemExit("static art encoding must be 'rows'")
    rows = payload.get("rows")
    if not isinstance(rows, list) or len(rows) != 200:
        raise SystemExit("static art rows must contain 200 strings")
    normalized_rows = []
    for row in rows:
        if not isinstance(row, str) or len(row) != 200:
            raise SystemExit("each static art row must be a 200-character string")
        normalized = row.replace("#", "1").replace(".", "0")
        if any(pixel not in "01" for pixel in normalized):
            raise SystemExit("static art rows may contain only 0/1 or ./# characters")
        normalized_rows.append(normalized)
    return {
        "width": 200,
        "height": 200,
        "encoding": "rows",
        "rows": normalized_rows,
    }


def _pbm_tokens(data: str) -> list[str]:
    tokens: list[str] = []
    for line in data.splitlines():
        content = line.split("#", 1)[0].strip()
        if content:
            tokens.extend(content.split())
    return tokens


def _static_art_from_pbm(path: Path) -> dict:
    tokens = _pbm_tokens(path.read_text(encoding="ascii"))
    if len(tokens) < 3 or tokens[0] != "P1":
        raise SystemExit("static art PBM must use ASCII P1 format")
    width = int(tokens[1])
    height = int(tokens[2])
    if width != 200 or height != 200:
        raise SystemExit("static art PBM must be exactly 200x200")
    pixels = tokens[3:]
    if len(pixels) != 200 * 200:
        raise SystemExit("static art PBM must contain exactly 40000 pixels")
    if any(pixel not in {"0", "1"} for pixel in pixels):
        raise SystemExit("static art PBM pixels must be 0 or 1")
    rows = ["".join(pixels[y * 200:(y + 1) * 200]) for y in range(200)]
    return {"width": 200, "height": 200, "encoding": "rows", "rows": rows}


def _static_art_from_pillow(path: Path) -> dict:
    try:
        from PIL import Image  # type: ignore
    except ImportError as exc:
        raise SystemExit("install Pillow to ingest this raster format, or provide .json/.pbm") from exc

    with Image.open(path) as image:
        image = image.convert("L").resize((200, 200))
        rows = []
        for y in range(200):
            row = []
            for x in range(200):
                row.append("1" if image.getpixel((x, y)) < 128 else "0")
            rows.append("".join(row))
    return {"width": 200, "height": 200, "encoding": "rows", "rows": rows}


def cmd_static_art_get(args: argparse.Namespace) -> int:
    session = _lan_session_from_args(args)
    session.require("static_art.read")
    _print_json(session.envelope(session.client.get_static_art()))  # type: ignore[union-attr]
    return 0


def cmd_static_art_set(args: argparse.Namespace) -> int:
    art = _load_static_art(args.file)
    session = _lan_session_from_args(args)
    session.require("static_art.write")
    response = session.client.put_static_art(art)  # type: ignore[union-attr]
    _print_json(session.envelope(response or {"updated": {"width": 200, "height": 200, "encoding": "rows"}}))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="pj")
    sub = parser.add_subparsers(dest="command", required=True)

    provision = sub.add_parser("provision", help="provision Wi-Fi over USB-C (default) or BLE")
    provision.add_argument("--ssid", required=True)
    provision.add_argument("--password", required=True)
    transport = provision.add_mutually_exclusive_group()
    transport.add_argument("--ble", action="store_true", help="use BLE instead of the default USB-C transport")
    transport.add_argument("--serial-port", metavar="PORT", help="USB-C serial port; auto-detected when omitted")
    provision.add_argument("--ble-name", help="BLE advertising name; requires --ble")
    provision.add_argument("--base-url")
    provision.add_argument("--data-dir")
    provision.add_argument("--serial-baud", type=int, default=115200)
    provision.add_argument("--timeout", type=float, default=6.0)
    provision.add_argument("--mock", action="store_true", help="simulate BLE provisioning; requires --ble")
    provision.set_defaults(func=cmd_provision)

    discover = sub.add_parser("discover", help="discover Pocket Journal devices on LAN")
    discover.set_defaults(func=cmd_discover)

    device = sub.add_parser("device", help="device maintenance commands")
    device_sub = device.add_subparsers(dest="device_command", required=True)
    device_status = device_sub.add_parser("status", help="read device status over USB-C or LAN/Wi-Fi")
    device_status.add_argument("--device", help="paired device id for Wi-Fi HTTP; optional for USB-C")
    device_status.add_argument("--base-url")
    device_status.add_argument("--token", help="override the stored LAN bearer token")
    device_status.add_argument("--data-dir")
    device_status.add_argument("--serial-port", help="USB-C serial port; auto-detected when omitted")
    device_status.add_argument("--serial-baud", type=int, default=115200)
    device_status.add_argument("--timeout", type=float, default=6.0)
    device_status.set_defaults(func=cmd_device_status)
    device_sync_time = device_sub.add_parser("sync-time", help="set device time from this computer")
    device_sync_time.add_argument("--device", help="paired device id for Wi-Fi HTTP; optional for USB-C")
    device_sync_time.add_argument("--base-url")
    device_sync_time.add_argument("--token", help="override the stored LAN bearer token")
    device_sync_time.add_argument("--data-dir")
    device_sync_time.add_argument("--serial-port", help="USB-C serial port, such as /dev/cu.usbmodem1101; auto-detected when omitted")
    device_sync_time.add_argument("--serial-baud", type=int, default=115200)
    device_sync_time.add_argument("--timeout", type=float, default=6.0)
    device_sync_time.set_defaults(func=cmd_device_sync_time)
    device_tone = device_sub.add_parser("tone", help="play a USB-C audio diagnostic tone")
    device_tone.add_argument("--device", help="optional label for output")
    device_tone.add_argument("--serial-port", help="USB-C serial port, such as /dev/cu.usbmodem1101; auto-detected when omitted")
    device_tone.add_argument("--serial-baud", type=int, default=115200)
    device_tone.add_argument("--timeout", type=float, default=6.0)
    device_tone.add_argument("--pa-level", type=int, choices=[0, 1], help="force speaker PA GPIO level for diagnosis")
    device_tone.add_argument("--dout-gpio", type=int, help="temporarily route I2S TX data to this GPIO for diagnosis")
    device_tone.add_argument("--audio-power-level", type=int, choices=[0, 1], help="force audio power GPIO level for diagnosis")
    device_tone.add_argument("--codec-gpio44", type=lambda value: int(value, 0), help="temporarily write ES8311 register 0x44 before the tone")
    device_tone.add_argument("--codec-gp45", type=lambda value: int(value, 0), help="temporarily write ES8311 register 0x45 before the tone")
    device_tone.set_defaults(func=cmd_device_tone)
    device_mic_check = device_sub.add_parser("mic-check", help="sample the ES8311 microphone path and report levels")
    device_mic_check.add_argument("--device", help="optional label for output")
    device_mic_check.add_argument("--serial-port", help="USB-C serial port, such as /dev/cu.usbmodem1101; auto-detected when omitted")
    device_mic_check.add_argument("--serial-baud", type=int, default=115200)
    device_mic_check.add_argument("--timeout", type=float, default=8.0)
    device_mic_check.add_argument("--duration-ms", type=int, default=1500)
    device_mic_check.add_argument("--gain-db", type=int, help="temporarily set ES8311 input gain for this check, 0..42 dB")
    device_mic_check.set_defaults(func=cmd_device_mic_check)

    firmware = sub.add_parser("firmware", help="firmware update commands")
    firmware_sub = firmware.add_subparsers(dest="firmware_command", required=True)
    firmware_status = firmware_sub.add_parser("status", help="read OTA status over LAN/Wi-Fi")
    firmware_status.add_argument("--device", required=True)
    firmware_status.add_argument("--base-url")
    firmware_status.add_argument("--token", help="override the stored LAN bearer token")
    firmware_status.add_argument("--data-dir")
    firmware_status.set_defaults(func=cmd_firmware_status)
    firmware_update = firmware_sub.add_parser("update", help="upload and activate a firmware image")
    firmware_update.add_argument("--device", required=True)
    firmware_update.add_argument("--base-url")
    firmware_update.add_argument("--token", help="override the stored LAN bearer token")
    firmware_update.add_argument("--data-dir")
    firmware_update.add_argument("--file", required=True)
    firmware_update.add_argument("--yes", action="store_true", help="confirm firmware activation")
    firmware_update.add_argument("--reconnect-timeout", type=float, default=90.0)
    firmware_update.set_defaults(func=cmd_firmware_update)

    sync = sub.add_parser("sync", help="download audio, transcribe, and upload transcripts")
    sync.add_argument("--device", required=True)
    sync.add_argument("--base-url")
    sync.add_argument("--token", help="override the stored LAN bearer token")
    sync.add_argument("--backend", choices=["fake", "hf"], default="hf")
    sync.add_argument("--reprocess", action="store_true", help="reprocess notes already marked synced")
    sync.add_argument("--data-dir")
    sync.set_defaults(func=cmd_sync)

    calendar = sub.add_parser("calendar", help="calendar commands")
    calendar_sub = calendar.add_subparsers(dest="calendar_command", required=True)
    calendar_sync = calendar_sub.add_parser("sync", help="sync today's calendar")
    calendar_sync.add_argument("--device", required=True)
    calendar_sync.add_argument("--base-url")
    calendar_sync.add_argument("--token", help="override the stored LAN bearer token")
    calendar_sync.add_argument("--data-dir")
    calendar_sync.add_argument("--fixture", help="JSON fixture of Google event objects")
    calendar_sync.set_defaults(func=cmd_calendar_sync)

    settings = sub.add_parser("settings", help="device settings")
    settings_sub = settings.add_subparsers(dest="settings_command", required=True)
    settings_get = settings_sub.add_parser("get")
    settings_get.add_argument("--device", required=True)
    settings_get.add_argument("--base-url")
    settings_get.add_argument("--token", help="override the stored LAN bearer token")
    settings_get.add_argument("--data-dir")
    settings_get.set_defaults(func=cmd_settings_get)
    settings_set = settings_sub.add_parser("set")
    settings_set.add_argument("--device", required=True)
    settings_set.add_argument("--base-url")
    settings_set.add_argument("--token", help="override the stored LAN bearer token")
    settings_set.add_argument("--data-dir")
    settings_set.add_argument("assignments", nargs="+")
    settings_set.set_defaults(func=cmd_settings_set)

    recordings = sub.add_parser("recordings", help="recording maintenance commands")
    recordings_sub = recordings.add_subparsers(dest="recordings_command", required=True)
    recordings_list = recordings_sub.add_parser("list", help="list retained recordings over LAN/Wi-Fi")
    recordings_list.add_argument("--device", required=True)
    recordings_list.add_argument("--base-url")
    recordings_list.add_argument("--token", help="override the stored LAN bearer token")
    recordings_list.add_argument("--data-dir")
    recordings_list.set_defaults(func=cmd_recordings_list)
    recordings_download = recordings_sub.add_parser("download", help="download one recording over LAN/Wi-Fi")
    recordings_download.add_argument("--device", required=True)
    recordings_download.add_argument("--base-url")
    recordings_download.add_argument("--token", help="override the stored LAN bearer token")
    recordings_download.add_argument("--data-dir")
    recordings_download.add_argument("--audio-id", required=True)
    recordings_download.add_argument("--output-dir")
    recordings_download.set_defaults(func=cmd_recordings_download)
    recordings_wipe = recordings_sub.add_parser("wipe", help="delete all recordings from the device")
    recordings_wipe.add_argument("--device", help="paired device id for Wi-Fi HTTP; optional for USB-C")
    recordings_wipe.add_argument("--base-url")
    recordings_wipe.add_argument("--token", help="override the stored LAN bearer token")
    recordings_wipe.add_argument("--data-dir")
    recordings_wipe.add_argument("--serial-port", help="USB-C serial port, such as /dev/cu.usbmodem1101; auto-detected when omitted")
    recordings_wipe.add_argument("--serial-baud", type=int, default=115200)
    recordings_wipe.add_argument("--timeout", type=float, default=6.0)
    recordings_wipe.add_argument("--yes", action="store_true", help="confirm deletion of all device recordings")
    recordings_wipe.set_defaults(func=cmd_recordings_wipe)

    home = sub.add_parser("home", help="custom home screen design")
    home_sub = home.add_subparsers(dest="home_command", required=True)
    home_get = home_sub.add_parser("get")
    home_get.add_argument("--device", required=True)
    home_get.add_argument("--base-url")
    home_get.add_argument("--token", help="override the stored LAN bearer token")
    home_get.add_argument("--data-dir")
    home_get.set_defaults(func=cmd_home_get)
    home_set = home_sub.add_parser("set")
    home_set.add_argument("--device", required=True)
    home_set.add_argument("--base-url")
    home_set.add_argument("--token", help="override the stored LAN bearer token")
    home_set.add_argument("--data-dir")
    home_set.add_argument("--file", required=True, help="JSON file with title and up to five home slots")
    home_set.set_defaults(func=cmd_home_set)

    static_art = sub.add_parser("static-art", help="custom resting-screen bitmap art")
    static_art_sub = static_art.add_subparsers(dest="static_art_command", required=True)
    static_art_get = static_art_sub.add_parser("get")
    static_art_get.add_argument("--device", required=True)
    static_art_get.add_argument("--base-url")
    static_art_get.add_argument("--token", help="override the stored LAN bearer token")
    static_art_get.add_argument("--data-dir")
    static_art_get.set_defaults(func=cmd_static_art_get)
    static_art_set = static_art_sub.add_parser("set")
    static_art_set.add_argument("--device", required=True)
    static_art_set.add_argument("--base-url")
    static_art_set.add_argument("--token", help="override the stored LAN bearer token")
    static_art_set.add_argument("--data-dir")
    static_art_set.add_argument("--file", required=True, help="200x200 1-bit JSON bitmap")
    static_art_set.set_defaults(func=cmd_static_art_set)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except DeviceError as exc:
        print(f"pj: error: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    except BrokenPipeError:
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
