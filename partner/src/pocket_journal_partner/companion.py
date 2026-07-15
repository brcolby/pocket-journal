from __future__ import annotations

from collections import OrderedDict
from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Callable
import hmac
import ipaddress
import json
import re
import socket
import threading

from .config import DeviceProfile
from .device import (
    DeviceClient,
    DeviceError,
    SerialDeviceClient,
    resolve_serial_port,
)
from .storage import PartnerStore
from .sync import sync_device_audio
from .transcription import TranscriptionBackend


COMPANION_SERVICE_TYPE = "_pj-companion._tcp.local."
COMPANION_SYNC_PATH = "/v1/sync"
DEFAULT_COMPANION_HOST = "0.0.0.0"
DEFAULT_COMPANION_PORT = 8765
MAX_REQUEST_BYTES = 1024
MAX_JOBS = 32
DEFAULT_USB_POLL_INTERVAL = 2.0
_OPERATION_ID = re.compile(r"^[A-Za-z0-9_-]{1,64}$")


@dataclass
class CompanionJob:
    operation_id: str
    device_id: str
    generation: int
    source_host: str
    client: Any | None = field(default=None, repr=False)
    state: str = "queued"
    stage: str = "queued"
    total: int = 0
    pending: int = 0
    transferred: int = 0
    failed: int = 0
    processed: int = 0
    error: str = ""
    lock: threading.Lock = field(default_factory=threading.Lock, repr=False)

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            payload: dict[str, Any] = {
                "operation_id": self.operation_id,
                "device_id": self.device_id,
                "generation": self.generation,
                "state": self.state,
                "stage": self.stage,
                "total": self.total,
                "pending": self.pending,
                "transferred": self.transferred,
                "failed": self.failed,
            }
            if self.error:
                payload["error"] = self.error[:160]
            return payload


class CompanionJobRegistry:
    def __init__(
        self,
        profile: DeviceProfile,
        store: PartnerStore,
        backend: TranscriptionBackend,
        *,
        client_factory: Callable[[str, str], DeviceClient] = DeviceClient,
        sync_runner: Callable[..., list[dict[str, Any]]] = sync_device_audio,
        worker_factory: Callable[[Callable[[], None]], None] | None = None,
    ) -> None:
        self.profile = profile
        self.store = store
        self.backend = backend
        self.client_factory = client_factory
        self.sync_runner = sync_runner
        self.worker_factory = worker_factory or self._start_thread
        self._lock = threading.Lock()
        self._jobs: OrderedDict[str, CompanionJob] = OrderedDict()
        self._active_id: str | None = None

    @staticmethod
    def _start_thread(target: Callable[[], None]) -> None:
        threading.Thread(target=target, name="pj-companion-sync", daemon=True).start()

    def _start(
        self,
        request_id: str,
        generation: int,
        source_host: str,
        client: Any | None,
    ) -> tuple[CompanionJob, bool]:
        if not _OPERATION_ID.fullmatch(request_id):
            raise ValueError("request_id must contain 1..64 letters, digits, '_' or '-'")
        if isinstance(generation, bool) or not isinstance(generation, int) or not 1 <= generation <= 0xFFFFFFFF:
            raise ValueError("generation must be between 1 and 4294967295")
        normalized_host = ""
        if client is None:
            normalized_host = str(ipaddress.ip_address(source_host))
        with self._lock:
            existing = self._jobs.get(request_id)
            if existing is not None:
                if existing.generation != generation:
                    raise ValueError("operation id was reused with a different generation")
                return existing, False
            if self._active_id is not None:
                active = self._jobs.get(self._active_id)
                if active is not None and active.snapshot()["state"] in {"queued", "running"}:
                    raise RuntimeError("another sync generation is active")
            job = CompanionJob(
                request_id,
                self.profile.device_id,
                generation,
                normalized_host,
                client,
            )
            self._jobs[request_id] = job
            self._active_id = request_id
            while len(self._jobs) > MAX_JOBS:
                self._jobs.popitem(last=False)
        try:
            self.worker_factory(lambda: self._run(job))
        except Exception:
            with self._lock:
                if self._active_id == request_id:
                    self._active_id = None
                self._jobs.pop(request_id, None)
            raise
        return job, True

    def start(
        self, request_id: str, source_host: str, generation: int
    ) -> tuple[CompanionJob, bool]:
        return self._start(request_id, generation, source_host, None)

    def start_usb(
        self, request_id: str, generation: int, client: SerialDeviceClient
    ) -> tuple[CompanionJob, bool]:
        return self._start(request_id, generation, "", client)

    def get(self, operation_id: str) -> CompanionJob | None:
        if not _OPERATION_ID.fullmatch(operation_id):
            return None
        with self._lock:
            return self._jobs.get(operation_id)

    def _progress(self, job: CompanionJob, event: dict[str, Any]) -> None:
        kind = event.get("event")
        with job.lock:
            job.state = "running"
            if kind == "listed":
                job.total = max(0, int(event.get("total", 0)))
                job.pending = job.total
                job.stage = "listed"
            elif kind == "item_started":
                job.stage = "transferring"
            elif kind == "item_complete":
                job.processed += 1
                status = event.get("status")
                if status in {"uploaded", "skipped"}:
                    job.transferred += 1
                elif status == "failed":
                    job.failed += 1
                job.pending = max(0, job.total - job.transferred)
                job.stage = "transcribed" if status in {"uploaded", "transcribed"} else str(status)
            elif kind == "complete":
                job.pending = max(0, job.total - job.transferred)
                job.stage = "finalizing"

    def _run(self, job: CompanionJob) -> None:
        try:
            if job.client is not None:
                client = job.client
            else:
                host = f"[{job.source_host}]" if ":" in job.source_host else job.source_host
                client = self.client_factory(f"http://{host}:80", self.profile.token)
            results = self.sync_runner(
                self.profile.device_id,
                client,
                self.store,
                self.backend,
                progress=lambda event: self._progress(job, event),
            )
            failures = [result for result in results if result.get("status") == "failed"]
            with job.lock:
                job.failed = len(failures)
                job.pending = max(0, job.total - job.transferred)
                if failures:
                    job.state = "failed"
                    job.stage = "failed"
                    job.error = f"{len(failures)} recording(s) failed"
                else:
                    job.state = "succeeded"
                    job.stage = "complete"
        except Exception as exc:
            with job.lock:
                job.state = "failed"
                job.stage = "failed"
                job.error = str(exc).strip() or exc.__class__.__name__
        finally:
            with self._lock:
                if self._active_id == job.operation_id:
                    self._active_id = None


class CompanionHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def _bearer_token(header: str | None) -> str:
    if header is None or not header.startswith("Bearer "):
        return ""
    return header[7:]


def _handler_for(registry: CompanionJobRegistry) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        server_version = "PocketJournalCompanion/1"
        sys_version = ""

        def log_message(self, format: str, *args: object) -> None:
            _ = format
            _ = args

        def _json(self, status: int, payload: dict[str, Any]) -> None:
            body = json.dumps(payload, separators=(",", ":"), sort_keys=True).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _authorized(self) -> bool:
            supplied = _bearer_token(self.headers.get("Authorization"))
            return bool(supplied) and hmac.compare_digest(supplied, registry.profile.token)

        def do_POST(self) -> None:
            if self.path != COMPANION_SYNC_PATH:
                self._json(404, {"error": "not found"})
                return
            try:
                length = int(self.headers.get("Content-Length", ""))
            except ValueError:
                length = -1
            if length < 2 or length > MAX_REQUEST_BYTES:
                self._json(400, {"error": "invalid request body"})
                return
            try:
                payload = json.loads(self.rfile.read(length).decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                self._json(400, {"error": "invalid request body"})
                return
            if not isinstance(payload, dict) or payload.get("device_id") != registry.profile.device_id:
                self._json(404, {"error": "device is not paired with this companion"})
                return
            if not self._authorized():
                self._json(401, {"error": "unauthorized"})
                return
            request_id = payload.get("request_id")
            generation = payload.get("generation")
            if not isinstance(request_id, str) or isinstance(generation, bool) or not isinstance(generation, int):
                self._json(400, {"error": "invalid request_id or generation"})
                return
            try:
                job, created = registry.start(
                    request_id, self.client_address[0], generation
                )
            except ValueError as exc:
                self._json(400, {"error": str(exc)})
                return
            except RuntimeError as exc:
                self._json(409, {"error": str(exc)})
                return
            self._json(202 if created else 200, job.snapshot())

        def do_GET(self) -> None:
            prefix = COMPANION_SYNC_PATH + "/"
            if not self.path.startswith(prefix):
                self._json(404, {"error": "not found"})
                return
            job = registry.get(self.path[len(prefix):])
            if job is None:
                self._json(404, {"error": "operation not found"})
                return
            if not self._authorized():
                self._json(401, {"error": "unauthorized"})
                return
            self._json(200, job.snapshot())

    return Handler


def local_ipv4_addresses() -> list[str]:
    addresses: set[str] = set()
    try:
        for family, _, _, _, sockaddr in socket.getaddrinfo(
            socket.gethostname(), None, socket.AF_INET, socket.SOCK_STREAM
        ):
            if family == socket.AF_INET:
                addresses.add(sockaddr[0])
    except OSError:
        pass
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("192.0.2.1", 9))
        addresses.add(probe.getsockname()[0])
    except OSError:
        pass
    finally:
        probe.close()
    return sorted(
        address for address in addresses
        if not ipaddress.ip_address(address).is_loopback and address != "0.0.0.0"
    )


