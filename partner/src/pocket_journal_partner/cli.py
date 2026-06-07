from __future__ import annotations

from datetime import date
from pathlib import Path
import argparse
import asyncio
import json
import sys

from .ble import provision_wifi
from .calendar import calendar_payload_for_day
from .config import DeviceProfile, load_config, save_config
from .device import DeviceClient, discover_mdns
from .storage import PartnerStore
from .sync import sync_device_audio
from .transcription import backend_from_name


def _print_json(payload) -> None:
    print(json.dumps(payload, indent=2, sort_keys=True))


def _device_profile(device_id: str, data_dir: str | None = None) -> DeviceProfile:
    config = load_config(Path(data_dir) if data_dir else None)
    try:
        return config.devices[device_id]
    except KeyError as exc:
        raise SystemExit(f"unknown device {device_id}; run pj provision or edit config") from exc


def cmd_provision(args: argparse.Namespace) -> int:
    provisioned = asyncio.run(provision_wifi(args.ble_name, args.ssid, args.password, mock=args.mock))
    data_dir = Path(args.data_dir) if args.data_dir else None
    config = load_config(data_dir)
    base_url = args.base_url or ""
    config.devices[provisioned.device_id] = DeviceProfile(
        device_id=provisioned.device_id,
        ble_name=provisioned.ble_name,
        token=provisioned.token,
        base_url=base_url,
    )
    save_config(config, data_dir)
    _print_json(config.devices[provisioned.device_id].__dict__)
    return 0


def cmd_discover(args: argparse.Namespace) -> int:
    _ = args
    _print_json({"devices": discover_mdns()})
    return 0


def _client_from_args(args: argparse.Namespace) -> tuple[str, DeviceClient]:
    profile = _device_profile(args.device, getattr(args, "data_dir", None))
    base_url = args.base_url or profile.base_url
    if not base_url:
        raise SystemExit("device base URL is required; pass --base-url or store it in config")
    return profile.device_id, DeviceClient(base_url, profile.token)


def cmd_sync(args: argparse.Namespace) -> int:
    device_id, client = _client_from_args(args)
    store = PartnerStore(Path(args.data_dir) if args.data_dir else None)
    backend = backend_from_name(args.backend)
    results = sync_device_audio(device_id, client, store, backend)
    _print_json({"synced": results})
    return 0


def cmd_calendar_sync(args: argparse.Namespace) -> int:
    device_id, client = _client_from_args(args)
    if args.fixture:
        events = json.loads(Path(args.fixture).read_text(encoding="utf-8"))
    else:
        from .calendar import fetch_google_events_for_day

        events = fetch_google_events_for_day(date.today())
    payload = calendar_payload_for_day(date.today(), events)
    client.upload_calendar_today(payload)
    _print_json({"device_id": device_id, "uploaded": len(payload["events"]), "date": payload["date"]})
    return 0


def cmd_settings_get(args: argparse.Namespace) -> int:
    _, client = _client_from_args(args)
    _print_json(client.get_settings())
    return 0


def cmd_settings_set(args: argparse.Namespace) -> int:
    _, client = _client_from_args(args)
    settings = {}
    for assignment in args.assignments:
        if "=" not in assignment:
            raise SystemExit(f"expected key=value assignment, got {assignment}")
        key, value = assignment.split("=", 1)
        settings[key] = value
    response = client.put_settings(settings)
    _print_json(response or {"updated": settings})
    return 0


def cmd_home_get(args: argparse.Namespace) -> int:
    _, client = _client_from_args(args)
    _print_json(client.get_home_design())
    return 0


def _load_home_design(path: str) -> dict:
    payload = json.loads(Path(path).read_text(encoding="utf-8"))
    slots = payload.get("slots")
    if not isinstance(slots, list) or not slots:
        raise SystemExit("home design JSON must include a non-empty slots array")
    normalized_slots = []
    for slot in slots[:5]:
        if not isinstance(slot, dict):
            raise SystemExit("each home slot must be an object")
        label = str(slot.get("label", "")).strip()
        icon = str(slot.get("icon", "")).strip()
        state = str(slot.get("state", "")).strip()
        if not label or not icon or not state:
            raise SystemExit("each home slot needs label, icon, and state")
        normalized_slots.append({"label": label, "icon": icon, "state": state})
    return {"title": str(payload.get("title", "Pocket Journal")), "slots": normalized_slots}


