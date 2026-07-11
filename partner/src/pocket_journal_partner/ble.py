from __future__ import annotations

from dataclasses import dataclass
import asyncio
import json
import secrets


SERVICE_UUID = "7e400001-b5a3-f393-e0a9-e50e24dcca9e"
SSID_UUID = "7e400002-b5a3-f393-e0a9-e50e24dcca9e"
PASSWORD_UUID = "7e400003-b5a3-f393-e0a9-e50e24dcca9e"
TOKEN_UUID = "7e400004-b5a3-f393-e0a9-e50e24dcca9e"
COMMIT_UUID = "7e400005-b5a3-f393-e0a9-e50e24dcca9e"
STATUS_UUID = "7e400006-b5a3-f393-e0a9-e50e24dcca9e"


@dataclass
class ProvisionedDevice:
    device_id: str
    ble_name: str
    token: str


async def provision_wifi(ble_name: str | None, ssid: str, password: str, *, mock: bool = False) -> ProvisionedDevice:
    if mock:
        suffix = secrets.token_hex(2).upper()
        return ProvisionedDevice(
            device_id=f"pj-{suffix.lower()}",
            ble_name=ble_name or f"PJ-{suffix}",
            token=secrets.token_urlsafe(24),
        )

    try:
        import bleak  # type: ignore
    except ImportError as exc:
        raise RuntimeError("Install the BLE extra first: pip install -e '.[ble]'") from exc

    if ble_name:
        device = await bleak.BleakScanner.find_device_by_name(ble_name, timeout=10.0)
    else:
        devices = await bleak.BleakScanner.discover(timeout=8.0)
        device = next((item for item in devices if (item.name or "").startswith("PJ-")), None)
    if device is None:
        raise RuntimeError(f"Pocket Journal BLE device not found{f': {ble_name}' if ble_name else ''}")

    token = secrets.token_urlsafe(24)
    async with bleak.BleakClient(device) as client:
        initial = json.loads(bytes(await client.read_gatt_char(STATUS_UUID)).decode("utf-8"))
        await client.write_gatt_char(SSID_UUID, ssid.encode("utf-8"), response=True)
        await client.write_gatt_char(PASSWORD_UUID, password.encode("utf-8"), response=True)
        await client.write_gatt_char(TOKEN_UUID, token.encode("utf-8"), response=True)
        await client.write_gatt_char(COMMIT_UUID, b"\x01", response=True)
        status = initial
        for _ in range(25):
            status = json.loads(bytes(await client.read_gatt_char(STATUS_UUID)).decode("utf-8"))
            if status.get("state") not in {"idle", "connected", "applying"}:
                break
            await asyncio.sleep(0.2)

    device_id = str(status.get("device_id") or initial.get("device_id") or "")
    if not device_id:
        raise RuntimeError("BLE provisioning response did not include device_id")
    return ProvisionedDevice(device_id=device_id, ble_name=str(device.name), token=token)
