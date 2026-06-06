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
