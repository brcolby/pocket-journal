from __future__ import annotations

from datetime import datetime, timezone
from pathlib import Path
from typing import Any
import argparse
import asyncio
import json
import secrets
import sqlite3
import sys
import webbrowser

from .ble import provision_wifi
from .config import DeviceProfile, load_config, save_config
from .device import (
    DeviceClient,
    DeviceError,
    SerialDeviceClient,
    SerialPortNotFound,
    discover_mdns,
    discover_serial_ports,
    resolve_serial_port,
)
from .diagnostics import credential_safe_status, wifi_diagnostics
from .library import LibraryNote, NoteLibrary
from .operations import DeviceSession
from .ota import inspect_firmware_image, ota_preflight, ota_status, stream_firmware_image, wait_for_ota_result
from .storage import PartnerStore
from .sync import sync_device_audio
from .transcription import FakeTranscriptionBackend, WhisperCppTranscriptionBackend, backend_from_name
from .transcription_benchmark import (
    BenchmarkManifestError,
    benchmark_manifest,
    write_benchmark_report,
)
from .tui import run_tui
from .web import DEFAULT_HOST, DEFAULT_PORT, create_server


USB_PROVISIONING_TOKEN_BYTES = 16
TIME_SYNC_PRECISION_SECONDS = 60
TIME_SYNC_RETRY_COMMAND = "pj device sync-time"


def _print_json(payload) -> None:
    print(json.dumps(payload, indent=2, sort_keys=True))


