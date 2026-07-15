from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from .device import AudioItem, DeviceClient, DeviceError, SerialDeviceClient


SUPPORTED_API_MAJOR = 1


@dataclass(frozen=True)
class DeviceSession:
    device_id: str
    client: DeviceClient | SerialDeviceClient

    @property
    def transport(self) -> str:
        return "usb" if isinstance(self.client, SerialDeviceClient) else "lan"

    def envelope(self, result: Any) -> dict[str, Any]:
        return {
            "device_id": self.device_id,
            "transport": self.transport,
            "result": result,
        }

    def status(self) -> dict[str, Any]:
        status = self.client.status()
        validate_status_compatibility(status)
        return status

    def require(self, capability: str, *, destructive: bool = False) -> None:
        if isinstance(self.client, SerialDeviceClient):
            supported = {
                "status",
                "time.write",
                "settings.read",
                "settings.write",
                "recordings.delete",
                "recordings.list",
                "recordings.download",
                "transcripts.write",
                "audio.sync",
                "diagnostics.tone",
                "diagnostics.microphone",
                "wifi.provision",
            }
            if capability not in supported:
                raise DeviceError(f"{capability} is not supported over USB-C; use LAN/Wi-Fi")

        # A status preflight catches explicit API mismatches before mutations and
        # honors capability advertisements when newer firmware provides them.
        if destructive or capability != "status":
            status = self.status()
            advertised = status.get("capabilities")
            if isinstance(advertised, dict) and advertised.get(capability) is False:
                raise DeviceError(f"device firmware does not support capability: {capability}")
            if isinstance(advertised, list) and capability not in advertised:
                raise DeviceError(f"device firmware does not support capability: {capability}")

    def list_recordings(self) -> list[AudioItem]:
        self.require("recordings.list")
        return self.client.list_audio()


def validate_status_compatibility(status: dict[str, Any]) -> None:
    if not isinstance(status, dict):
        raise DeviceError("device returned an invalid status payload")
    api_version = status.get("api_version")
    if api_version is None:
        return
    raw = str(api_version).lower().removeprefix("v").split(".", 1)[0]
    try:
        major = int(raw)
    except ValueError as exc:
        raise DeviceError(f"device reported an invalid API version: {api_version!r}") from exc
    if major != SUPPORTED_API_MAJOR:
        raise DeviceError(
            f"unsupported device API version {api_version!r}; partner supports v{SUPPORTED_API_MAJOR}"
        )
