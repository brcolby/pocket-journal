from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib import error, parse, request
import json


class DeviceError(RuntimeError):
    pass


@dataclass
class AudioItem:
    audio_id: str
    filename: str
    size: int | None = None
    created_at: str | None = None
    transcript_uploaded: bool = False


class DeviceClient:
    def __init__(self, base_url: str, token: str, timeout: float = 20.0) -> None:
        self.base_url = base_url.rstrip("/")
        self.token = token
        self.timeout = timeout

    def _url(self, path: str) -> str:
        return parse.urljoin(self.base_url + "/", path.lstrip("/"))

    def _request(self, method: str, path: str, body: Any | None = None) -> Any:
        data = None
        headers = {"Authorization": f"Bearer {self.token}"}
        if body is not None:
            data = json.dumps(body).encode("utf-8")
            headers["Content-Type"] = "application/json"
        req = request.Request(self._url(path), data=data, method=method, headers=headers)
        try:
            with request.urlopen(req, timeout=self.timeout) as response:
                payload = response.read()
                if not payload:
                    return None
                content_type = response.headers.get("Content-Type", "")
                if "application/json" in content_type:
                    return json.loads(payload.decode("utf-8"))
                return payload
        except error.HTTPError as exc:
            raise DeviceError(f"{method} {path} failed: HTTP {exc.code}") from exc
        except error.URLError as exc:
            raise DeviceError(f"{method} {path} failed: {exc.reason}") from exc

    def status(self) -> dict[str, Any]:
        return self._request("GET", "/v1/status")

    def get_settings(self) -> dict[str, Any]:
        return self._request("GET", "/v1/settings")

    def put_settings(self, settings: dict[str, Any]) -> dict[str, Any] | None:
        return self._request("PUT", "/v1/settings", settings)

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
        return [AudioItem(**item) for item in payload.get("audio", [])]

    def download_audio(self, item: AudioItem, target_dir: Path) -> Path:
        target_dir.mkdir(parents=True, exist_ok=True)
        data = self._request("GET", f"/v1/audio/{parse.quote(item.audio_id)}")
        path = target_dir / item.filename
        path.write_bytes(data)
        return path

    def upload_transcript(self, audio_id: str, transcript: dict[str, Any]) -> None:
        self._request("PUT", f"/v1/transcripts/{parse.quote(audio_id)}", transcript)

    def upload_calendar_today(self, payload: dict[str, Any]) -> None:
        self._request("PUT", "/v1/calendar/today", payload)


def discover_mdns() -> list[dict[str, str]]:
    return []
