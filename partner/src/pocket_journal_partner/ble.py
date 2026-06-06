from __future__ import annotations

from dataclasses import dataclass
import secrets


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

    _ = bleak
    _ = ssid
    _ = password
    raise NotImplementedError("BLE provisioning transport is staged; use --mock until hardware is available")