class CompanionUSBPoller:
    def __init__(
        self,
        profile: DeviceProfile,
        registry: CompanionJobRegistry,
        *,
        serial_port: str | None = None,
        poll_interval: float = DEFAULT_USB_POLL_INTERVAL,
        port_resolver: Callable[[str | None], str] = resolve_serial_port,
        client_factory: Callable[[str], SerialDeviceClient] = SerialDeviceClient,
    ) -> None:
        if not 0.25 <= poll_interval <= 60.0:
            raise ValueError("USB poll interval must be between 0.25 and 60 seconds")
        self.profile = profile
        self.registry = registry
        self.serial_port = serial_port
        self.poll_interval = poll_interval
        self.port_resolver = port_resolver
        self.client_factory = client_factory
        self.last_error = ""
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    @staticmethod
    def _reported_state(snapshot: dict[str, Any]) -> str:
        state = snapshot.get("state")
        return state if state in {"succeeded", "failed"} else "running"

    def poll_once(self) -> dict[str, Any] | None:
        port = self.port_resolver(self.serial_port)
        client = self.client_factory(port)
        status = client.companion_sync_status()
        if status.get("device_id") != self.profile.device_id:
            raise DeviceError(
                f"USB device {status.get('device_id')!r} does not match paired device "
                f"{self.profile.device_id!r}"
            )
        if not status["request_pending"]:
            self.last_error = ""
            return None
        generation = int(status["claim_generation"])
        operation_id = str(status["operation_id"])
        claim = client.companion_sync_claim(generation, operation_id)
        if claim["claim_result"] in {"busy", "stale"}:
            return claim
        try:
            job, _ = self.registry.start_usb(operation_id, generation, client)
        except RuntimeError:
            return claim
        snapshot = job.snapshot()
        reported_state = self._reported_state(snapshot)
        client.companion_sync_progress(
            generation,
            operation_id,
            reported_state,
            int(snapshot["pending"]),
            int(snapshot["transferred"]),
            int(snapshot["failed"]),
            str(snapshot.get("error", "")),
        )
        self.last_error = ""
        return snapshot

    def run(self) -> None:
        while not self._stop.is_set():
            try:
                self.poll_once()
            except (DeviceError, OSError, ValueError) as exc:
                self.last_error = str(exc).strip() or exc.__class__.__name__
            self._stop.wait(self.poll_interval)

    def start(self) -> None:
        if self._thread is not None and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(
            target=self.run, name="pj-companion-usb", daemon=True
        )
        self._thread.start()

    def close(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=self.poll_interval + 8.0)
            self._thread = None


class CompanionService:
    def __init__(
        self,
        profile: DeviceProfile,
        store: PartnerStore,
        backend: TranscriptionBackend,
        *,
        host: str = DEFAULT_COMPANION_HOST,
        port: int = DEFAULT_COMPANION_PORT,
        advertise_addresses: list[str] | None = None,
        advertise: bool = True,
        usb: bool = True,
        serial_port: str | None = None,
        usb_poll_interval: float = DEFAULT_USB_POLL_INTERVAL,
    ) -> None:
        self.profile = profile
        self.registry = CompanionJobRegistry(profile, store, backend)
        self.server = CompanionHTTPServer((host, port), _handler_for(self.registry))
        self.advertise_addresses = advertise_addresses
        self.advertise = advertise
        self.usb_poller = CompanionUSBPoller(
            profile,
            self.registry,
            serial_port=serial_port,
            poll_interval=usb_poll_interval,
        ) if usb else None
        self._zeroconf: Any | None = None
        self._service_info: Any | None = None

    @property
    def port(self) -> int:
        return int(self.server.server_address[1])

    def _start_advertising(self) -> None:
        if not self.advertise:
            return
        try:
            from zeroconf import ServiceInfo, Zeroconf  # type: ignore
        except ImportError as exc:
            raise RuntimeError("mDNS advertisement requires the zeroconf package") from exc
        addresses = self.advertise_addresses or local_ipv4_addresses()
        if not addresses:
            if self.usb_poller is not None:
                return
            raise RuntimeError("no non-loopback IPv4 address is available for mDNS advertisement")
        packed = [socket.inet_aton(str(ipaddress.ip_address(address))) for address in addresses]
        hostname = re.sub(r"[^A-Za-z0-9-]", "-", socket.gethostname().split(".", 1)[0]).strip("-")
        hostname = hostname or "pocket-journal-companion"
        name = f"Pocket Journal Companion {self.profile.device_id}.{COMPANION_SERVICE_TYPE}"
        self._service_info = ServiceInfo(
            COMPANION_SERVICE_TYPE,
            name,
            addresses=packed,
            port=self.port,
            properties={
                "device_id": self.profile.device_id,
                "path": COMPANION_SYNC_PATH,
                "api": "1",
            },
            server=f"{hostname}.local.",
        )
        self._zeroconf = Zeroconf()
        self._zeroconf.register_service(self._service_info)

    def serve_forever(self) -> None:
        try:
            if self.usb_poller is not None:
                self.usb_poller.start()
            self._start_advertising()
            self.server.serve_forever(poll_interval=0.25)
        finally:
            self.close()

    def close(self) -> None:
        if self.usb_poller is not None:
            self.usb_poller.close()
        if self._zeroconf is not None and self._service_info is not None:
            self._zeroconf.unregister_service(self._service_info)
            self._zeroconf.close()
            self._zeroconf = None
            self._service_info = None
        self.server.server_close()
