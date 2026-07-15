from __future__ import annotations

from collections import OrderedDict
from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Callable
import ipaddress
import json
import re
import socket
import threading
import time

from .config import DeviceProfile
from .companion_auth import (
    CompanionAuthenticationError,
    CompanionProtocolError,
    normalize_error,
    pairing_epoch,
    provisioned_token,
    response_envelope,
    scoped_data_token,
    verify_request_envelope,
)
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
COMPANION_STATUS_PATH = "/v1/sync/status"
DEFAULT_COMPANION_HOST = "0.0.0.0"
DEFAULT_COMPANION_PORT = 8765
MAX_REQUEST_BYTES = 1024
MAX_JOBS = 32
COMPANION_STATE_SCHEMA = 2
DEFAULT_USB_POLL_INTERVAL = 2.0
_OPERATION_ID = re.compile(r"^[A-Za-z0-9_-]{1,64}$")
_REQUEST_NONCE = re.compile(r"^[0-9a-f]{32}$")
_TERMINAL_FIELDS = {"state", "total", "pending", "transferred", "failed", "error"}


class CompanionPersistenceError(RuntimeError):
    pass


def _unique_json_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for name, value in pairs:
        if name in result:
            raise ValueError("duplicate JSON field")
        result[name] = value
    return result


@dataclass
class CompanionJob:
    operation_id: str
    device_id: str
    generation: int
    requested_ms: int
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
    persistence_lock: threading.Lock = field(default_factory=threading.Lock, repr=False)
    terminal_candidate: dict[str, Any] | None = field(default=None, repr=False)

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            payload: dict[str, Any] = {
                "operation_id": self.operation_id,
                "device_id": self.device_id,
                "generation": self.generation,
                "requested_ms": self.requested_ms,
                "state": self.state,
                "stage": self.stage,
                "total": self.total,
                "pending": self.pending,
                "transferred": self.transferred,
                "failed": self.failed,
            }
            payload["error"] = normalize_error(self.error) if self.error else ""
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
        worker_factory: Callable[[Callable[[], None]], Any] | None = None,
    ) -> None:
        if not provisioned_token(profile.token):
            raise ValueError("companion requires a provisioned pairing token")
        self.profile = profile
        self.store = store
        self.backend = backend
        self.client_factory = client_factory
        self.sync_runner = sync_runner
        self.worker_factory = worker_factory
        self._lock = threading.Lock()
        self._jobs: OrderedDict[str, CompanionJob] = OrderedDict()
        self._active_id: str | None = None
        self._workers: set[threading.Thread] = set()
        self._closing = threading.Event()
        self._durable = self._load_durable_state()

    def _load_durable_state(self) -> dict[str, Any] | None:
        payload = self.store.load_companion_state(self.profile.device_id)
        if payload is None:
            return None
        expected = {
            "schema_version", "device_id", "operation_id", "generation",
            "requested_ms", "last_start_nonce", "pairing_epoch", "terminal",
        }
        operation_id = payload.get("operation_id")
        generation = payload.get("generation")
        requested_ms = payload.get("requested_ms")
        nonce = payload.get("last_start_nonce")
        epoch = payload.get("pairing_epoch")
        if (
            set(payload) != expected
            or payload.get("schema_version") != COMPANION_STATE_SCHEMA
            or payload.get("device_id") != self.profile.device_id
            or not isinstance(epoch, str)
            or re.fullmatch(r"[0-9a-f]{64}", epoch) is None
            or not isinstance(operation_id, str)
            or not _OPERATION_ID.fullmatch(operation_id)
            or isinstance(generation, bool) or not isinstance(generation, int)
            or not 1 <= generation <= 0xFFFFFFFF
            or isinstance(requested_ms, bool) or not isinstance(requested_ms, int)
            or not 0 <= requested_ms <= (1 << 53) - 1
            or not isinstance(nonce, str)
            or (nonce != "" and not _REQUEST_NONCE.fullmatch(nonce))
        ):
            raise ValueError("companion replay state is invalid")
        if epoch != pairing_epoch(self.profile.token):
            return None
        terminal = payload.get("terminal")
        if terminal is not None:
            if not isinstance(terminal, dict) or set(terminal) != _TERMINAL_FIELDS:
                raise ValueError("companion terminal replay state is invalid")
            snapshot = {
                "device_id": self.profile.device_id,
                "operation_id": operation_id,
                "generation": generation,
                "requested_ms": requested_ms,
                **terminal,
            }
            try:
                checked = response_envelope(
                    snapshot, self.profile.token,
                    action="status", nonce="0" * 32,
                )
            except (CompanionProtocolError, CompanionAuthenticationError, TypeError) as exc:
                raise ValueError("companion terminal replay state is invalid") from exc
            if terminal.get("state") not in {"succeeded", "failed"} or any(
                checked[field] != terminal[field] for field in _TERMINAL_FIELDS
            ):
                raise ValueError("companion terminal replay state is invalid")
        return payload

    def _save_durable_state(self, payload: dict[str, Any]) -> None:
        try:
            self.store.save_companion_state(self.profile.device_id, payload)
        except (OSError, UnicodeError, TypeError, ValueError) as exc:
            raise CompanionPersistenceError(
                "unable to persist companion replay state"
            ) from exc
        self._durable = payload

    @staticmethod
    def _restore_terminal_job(
        durable: dict[str, Any], source_host: str, client: Any | None,
    ) -> CompanionJob:
        job = CompanionJob(
            str(durable["operation_id"]), str(durable["device_id"]),
            int(durable["generation"]), int(durable["requested_ms"]),
            source_host, client,
        )
        terminal = durable["terminal"]
        job.state = str(terminal["state"])
        job.stage = "complete" if job.state == "succeeded" else "failed"
        job.total = int(terminal["total"])
        job.pending = int(terminal["pending"])
        job.transferred = int(terminal["transferred"])
        job.failed = int(terminal["failed"])
        job.error = str(terminal["error"])
        return job

    @staticmethod
    def _new_worker(target: Callable[[], None]) -> threading.Thread:
        return threading.Thread(
            target=target, name="pj-companion-sync", daemon=True
        )

    def _start(
        self,
        request_id: str,
        generation: int,
        requested_ms: int,
        source_host: str,
        client: Any | None,
        request_nonce: str = "",
    ) -> tuple[CompanionJob, bool]:
        if not _OPERATION_ID.fullmatch(request_id):
            raise ValueError("request_id must contain 1..64 letters, digits, '_' or '-'")
        if isinstance(generation, bool) or not isinstance(generation, int) or not 1 <= generation <= 0xFFFFFFFF:
            raise ValueError("generation must be between 1 and 4294967295")
        if (
            isinstance(requested_ms, bool) or not isinstance(requested_ms, int)
            or not 0 <= requested_ms <= (1 << 53) - 1
        ):
            raise ValueError("requested_ms is out of range")
        if request_nonce and not _REQUEST_NONCE.fullmatch(request_nonce):
            raise ValueError("invalid start request nonce")
        normalized_host = ""
        if client is None:
            normalized_host = str(ipaddress.ip_address(source_host))
        with self._lock:
            if self._closing.is_set():
                raise RuntimeError("companion is shutting down")
            existing = self._jobs.get(request_id)
            if existing is not None:
                if existing.generation != generation or existing.requested_ms != requested_ms:
                    raise ValueError("operation id was reused with different request identity")
                return existing, False
            durable = self._durable
            if durable is not None:
                durable_generation = int(durable["generation"])
                if generation < durable_generation:
                    raise ValueError("stale sync generation")
                if generation == durable_generation:
                    if (
                        request_id != durable["operation_id"]
                        or requested_ms != durable["requested_ms"]
                    ):
                        raise ValueError(
                            "generation was reused with different request identity"
                        )
                    if durable["terminal"] is not None:
                        restored = self._restore_terminal_job(
                            durable, normalized_host, client
                        )
                        self._jobs[request_id] = restored
                        return restored, False
                    if request_nonce and request_nonce == durable["last_start_nonce"]:
                        raise ValueError("replayed start request")
            if self._active_id is not None:
                active = self._jobs.get(self._active_id)
                if active is not None and active.snapshot()["state"] in {"queued", "running"}:
                    raise RuntimeError("another sync generation is active")
            job = CompanionJob(
                request_id,
                self.profile.device_id,
                generation,
                requested_ms,
                normalized_host,
                client,
            )
            self._save_durable_state({
                "schema_version": COMPANION_STATE_SCHEMA,
                "device_id": self.profile.device_id,
                "operation_id": request_id,
                "generation": generation,
                "requested_ms": requested_ms,
                "last_start_nonce": request_nonce,
                "pairing_epoch": pairing_epoch(self.profile.token),
                "terminal": None,
            })
            self._jobs[request_id] = job
            self._active_id = request_id
            while len(self._jobs) > MAX_JOBS:
                self._jobs.popitem(last=False)
        try:
            target = lambda: self._run(job)
            if self.worker_factory is None:
                worker = self._new_worker(target)
                with self._lock:
                    if self._closing.is_set():
                        raise RuntimeError("companion is shutting down")
                    self._workers.add(worker)
                    worker.start()
            else:
                worker = self.worker_factory(target)
                if isinstance(worker, threading.Thread):
                    with self._lock:
                        self._workers.add(worker)
        except Exception:
            with self._lock:
                if self._active_id == request_id:
                    self._active_id = None
                self._jobs.pop(request_id, None)
                if "worker" in locals() and isinstance(worker, threading.Thread):
                    self._workers.discard(worker)
            raise
        return job, True

    def start(
        self, request_id: str, source_host: str, generation: int,
        requested_ms: int, request_nonce: str = "",
    ) -> tuple[CompanionJob, bool]:
        return self._start(
            request_id, generation, requested_ms, source_host, None,
            request_nonce,
        )

    def start_usb(
        self, request_id: str, generation: int, requested_ms: int,
        client: SerialDeviceClient,
    ) -> tuple[CompanionJob, bool]:
        return self._start(request_id, generation, requested_ms, "", client)

    def get(self, operation_id: str) -> CompanionJob | None:
        if not _OPERATION_ID.fullmatch(operation_id):
            return None
        with self._lock:
            return self._jobs.get(operation_id)

    def resume_status(
        self, request_id: str, source_host: str, generation: int,
        requested_ms: int, request_nonce: str,
    ) -> CompanionJob | None:
        existing = self.get(request_id)
        if existing is not None:
            return existing
        with self._lock:
            durable = self._durable
            if (
                durable is None
                or durable["operation_id"] != request_id
                or durable["generation"] != generation
                or durable["requested_ms"] != requested_ms
            ):
                return None
        job, _ = self._start(
            request_id, generation, requested_ms, source_host, None,
            request_nonce,
        )
        return job

    def _progress(self, job: CompanionJob, event: dict[str, Any]) -> None:
        if self._closing.is_set():
            raise RuntimeError("companion is shutting down")
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
                job.pending = max(0, job.total - job.transferred - job.failed)
                job.stage = "transcribed" if status in {"uploaded", "transcribed"} else str(status)
            elif kind == "complete":
                job.pending = max(0, job.total - job.transferred - job.failed)
                job.stage = "finalizing"

    def _finish_job(
        self, job: CompanionJob, *, state: str, total: int, pending: int,
        transferred: int, failed: int, error: str,
    ) -> None:
        terminal = {
            "state": state,
            "total": total,
            "pending": pending,
            "transferred": transferred,
            "failed": failed,
            "error": normalize_error(error) if error else "",
        }
        with job.persistence_lock:
            try:
                self._persist_terminal(job, terminal)
            except RuntimeError:
                with job.lock:
                    job.state = "running"
                    job.stage = "persisting"
                    job.error = ""
                    job.terminal_candidate = terminal
                return
            self._publish_terminal(job, terminal)

    def _persist_terminal(
        self, job: CompanionJob, terminal: dict[str, Any],
    ) -> None:
        with self._lock:
            durable = self._durable
            if (
                durable is None
                or durable["operation_id"] != job.operation_id
                or durable["generation"] != job.generation
                or durable["requested_ms"] != job.requested_ms
            ):
                raise RuntimeError("companion replay identity changed")
            persisted = dict(durable)
            persisted["terminal"] = terminal
            self._save_durable_state(persisted)

    @staticmethod
    def _publish_terminal(job: CompanionJob, terminal: dict[str, Any]) -> None:
        with job.lock:
            job.terminal_candidate = None
            job.state = str(terminal["state"])
            job.stage = "complete" if job.state == "succeeded" else "failed"
            job.total = int(terminal["total"])
            job.pending = int(terminal["pending"])
            job.transferred = int(terminal["transferred"])
            job.failed = int(terminal["failed"])
            job.error = str(terminal["error"])

    def response_snapshot(self, job: CompanionJob) -> dict[str, Any]:
        with job.persistence_lock:
            with job.lock:
                candidate = (
                    dict(job.terminal_candidate)
                    if job.terminal_candidate is not None else None
                )
            if candidate is not None:
                try:
                    self._persist_terminal(job, candidate)
                except RuntimeError as exc:
                    raise RuntimeError(
                        "companion terminal state is not durable"
                    ) from exc
                self._publish_terminal(job, candidate)
        return job.snapshot()

    @staticmethod
    def _safe_counts(job: CompanionJob) -> tuple[int, int, int, int, bool]:
        with job.lock:
            raw = (job.total, job.pending, job.transferred, job.failed)
        total = max(0, raw[0])
        transferred = min(max(0, raw[2]), total)
        failed = min(max(0, raw[3]), total - transferred)
        pending = total - transferred - failed
        return total, pending, transferred, failed, raw != (
            total, pending, transferred, failed
        )

    def _run(self, job: CompanionJob) -> None:
        try:
            if self._closing.is_set():
                raise RuntimeError("companion is shutting down")
            if job.client is not None:
                client = job.client
            else:
                host = f"[{job.source_host}]" if ":" in job.source_host else job.source_host
                data_token = scoped_data_token(
                    self.profile.token, self.profile.device_id, job.operation_id,
                    job.generation, job.requested_ms,
                )
                client = self.client_factory(f"http://{host}:80", data_token)
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
                job.pending = max(0, job.total - job.transferred - job.failed)
            total, pending, transferred, failed, inconsistent = self._safe_counts(job)
            if inconsistent:
                state = "failed"
                message = "Sync results did not match the listed recordings"
            elif failures:
                state = "failed"
                message = f"{len(failures)} recording(s) failed"
            elif pending:
                state = "failed"
                message = f"Sync ended with {pending} recording(s) unresolved"
            else:
                state = "succeeded"
                message = ""
            self._finish_job(
                job, state=state, total=total, pending=pending,
                transferred=transferred, failed=failed, error=message,
            )
        except Exception as exc:
            total, pending, transferred, failed, _ = self._safe_counts(job)
            self._finish_job(
                job, state="failed", total=total, pending=pending,
                transferred=transferred, failed=failed,
                error=normalize_error(exc, exc.__class__.__name__),
            )
        finally:
            with self._lock:
                if self._active_id == job.operation_id:
                    self._active_id = None
                self._workers.discard(threading.current_thread())

    def close(self, timeout: float = 30.0) -> bool:
        self._closing.set()
        deadline = time.monotonic() + max(0.0, timeout)
        with self._lock:
            jobs = list(self._jobs.values())
        clients = [
            job.client for job in jobs
            if job.client is not None
            and job.snapshot()["state"] in {"queued", "running"}
        ]
        for client in clients:
            close = getattr(client, "close", None)
            if callable(close):
                close()
        while True:
            with self._lock:
                workers = [worker for worker in self._workers if worker.is_alive()]
            if not workers:
                return True
            for worker in workers:
                if worker is threading.current_thread():
                    continue
                worker.join(timeout=max(0.0, deadline - time.monotonic()))
            if time.monotonic() >= deadline:
                return False


class CompanionHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


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

        def do_POST(self) -> None:
            if self.path not in {COMPANION_SYNC_PATH, COMPANION_STATUS_PATH}:
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
                payload = json.loads(
                    self.rfile.read(length).decode("utf-8"),
                    object_pairs_hook=_unique_json_object,
                )
            except (UnicodeDecodeError, ValueError):
                self._json(400, {"error": "invalid request body"})
                return
            action = "start" if self.path == COMPANION_SYNC_PATH else "status"
            try:
                authenticated = verify_request_envelope(
                    payload,
                    registry.profile.token,
                    expected_action=action,
                    expected_device_id=registry.profile.device_id,
                    expected_peer_ip=self.client_address[0],
                )
            except CompanionAuthenticationError:
                self._json(401, {"error": "unauthorized"})
                return
            except CompanionProtocolError:
                self._json(400, {"error": "invalid request envelope"})
                return
            operation_id = authenticated["operation_id"]
            generation = authenticated["generation"]
            requested_ms = authenticated["requested_ms"]
            nonce = authenticated["nonce"]
            if action == "status":
                try:
                    job = registry.resume_status(
                        operation_id, authenticated["device_ip"], generation,
                        requested_ms, nonce,
                    )
                except ValueError as exc:
                    self._json(400, {"error": normalize_error(exc)})
                    return
                except RuntimeError:
                    self._json(503, {"error": "sync state is temporarily unavailable"})
                    return
                if (
                    job is None or job.generation != generation
                    or job.requested_ms != requested_ms
                ):
                    self._json(404, {"error": "operation not found"})
                    return
                try:
                    snapshot = registry.response_snapshot(job)
                except RuntimeError:
                    self._json(503, {"error": "sync state is temporarily unavailable"})
                    return
                self._json(200, response_envelope(
                    snapshot, registry.profile.token, action=action, nonce=nonce,
                ))
                return
            try:
                job, created = registry.start(
                    operation_id, authenticated["device_ip"], generation,
                    requested_ms, nonce,
                )
            except ValueError as exc:
                self._json(400, {"error": normalize_error(exc)})
                return
            except CompanionPersistenceError:
                self._json(503, {"error": "sync state is temporarily unavailable"})
                return
            except RuntimeError as exc:
                self._json(409, {"error": normalize_error(exc)})
                return
            try:
                snapshot = registry.response_snapshot(job)
            except RuntimeError:
                self._json(503, {"error": "sync state is temporarily unavailable"})
                return
            self._json(
                202 if created else 200,
                response_envelope(
                    snapshot, registry.profile.token, action=action, nonce=nonce,
                ),
            )

        def do_GET(self) -> None:
            self._json(405, {"error": "authenticated POST required"})

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
        self._closed = False
        self._client_lock = threading.Lock()
        self._active_client: SerialDeviceClient | None = None

    @staticmethod
    def _reported_state(snapshot: dict[str, Any]) -> str:
        state = snapshot.get("state")
        return state if state in {"succeeded", "failed"} else "running"

    def poll_once(self) -> dict[str, Any] | None:
        if self._closed:
            return None
        port = self.port_resolver(self.serial_port)
        if self._closed:
            return None
        client = self.client_factory(port)
        with self._client_lock:
            if self._closed:
                close = getattr(client, "close", None)
                if callable(close):
                    close()
                return None
            self._active_client = client
        try:
            return self._poll_client(client)
        finally:
            with self._client_lock:
                if self._active_client is client:
                    self._active_client = None

    def _poll_client(self, client: SerialDeviceClient) -> dict[str, Any] | None:
        status = client.companion_sync_status()
        if self._closed:
            return None
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
        requested_ms = int(status["claim_requested_ms"])
        claim = client.companion_sync_claim(generation, operation_id)
        if self._closed:
            return claim
        if claim["claim_result"] in {"busy", "stale"}:
            return claim
        try:
            job, _ = self.registry.start_usb(
                operation_id, generation, requested_ms, client
            )
        except RuntimeError:
            return claim
        try:
            snapshot = self.registry.response_snapshot(job)
        except RuntimeError as exc:
            raise DeviceError(
                "companion terminal state is temporarily unavailable"
            ) from exc
        reported_state = self._reported_state(snapshot)
        client.companion_sync_progress(
            generation,
            operation_id,
            reported_state,
            int(snapshot["total"]),
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
        if self._closed:
            raise RuntimeError("USB poller is closed")
        if self._thread is not None and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(
            target=self.run, name="pj-companion-usb", daemon=True
        )
        self._thread.start()

    def request_close(self) -> None:
        with self._client_lock:
            self._closed = True
            client = self._active_client
        self._stop.set()
        close = getattr(client, "close", None)
        if callable(close):
            close()

    def close(self, timeout: float = 2.0) -> bool:
        self.request_close()
        if self._thread is not None:
            self._thread.join(timeout=max(0.0, timeout))
            if self._thread.is_alive():
                return False
            self._thread = None
        return True


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
        self.server.timeout = 0.25
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
        self._closed = False
        self._closing = False
        self._close_lock = threading.RLock()
        self._serve_thread: threading.Thread | None = None
        self._stop = threading.Event()
        self._serve_join_timeout = 1.0

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
                "api": "2",
                "auth": "hmac-sha256-v1",
            },
            server=f"{hostname}.local.",
        )
        self._zeroconf = Zeroconf()
        self._zeroconf.register_service(self._service_info)

    def serve_forever(self) -> None:
        with self._close_lock:
            if self._closing or self._closed:
                raise RuntimeError("companion service is closing")
            self._stop.clear()
            self._serve_thread = threading.current_thread()
        try:
            if self.usb_poller is not None:
                self.usb_poller.start()
            self._start_advertising()
            while not self._stop.is_set():
                self.server.handle_request()
        finally:
            try:
                with self._close_lock:
                    externally_closing = self._closing
                if not externally_closing:
                    self.close()
            finally:
                with self._close_lock:
                    if self._serve_thread is threading.current_thread():
                        self._serve_thread = None

    def close(self) -> None:
        with self._close_lock:
            if self._closed:
                return
            self._closing = True
            self._stop.set()
            serving = self._serve_thread
        if (
            serving is not None
            and serving is not threading.current_thread()
            and serving.is_alive()
        ):
            serving.join(timeout=self._serve_join_timeout)
            if serving.is_alive():
                raise RuntimeError(
                    "companion shutdown is incomplete; service startup or request loop is still stopping"
                )
        with self._close_lock:
            if self._closed:
                return
            if self.usb_poller is not None:
                self.usb_poller.request_close()
            workers_closed = self.registry.close()
            usb_closed = self.usb_poller is None or self.usb_poller.close()
            if not usb_closed or not workers_closed:
                raise RuntimeError(
                    "companion shutdown is incomplete; active USB or sync work is still stopping"
                )
            if self._zeroconf is not None and self._service_info is not None:
                self._zeroconf.unregister_service(self._service_info)
                self._zeroconf.close()
                self._zeroconf = None
                self._service_info = None
            self.server.server_close()
            self._closed = True
