from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import json
import os


def default_data_dir() -> Path:
    return Path(os.environ.get("POCKET_JOURNAL_HOME", Path.home() / ".pocket-journal"))


@dataclass
class DeviceProfile:
    device_id: str
    base_url: str = ""
    token: str = ""
    ble_name: str = ""


@dataclass
class PartnerConfig:
    devices: dict[str, DeviceProfile] = field(default_factory=dict)

    @classmethod
    def load(cls, path: Path) -> "PartnerConfig":
        if not path.exists():
            return cls()
        data = json.loads(path.read_text(encoding="utf-8"))
        devices = {
            key: DeviceProfile(**value)
            for key, value in data.get("devices", {}).items()
        }
        return cls(devices=devices)

    def save(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        data = {
            "devices": {
                key: profile.__dict__
                for key, profile in sorted(self.devices.items())
            }
        }
        path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def config_path(data_dir: Path | None = None) -> Path:
    return (data_dir or default_data_dir()) / "config.json"


def load_config(data_dir: Path | None = None) -> PartnerConfig:
    return PartnerConfig.load(config_path(data_dir))


def save_config(config: PartnerConfig, data_dir: Path | None = None) -> None:
    config.save(config_path(data_dir))

