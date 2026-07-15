from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import json
import os
import tempfile


def default_data_dir() -> Path:
    return Path(os.environ.get("POCKET_JOURNAL_HOME", Path.home() / ".pocket-journal"))


@dataclass
class DeviceProfile:
    device_id: str
    base_url: str = ""
    token: str = ""
    ble_name: str = ""


@dataclass
class TranscriptionProfile:
    backend: str = "whisper-cpp"
    executable: str = ""
    executable_sha256: str = ""
    runtime_version: str = ""
    runtime_source: str = ""
    runtime_license: str = ""
    runtime_archive_sha256: str = ""
    model_path: str = ""
    model_sha256: str = ""
    model_source: str = ""
    model_license: str = ""
    threads: int = 4

    @property
    def configured(self) -> bool:
        return bool(
            self.backend == "whisper-cpp"
            and self.executable
            and self.executable_sha256
            and self.model_path
            and self.model_sha256
        )


@dataclass
class PartnerConfig:
    devices: dict[str, DeviceProfile] = field(default_factory=dict)
    transcription: TranscriptionProfile | None = None

    @classmethod
    def load(cls, path: Path) -> "PartnerConfig":
        if not path.exists():
            return cls()
        data = json.loads(path.read_text(encoding="utf-8"))
        devices = {
            key: DeviceProfile(**value)
            for key, value in data.get("devices", {}).items()
        }
        raw_transcription = data.get("transcription")
        transcription = (
            TranscriptionProfile(**raw_transcription)
            if isinstance(raw_transcription, dict)
            else None
        )
        return cls(devices=devices, transcription=transcription)

    def save(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        data = {
            "devices": {
                key: profile.__dict__
                for key, profile in sorted(self.devices.items())
            }
        }
        if self.transcription is not None:
            data["transcription"] = self.transcription.__dict__
        serialized = json.dumps(data, indent=2, sort_keys=True) + "\n"
        temp_path: Path | None = None
        try:
            with tempfile.NamedTemporaryFile(
                "w",
                encoding="utf-8",
                dir=path.parent,
                prefix=f".{path.name}.",
                delete=False,
            ) as handle:
                temp_path = Path(handle.name)
                handle.write(serialized)
            os.chmod(temp_path, 0o600)
            os.replace(temp_path, path)
        finally:
            if temp_path is not None and temp_path.exists():
                temp_path.unlink()


def config_path(data_dir: Path | None = None) -> Path:
    return (data_dir or default_data_dir()) / "config.json"


def load_config(data_dir: Path | None = None) -> PartnerConfig:
    return PartnerConfig.load(config_path(data_dir))


def save_config(config: PartnerConfig, data_dir: Path | None = None) -> None:
    config.save(config_path(data_dir))