def _sync_time_from_host(
    client: DeviceClient | SerialDeviceClient,
    now: datetime | None = None,
) -> dict[str, Any]:
    local_now = now or datetime.now().astimezone()
    local_now = local_now.replace(second=0, microsecond=0)
    offset = local_now.utcoffset() if local_now.tzinfo is not None else None
    if offset is None:
        raise DeviceError("host time must include a UTC offset")
    offset_seconds = offset.total_seconds()
    if offset_seconds % 60 != 0:
        raise DeviceError("host UTC offset must resolve to whole minutes")
    expected = {
        "hour": local_now.hour,
        "minute": local_now.minute,
        "year": local_now.year,
        "month": local_now.month,
        "day": local_now.day,
    }
    response = client.put_time(
        expected["hour"],
        expected["minute"],
        expected["month"],
        expected["day"],
        expected["year"],
        int(offset_seconds // 60),
    )
    if (
        not isinstance(response, dict)
        or any(response.get(key) != value for key, value in expected.items())
        or (
            "utc_offset_minutes" in response
            and response.get("utc_offset_minutes") != int(offset_seconds // 60)
        )
    ):
        raise DeviceError(f"device time sync could not be validated; run '{TIME_SYNC_RETRY_COMMAND}' to retry")
    return {
        "state": "synced",
        "source": "host",
        "validated": True,
        "precision_seconds": TIME_SYNC_PRECISION_SECONDS,
        "host_local": local_now.isoformat(),
        "host_utc": local_now.astimezone(timezone.utc).isoformat().replace("+00:00", "Z"),
        "utc_offset_minutes": int(offset_seconds // 60),
        "device_local_time": expected,
    }


def cmd_provision(args: argparse.Namespace) -> int:
    data_dir = Path(args.data_dir) if args.data_dir else None
    base_url = args.base_url or ""
    if args.ble_name and not args.ble:
        raise DeviceError("--ble-name requires --ble")
    if args.mock and not args.ble:
        raise DeviceError("--mock requires --ble")

    serial_client: SerialDeviceClient | None = None
    if not args.ble:
        # Hex encoding doubles this value on the legacy serial protocol. 128 bits
        # keeps ordinary provisioning commands below small USB console boundaries.
        token = secrets.token_urlsafe(USB_PROVISIONING_TOKEN_BYTES)
        serial_client = SerialDeviceClient(
            resolve_serial_port(args.serial_port),
            baudrate=args.serial_baud,
            timeout=args.timeout,
        )
        response = serial_client.provision_wifi(args.ssid, args.password, token)
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

    if serial_client is None:
        time_sync: dict[str, Any] = {
            "state": "deferred",
            "reason": "BLE provisioning does not expose a time-write operation; connect over USB-C or LAN",
            "retryable": True,
            "retry_command": TIME_SYNC_RETRY_COMMAND,
        }
    else:
        try:
            time_sync = _sync_time_from_host(serial_client)
        except DeviceError as exc:
            # Provisioning has already rotated the bearer token. Preserve the
            # paired profile and report time failure independently so it can be
            # retried without re-entering credentials.
            time_sync = {
                "state": "failed",
                "error": str(exc),
                "retryable": True,
                "retry_command": TIME_SYNC_RETRY_COMMAND,
            }
    _print_json({
        "device_id": profile.device_id,
        "ble_name": profile.ble_name,
        "base_url": profile.base_url,
        "provisioned": True,
        "time_sync": time_sync,
    })
    return 0


def cmd_discover(args: argparse.Namespace) -> int:
    _ = args
    lan = [{**device, "transport": "lan"} for device in discover_mdns()]
    usb_ports = discover_serial_ports()
    usb = [{"transport": "usb", "port": port} for port in usb_ports]
    _print_json({
        "devices": usb + lan,
        "count": len(usb) + len(lan),
        "usb_ports": usb_ports,
        "lan_devices": lan,
    })
    return 0


def _discovered_lan_devices() -> list[dict[str, str]]:
    return discover_mdns()


def _client_from_args(args: argparse.Namespace) -> tuple[str, DeviceClient]:
    data_dir = Path(args.data_dir) if getattr(args, "data_dir", None) else None
    config = load_config(data_dir)
    requested_id = getattr(args, "device", None)
    base_url_override = getattr(args, "base_url", None) or ""
    token_override = getattr(args, "token", None) or ""

    if requested_id:
        profile = config.devices.get(requested_id)
        base_url = base_url_override or (profile.base_url if profile else "")
        if not base_url:
            matches = [
                device for device in _discovered_lan_devices()
                if device.get("device_id") == requested_id and device.get("base_url")
            ]
            if len(matches) == 1:
                base_url = matches[0]["base_url"]
            elif len(matches) > 1:
                raise DeviceError(
                    f"multiple LAN endpoints found for {requested_id}; pass --base-url"
                )
        token = token_override or (profile.token if profile else "")
        if not base_url:
            raise DeviceError(
                f"no LAN endpoint found for {requested_id}; connect it to Wi-Fi or pass --base-url"
            )
        if not token:
            raise DeviceError(
                f"no bearer token stored for {requested_id}; provision it or pass --token"
            )
        return requested_id, DeviceClient(base_url, token)

    profiles = list(config.devices.values())
    if base_url_override:
        matching_profiles = [
            profile for profile in profiles
            if profile.base_url.rstrip("/") == base_url_override.rstrip("/")
        ]
        profile = matching_profiles[0] if len(matching_profiles) == 1 else None
        if profile is None and len(profiles) == 1:
            profile = profiles[0]
        token = token_override or (profile.token if profile else "")
        if not token:
            raise DeviceError("a bearer token is required with --base-url; pass --token or provision the device")
        return profile.device_id if profile else "explicit", DeviceClient(base_url_override, token)

    complete_profiles = [profile for profile in profiles if profile.base_url and profile.token]
    if len(complete_profiles) == 1:
        profile = complete_profiles[0]
        return profile.device_id, DeviceClient(profile.base_url, token_override or profile.token)

    discovered = _discovered_lan_devices()
    candidates: dict[str, tuple[str, str]] = {}
    for device in discovered:
        device_id = device.get("device_id", "")
        base_url = device.get("base_url", "")
        profile = config.devices.get(device_id)
        token = token_override or (profile.token if profile else "")
        if device_id and base_url and token:
            candidates[device_id] = (base_url, token)

    if len(candidates) == 1:
        device_id, (base_url, token) = next(iter(sorted(candidates.items())))
        return device_id, DeviceClient(base_url, token)
    if len(candidates) > 1:
        raise DeviceError(
            "multiple paired Pocket Journals are reachable; pass --device: "
            + ", ".join(sorted(candidates))
        )

    discovered_ids = sorted({device.get("device_id", "") for device in discovered if device.get("device_id")})
    if len(discovered_ids) == 1:
        raise DeviceError(
            f"found {discovered_ids[0]} on LAN but no bearer token is available; provision it or pass --token"
        )
    if len(discovered_ids) > 1:
        raise DeviceError(
            "multiple Pocket Journals found on LAN; pass --device: " + ", ".join(discovered_ids)
        )
    if len(complete_profiles) > 1:
        raise DeviceError(
            "multiple paired Pocket Journals are configured; pass --device: "
            + ", ".join(sorted(profile.device_id for profile in complete_profiles))
        )
    if len(profiles) == 1:
        raise DeviceError(
            f"paired device {profiles[0].device_id} is not discoverable on LAN; connect it to Wi-Fi or pass --base-url"
        )
    raise DeviceError("no paired or discoverable Pocket Journal found; provision a device or pass --base-url and --token")


def _control_client_from_args(args: argparse.Namespace):
    transport = getattr(args, "transport", "auto")
    serial_port = getattr(args, "serial_port", None)
    network_requested = any(getattr(args, name, None) for name in ("device", "base_url", "token"))
    if transport == "lan":
        if serial_port:
            raise DeviceError("--serial-port cannot be used with --transport lan")
        return _client_from_args(args)
    if transport == "usb" and network_requested:
        raise DeviceError("--device, --base-url, and --token cannot be used with --transport usb")
    if serial_port:
        resolved_port = resolve_serial_port(serial_port)
        return "usb", SerialDeviceClient(
            resolved_port,
            baudrate=getattr(args, "serial_baud", 115200),
            timeout=getattr(args, "timeout", 6.0),
        )
    if network_requested:
        return _client_from_args(args)
    try:
        resolved_port = resolve_serial_port()
    except SerialPortNotFound:
        if transport == "usb":
            raise
        return _client_from_args(args)
    return "usb", SerialDeviceClient(
        resolved_port,
        baudrate=getattr(args, "serial_baud", 115200),
        timeout=getattr(args, "timeout", 6.0),
    )


def _session_from_args(args: argparse.Namespace) -> DeviceSession:
    device_id, client = _control_client_from_args(args)
    return DeviceSession(device_id, client)


def _lan_session_from_args(args: argparse.Namespace) -> DeviceSession:
    device_id, client = _client_from_args(args)
    return DeviceSession(device_id, client)


def _sync_session_from_args(args: argparse.Namespace) -> DeviceSession:
    if getattr(args, "transport", "auto") == "lan":
        return _lan_session_from_args(args)
    session = _session_from_args(args)
    if isinstance(session.client, SerialDeviceClient):
        status = session.status()
        device_id = status.get("device_id")
        if not isinstance(device_id, str) or not device_id or len(device_id) > 160:
            raise DeviceError("USB status did not provide a stable device id for local sync")
        return DeviceSession(device_id, session.client)
    return session


def cmd_sync(args: argparse.Namespace) -> int:
    store = PartnerStore(Path(args.data_dir) if args.data_dir else None)
    try:
        backend = backend_from_name(
            args.backend,
            model_path=getattr(args, "model", None),
            executable=getattr(args, "whisper_executable", None),
            threads=getattr(args, "threads", None),
        )
    except ValueError as exc:
        raise DeviceError(str(exc)) from exc
    if isinstance(backend, WhisperCppTranscriptionBackend):
        availability = backend.availability()
        if not availability["available"]:
            raise DeviceError("; ".join(availability["issues"]))
    session = _sync_session_from_args(args)
    session.require("audio.sync")
    upload_transcripts = not isinstance(backend, FakeTranscriptionBackend)
    try:
        results = sync_device_audio(
            session.device_id,
            session.client,
            store,
            backend,  # type: ignore[arg-type]
            upload_transcripts=upload_transcripts,
            reprocess_synced=getattr(args, "reprocess", False),
        )
    except RuntimeError as exc:
        raise DeviceError(str(exc)) from exc
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


def _library_from_args(args: argparse.Namespace) -> NoteLibrary:
    store = PartnerStore(Path(args.data_dir) if getattr(args, "data_dir", None) else None)
    try:
        library = NoteLibrary(store.root)
        library.import_partner_store(store)
    except (OSError, RuntimeError, sqlite3.Error) as exc:
        raise DeviceError(f"local note library is unavailable: {exc}") from exc
    return library


def _library_note_payload(note: LibraryNote, *, include_transcript: bool) -> dict[str, Any]:
    payload = note.as_dict()
    if not include_transcript:
        payload.pop("transcript", None)
    return payload


def cmd_library_list(args: argparse.Namespace) -> int:
    library = _library_from_args(args)
    try:
        notes = library.list_notes(limit=args.limit, offset=args.offset, search=args.search)
        total = library.count(search=args.search)
    except (ValueError, sqlite3.Error) as exc:
        raise DeviceError(str(exc)) from exc
    _print_json({
        "schema_version": library.schema_version(),
        "notes": [_library_note_payload(note, include_transcript=False) for note in notes],
        "count": len(notes),
        "total": total,
        "offset": args.offset,
    })
    return 0


def cmd_library_show(args: argparse.Namespace) -> int:
    note = _library_from_args(args).get(args.note_id)
    if note is None:
        raise DeviceError(f"local note not found: {args.note_id}")
    _print_json(_library_note_payload(note, include_transcript=True))
    return 0


def cmd_library_title(args: argparse.Namespace) -> int:
    library = _library_from_args(args)
    try:
        note = library.update_title(args.note_id, args.title)
    except KeyError as exc:
        raise DeviceError(f"local note not found: {args.note_id}") from exc
    except (OSError, ValueError, sqlite3.Error) as exc:
        raise DeviceError(str(exc)) from exc
    _print_json(_library_note_payload(note, include_transcript=False))
    return 0


def cmd_library_tui(args: argparse.Namespace) -> int:
    return run_tui(_library_from_args(args))


def cmd_library_serve(args: argparse.Namespace) -> int:
    try:
        server = create_server(_library_from_args(args), args.host, args.port)
    except (OSError, ValueError) as exc:
        raise DeviceError(str(exc)) from exc
    host, port = server.server_address[:2]
    url_host = f"[{host}]" if ":" in host else host
    url = f"http://{url_host}:{port}/"
    print(f"Pocket Journal library: {url}", file=sys.stderr)
    if not args.no_open:
        webbrowser.open(url)
    try:
        server.serve_forever(poll_interval=0.25)
    finally:
        server.server_close()
    return 0


def cmd_transcription_status(args: argparse.Namespace) -> int:
    try:
        backend = WhisperCppTranscriptionBackend(
            model_path=args.model,
            executable=args.whisper_executable,
            threads=args.threads,
        )
    except ValueError as exc:
        raise DeviceError(str(exc)) from exc
    status = backend.availability(digest=args.digest)
    _print_json(status)
    return 0 if status["available"] else 1


def cmd_transcription_benchmark(args: argparse.Namespace) -> int:
    try:
        backend = WhisperCppTranscriptionBackend(
            model_path=args.model,
            executable=args.whisper_executable,
            threads=args.threads,
        )
        availability = backend.availability()
        if not availability["available"]:
            raise DeviceError("; ".join(availability["issues"]))
        report = benchmark_manifest(
            Path(args.manifest),
            backend,
            runs=args.runs,
            timeout_seconds=args.timeout,
            runtime_root=Path(args.runtime_root) if args.runtime_root else None,
            provenance={
                "runtime": {
                    "source": args.runtime_source,
                    "license": args.runtime_license,
                },
                "model": {
                    "source": args.model_source,
                    "license": args.model_license,
                },
            },
        )
        if args.output:
            write_benchmark_report(Path(args.output), report)
    except (BenchmarkManifestError, OSError, ValueError, RuntimeError) as exc:
        raise DeviceError(f"transcription benchmark failed: {exc}") from exc
    _print_json(report)
    return 0 if report["passed"] else 1


def cmd_settings_get(args: argparse.Namespace) -> int:
    session = _session_from_args(args)
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
    session = _session_from_args(args)
    session.require("settings.write")
    response = session.client.put_settings(settings)  # type: ignore[union-attr]
    _print_json(session.envelope(response or {"updated": settings}))
    return 0


def cmd_device_sync_time(args: argparse.Namespace) -> int:
    session = _session_from_args(args)
    session.require("time.write")
    _print_json(session.envelope(_sync_time_from_host(session.client)))
    return 0


def cmd_device_status(args: argparse.Namespace) -> int:
    session = _session_from_args(args)
    status = credential_safe_status(session.status())
    if isinstance(status, dict):
        status.pop("wifi_diagnostics", None)
        status.pop("time_sync", None)
    _print_json(session.envelope(status))
    return 0


def cmd_device_stop_interval(args: argparse.Namespace) -> int:
    resolved_port = resolve_serial_port(args.serial_port)
    client = SerialDeviceClient(
        resolved_port,
        baudrate=args.serial_baud,
        timeout=args.timeout,
    )
    _print_json({
        "device_id": "usb",
        "transport": "usb",
        "result": client.reset_interval(),
    })
    return 0


def cmd_device_wifi_diagnostics(args: argparse.Namespace) -> int:
    session = _session_from_args(args)
    _print_json(session.envelope(wifi_diagnostics(session.status())))
    return 0


def cmd_device_usb_recover(args: argparse.Namespace) -> int:
    port = resolve_serial_port(args.serial_port)
    client = SerialDeviceClient(port, baudrate=args.serial_baud, timeout=args.timeout)
    result = credential_safe_status(client.recover_usb(probe_only=args.probe_only))
    _print_json({"device_id": "usb", "transport": "usb", "result": result})
    return 0 if result.get("final_state") == "application" else 1


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
    session = _session_from_args(args)
    items = [_audio_item_payload(item) for item in session.list_recordings()]
    _print_json(session.envelope({"recordings": items, "count": len(items)}))
    return 0


def cmd_recordings_download(args: argparse.Namespace) -> int:
    session = _session_from_args(args)
    items = session.list_recordings()
    item = next((candidate for candidate in items if candidate.audio_id == args.audio_id), None)
    if item is None:
        raise DeviceError(f"recording not found: {args.audio_id}")
    session.require("recordings.download")
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


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="pj")
    sub = parser.add_subparsers(
        dest="command",
        required=True,
        metavar="{provision,discover,device,firmware,sync,library,transcription,settings,recordings}",
    )

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

    discover = sub.add_parser("discover", help="discover Pocket Journal devices over USB-C and LAN")
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
    device_status.add_argument("--transport", choices=["auto", "usb", "lan"], default="auto")
    device_status.set_defaults(func=cmd_device_status)
    device_stop_interval = device_sub.add_parser(
        "stop-interval",
        help="silence and persistently reset a running interval over USB-C",
    )
    device_stop_interval.add_argument(
        "--serial-port",
        help="USB-C serial port, such as /dev/cu.usbmodem1101; auto-detected when omitted",
    )
    device_stop_interval.add_argument("--serial-baud", type=int, default=115200)
    device_stop_interval.add_argument("--timeout", type=float, default=6.0)
    device_stop_interval.set_defaults(func=cmd_device_stop_interval)
    device_wifi = device_sub.add_parser(
        "wifi-diagnostics",
        help="explain Wi-Fi connection state without exposing credentials",
    )
    device_wifi.add_argument("--device", help="paired device id for Wi-Fi HTTP; optional for USB-C")
    device_wifi.add_argument("--base-url")
    device_wifi.add_argument("--token", help="override the stored LAN bearer token")
    device_wifi.add_argument("--data-dir")
    device_wifi.add_argument("--serial-port", help="USB-C serial port; auto-detected when omitted")
    device_wifi.add_argument("--serial-baud", type=int, default=115200)
    device_wifi.add_argument("--timeout", type=float, default=6.0)
    device_wifi.add_argument("--transport", choices=["auto", "usb", "lan"], default="auto")
    device_wifi.set_defaults(func=cmd_device_wifi_diagnostics)
    device_sync_time = device_sub.add_parser("sync-time", help="set device time from this computer")
    device_sync_time.add_argument("--device", help="paired device id for Wi-Fi HTTP; optional for USB-C")
    device_sync_time.add_argument("--base-url")
    device_sync_time.add_argument("--token", help="override the stored LAN bearer token")
    device_sync_time.add_argument("--data-dir")
    device_sync_time.add_argument("--serial-port", help="USB-C serial port, such as /dev/cu.usbmodem1101; auto-detected when omitted")
    device_sync_time.add_argument("--serial-baud", type=int, default=115200)
    device_sync_time.add_argument("--timeout", type=float, default=6.0)
    device_sync_time.add_argument("--transport", choices=["auto", "usb", "lan"], default="auto")
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
    device_usb_recover = device_sub.add_parser(
        "usb-recover",
        help="diagnose USB-C state and safely reset an ESP32-S3 out of ROM download mode",
    )
    device_usb_recover.add_argument(
        "--serial-port",
        help="USB-C serial port, such as /dev/cu.usbmodem1101; auto-detected when omitted",
    )
    device_usb_recover.add_argument("--serial-baud", type=int, default=115200)
    device_usb_recover.add_argument(
        "--timeout",
        type=float,
        default=8.0,
        help="maximum probe/reconnect wait in seconds",
    )
    device_usb_recover.add_argument(
        "--probe-only",
        action="store_true",
        help="report application, ROM, or unresponsive state without resetting",
    )
    device_usb_recover.set_defaults(func=cmd_device_usb_recover)

    firmware = sub.add_parser("firmware", help="firmware update commands")
    firmware_sub = firmware.add_subparsers(dest="firmware_command", required=True)
    firmware_status = firmware_sub.add_parser("status", help="read OTA status over LAN/Wi-Fi")
    firmware_status.add_argument("--device", help="paired device id; auto-selected when unambiguous")
    firmware_status.add_argument("--base-url")
    firmware_status.add_argument("--token", help="override the stored LAN bearer token")
    firmware_status.add_argument("--data-dir")
    firmware_status.set_defaults(func=cmd_firmware_status)
    firmware_update = firmware_sub.add_parser("update", help="upload and activate a firmware image")
    firmware_update.add_argument("--device", help="paired device id; auto-selected when unambiguous")
    firmware_update.add_argument("--base-url")
    firmware_update.add_argument("--token", help="override the stored LAN bearer token")
    firmware_update.add_argument("--data-dir")
    firmware_update.add_argument("--file", required=True)
    firmware_update.add_argument("--yes", action="store_true", help="confirm firmware activation")
    firmware_update.add_argument("--reconnect-timeout", type=float, default=90.0)
    firmware_update.set_defaults(func=cmd_firmware_update)

    sync = sub.add_parser("sync", help="download audio, transcribe, and upload transcripts")
    sync.add_argument("--device", help="paired device id; auto-selected when unambiguous")
    sync.add_argument("--base-url")
    sync.add_argument("--token", help="override the stored LAN bearer token")
    sync.add_argument("--serial-port", help="USB-C serial port; auto-detected when omitted")
    sync.add_argument("--serial-baud", type=int, default=115200)
    sync.add_argument("--timeout", type=float, default=20.0)
    sync.add_argument(
        "--transport",
        choices=["auto", "lan", "usb"],
        default="auto",
        help="sync transport; auto prefers an unambiguous USB-C device and falls back to LAN",
    )
    sync.add_argument(
        "--backend",
        choices=["whisper-cpp", "hf", "fake"],
        default="whisper-cpp",
        help="local transcription runtime (default: whisper-cpp)",
    )
    sync.add_argument("--model", help="local whisper.cpp GGML/GGUF model path; or set PJ_WHISPER_MODEL")
    sync.add_argument(
        "--whisper-executable",
        help="whisper.cpp CLI executable; or set PJ_WHISPER_CPP",
    )
    sync.add_argument("--threads", type=int, help="CPU threads for whisper.cpp")
    sync.add_argument("--reprocess", action="store_true", help="reprocess notes already marked synced")
    sync.add_argument("--data-dir")
    sync.set_defaults(func=cmd_sync)

    library = sub.add_parser("library", help="browse and edit the local audio and transcript library")
    library_sub = library.add_subparsers(dest="library_command", required=True)
    library_list = library_sub.add_parser("list", help="list locally synced notes")
    library_list.add_argument("--data-dir")
    library_list.add_argument("--limit", type=int, default=100)
    library_list.add_argument("--offset", type=int, default=0)
    library_list.add_argument("--search")
    library_list.set_defaults(func=cmd_library_list)
    library_show = library_sub.add_parser("show", help="show one local note and its transcript")
    library_show.add_argument("note_id")
    library_show.add_argument("--data-dir")
    library_show.set_defaults(func=cmd_library_show)
    library_title = library_sub.add_parser("title", help="edit a local note title")
    library_title.add_argument("note_id")
    library_title.add_argument("title")
    library_title.add_argument("--data-dir")
    library_title.set_defaults(func=cmd_library_title)
    library_tui = library_sub.add_parser("tui", help="open the terminal note browser")
    library_tui.add_argument("--data-dir")
    library_tui.set_defaults(func=cmd_library_tui)
    library_serve = library_sub.add_parser("serve", help="open the loopback-only note web UI")
    library_serve.add_argument("--data-dir")
    library_serve.add_argument("--host", default=DEFAULT_HOST, help="loopback bind address")
    library_serve.add_argument("--port", type=int, default=DEFAULT_PORT)
    library_serve.add_argument("--no-open", action="store_true", help="do not open a browser")
    library_serve.set_defaults(func=cmd_library_serve)

    transcription = sub.add_parser("transcription", help="inspect local transcription availability")
    transcription_sub = transcription.add_subparsers(dest="transcription_command", required=True)
    transcription_status = transcription_sub.add_parser(
        "status", help="check the recommended CPU-only whisper.cpp backend"
    )
    transcription_status.add_argument("--model", help="local model path; or set PJ_WHISPER_MODEL")
    transcription_status.add_argument("--whisper-executable", help="whisper.cpp executable; or set PJ_WHISPER_CPP")
    transcription_status.add_argument("--threads", type=int)
    transcription_status.add_argument("--digest", action="store_true", help="compute the model SHA-256")
    transcription_status.set_defaults(func=cmd_transcription_status)
    transcription_benchmark = transcription_sub.add_parser(
        "benchmark",
        help="run a manifest-driven, CPU-only whisper.cpp benchmark",
    )
    transcription_benchmark.add_argument("--manifest", required=True, help="JSON corpus manifest")
    transcription_benchmark.add_argument("--model", help="local model path; or set PJ_WHISPER_MODEL")
    transcription_benchmark.add_argument(
        "--whisper-executable",
        help="whisper.cpp executable; or set PJ_WHISPER_CPP",
    )
    transcription_benchmark.add_argument("--threads", type=int)
    transcription_benchmark.add_argument("--runs", type=int, default=2)
    transcription_benchmark.add_argument(
        "--timeout",
        type=float,
        default=60 * 60,
        help="per-process timeout in seconds (default: 3600)",
    )
    transcription_benchmark.add_argument("--output", help="atomically write the JSON report")
    transcription_benchmark.add_argument(
        "--runtime-root",
        help="runtime install directory to measure; defaults to the executable",
    )
    transcription_benchmark.add_argument("--runtime-source")
    transcription_benchmark.add_argument("--runtime-license")
    transcription_benchmark.add_argument("--model-source")
    transcription_benchmark.add_argument("--model-license")
    transcription_benchmark.set_defaults(func=cmd_transcription_benchmark)

    settings = sub.add_parser("settings", help="device settings")
    settings_sub = settings.add_subparsers(dest="settings_command", required=True)
    settings_get = settings_sub.add_parser("get")
    settings_get.add_argument("--device", help="paired device id; auto-selected when unambiguous")
    settings_get.add_argument("--base-url")
    settings_get.add_argument("--token", help="override the stored LAN bearer token")
    settings_get.add_argument("--data-dir")
    settings_get.add_argument("--serial-port", help="USB-C serial port; auto-detected when omitted")
    settings_get.add_argument("--serial-baud", type=int, default=115200)
    settings_get.add_argument("--timeout", type=float, default=6.0)
    settings_get.add_argument("--transport", choices=["auto", "usb", "lan"], default="auto")
    settings_get.set_defaults(func=cmd_settings_get)
    settings_set = settings_sub.add_parser("set")
    settings_set.add_argument("--device", help="paired device id; auto-selected when unambiguous")
    settings_set.add_argument("--base-url")
    settings_set.add_argument("--token", help="override the stored LAN bearer token")
    settings_set.add_argument("--data-dir")
    settings_set.add_argument("--serial-port", help="USB-C serial port; auto-detected when omitted")
    settings_set.add_argument("--serial-baud", type=int, default=115200)
    settings_set.add_argument("--timeout", type=float, default=6.0)
    settings_set.add_argument("--transport", choices=["auto", "usb", "lan"], default="auto")
    settings_set.add_argument("assignments", nargs="+")
    settings_set.set_defaults(func=cmd_settings_set)

    recordings = sub.add_parser("recordings", help="recording maintenance commands")
    recordings_sub = recordings.add_subparsers(dest="recordings_command", required=True)
    recordings_list = recordings_sub.add_parser("list", help="list retained recordings over USB-C or LAN/Wi-Fi")
    recordings_list.add_argument("--device", help="paired device id for LAN/Wi-Fi")
    recordings_list.add_argument("--base-url")
    recordings_list.add_argument("--token", help="override the stored LAN bearer token")
    recordings_list.add_argument("--data-dir")
    recordings_list.add_argument("--serial-port", help="USB-C serial port; auto-detected when omitted")
    recordings_list.add_argument("--serial-baud", type=int, default=115200)
    recordings_list.add_argument("--timeout", type=float, default=20.0)
    recordings_list.add_argument("--transport", choices=["auto", "usb", "lan"], default="auto")
    recordings_list.set_defaults(func=cmd_recordings_list)
    recordings_download = recordings_sub.add_parser("download", help="download one recording over USB-C or LAN/Wi-Fi")
    recordings_download.add_argument("--device", help="paired device id for LAN/Wi-Fi")
    recordings_download.add_argument("--base-url")
    recordings_download.add_argument("--token", help="override the stored LAN bearer token")
    recordings_download.add_argument("--data-dir")
    recordings_download.add_argument("--serial-port", help="USB-C serial port; auto-detected when omitted")
    recordings_download.add_argument("--serial-baud", type=int, default=115200)
    recordings_download.add_argument("--timeout", type=float, default=20.0)
    recordings_download.add_argument("--transport", choices=["auto", "usb", "lan"], default="auto")
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
    recordings_wipe.add_argument("--transport", choices=["auto", "usb", "lan"], default="auto")
    recordings_wipe.add_argument("--yes", action="store_true", help="confirm deletion of all device recordings")
    recordings_wipe.set_defaults(func=cmd_recordings_wipe)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except DeviceError as exc:
        print(f"pj: error: {exc}", file=sys.stderr)
        return 1
    except sqlite3.Error as exc:
        print(f"pj: error: local note library failed: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    except BrokenPipeError:
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