def cmd_home_set(args: argparse.Namespace) -> int:
    _, client = _client_from_args(args)
    design = _load_home_design(args.file)
    response = client.put_home_design(design)
    _print_json(response or {"updated": design})
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
    _, client = _client_from_args(args)
    _print_json(client.get_static_art())
    return 0


def cmd_static_art_set(args: argparse.Namespace) -> int:
    _, client = _client_from_args(args)
    art = _load_static_art(args.file)
    response = client.put_static_art(art)
    _print_json(response or {"updated": {"width": 200, "height": 200, "encoding": "rows"}})
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="pj")
    sub = parser.add_subparsers(dest="command", required=True)

    provision = sub.add_parser("provision", help="provision Wi-Fi over BLE")
    provision.add_argument("--ssid", required=True)
    provision.add_argument("--password", required=True)
    provision.add_argument("--ble-name")
    provision.add_argument("--base-url")
    provision.add_argument("--data-dir")
    provision.add_argument("--mock", action="store_true", help="store a mock provisioned profile before hardware is available")
    provision.set_defaults(func=cmd_provision)

    discover = sub.add_parser("discover", help="discover Pocket Journal devices on LAN")
    discover.set_defaults(func=cmd_discover)

    sync = sub.add_parser("sync", help="download audio, transcribe, and upload transcripts")
    sync.add_argument("--device", required=True)
    sync.add_argument("--base-url")
    sync.add_argument("--backend", choices=["fake", "hf"], default="hf")
    sync.add_argument("--data-dir")
    sync.set_defaults(func=cmd_sync)

    calendar = sub.add_parser("calendar", help="calendar commands")
    calendar_sub = calendar.add_subparsers(dest="calendar_command", required=True)
    calendar_sync = calendar_sub.add_parser("sync", help="sync today's calendar")
    calendar_sync.add_argument("--device", required=True)
    calendar_sync.add_argument("--base-url")
    calendar_sync.add_argument("--data-dir")
    calendar_sync.add_argument("--fixture", help="JSON fixture of Google event objects")
    calendar_sync.set_defaults(func=cmd_calendar_sync)

    settings = sub.add_parser("settings", help="device settings")
    settings_sub = settings.add_subparsers(dest="settings_command", required=True)
    settings_get = settings_sub.add_parser("get")
    settings_get.add_argument("--device", required=True)
    settings_get.add_argument("--base-url")
    settings_get.add_argument("--data-dir")
    settings_get.set_defaults(func=cmd_settings_get)
    settings_set = settings_sub.add_parser("set")
    settings_set.add_argument("--device", required=True)
    settings_set.add_argument("--base-url")
    settings_set.add_argument("--data-dir")
    settings_set.add_argument("assignments", nargs="+")
    settings_set.set_defaults(func=cmd_settings_set)

    home = sub.add_parser("home", help="custom home screen design")
    home_sub = home.add_subparsers(dest="home_command", required=True)
    home_get = home_sub.add_parser("get")
    home_get.add_argument("--device", required=True)
    home_get.add_argument("--base-url")
    home_get.add_argument("--data-dir")
    home_get.set_defaults(func=cmd_home_get)
    home_set = home_sub.add_parser("set")
    home_set.add_argument("--device", required=True)
    home_set.add_argument("--base-url")
    home_set.add_argument("--data-dir")
    home_set.add_argument("--file", required=True, help="JSON file with title and up to five home slots")
    home_set.set_defaults(func=cmd_home_set)

    static_art = sub.add_parser("static-art", help="custom resting-screen bitmap art")
    static_art_sub = static_art.add_subparsers(dest="static_art_command", required=True)
    static_art_get = static_art_sub.add_parser("get")
    static_art_get.add_argument("--device", required=True)
    static_art_get.add_argument("--base-url")
    static_art_get.add_argument("--data-dir")
    static_art_get.set_defaults(func=cmd_static_art_get)
    static_art_set = static_art_sub.add_parser("set")
    static_art_set.add_argument("--device", required=True)
    static_art_set.add_argument("--base-url")
    static_art_set.add_argument("--data-dir")
    static_art_set.add_argument("--file", required=True, help="200x200 1-bit JSON bitmap")
    static_art_set.set_defaults(func=cmd_static_art_set)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except KeyboardInterrupt:
        return 130
    except BrokenPipeError:
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
