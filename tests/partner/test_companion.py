from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
from urllib import error, request
import json
import secrets
import threading
import time
import unittest

from pocket_journal_partner import cli
from pocket_journal_partner.companion import (
    COMPANION_SERVICE_TYPE,
    COMPANION_STATUS_PATH,
    COMPANION_SYNC_PATH,
    CompanionHTTPServer,
    CompanionJobRegistry,
    CompanionService,
    CompanionUSBPoller,
    _handler_for,
    local_ipv4_addresses,
)
from pocket_journal_partner.companion_auth import (
    request_envelope,
    scoped_data_token,
    verify_response_envelope,
)
from pocket_journal_partner.config import DeviceProfile
from pocket_journal_partner.device import DeviceError
from pocket_journal_partner.storage import PartnerStore


class FakeBackend:
    def fingerprint(self) -> dict[str, str]:
        return {"backend": "fake"}

    def transcribe(self, audio_path: Path) -> dict[str, str]:
        _ = audio_path
        return {"text": "test"}


class CompanionTests(unittest.TestCase):
    TOKEN = "paired-token-0123456789"

    def test_mdns_service_label_is_dns_sd_compatible(self) -> None:
        service_label = COMPANION_SERVICE_TYPE.split(".", 1)[0]
        self.assertEqual(COMPANION_SERVICE_TYPE, "_pj-companion._tcp.local.")
        self.assertLessEqual(len(service_label), 15)

    def _request(
        self,
        base_url: str,
        method: str,
        path: str,
        token: str,
        *,
        action: str,
        operation_id: str,
        generation: int,
        requested_ms: int,
        payload: dict[str, object] | None = None,
    ) -> tuple[int, dict[str, object]]:
        nonce = secrets.token_hex(16)
        envelope = payload or request_envelope(
            token, action, "pj-test", "127.0.0.1", operation_id, generation,
            requested_ms, nonce,
        )
        data = json.dumps(envelope).encode("utf-8")
        req = request.Request(
            base_url + path,
            data=data,
            method=method,
            headers={"Content-Type": "application/json"},
        )
        with request.urlopen(req, timeout=2) as response:
            response_payload = json.loads(response.read())
            return response.status, verify_response_envelope(
                response_payload, token,
                expected_device_id="pj-test",
                expected_operation_id=operation_id,
                expected_generation=generation,
                expected_requested_ms=requested_ms,
                expected_action=action,
                expected_nonce=str(envelope["nonce"]),
            )

    def test_authenticated_request_runs_pipeline_and_reports_progress(self) -> None:
        calls: list[tuple[str, str]] = []

        def client_factory(base_url: str, token: str):
            calls.append((base_url, token))
            return object()

        def sync_runner(device_id, client, store, backend, *, progress):
            _ = device_id, client, store, backend
            progress({"event": "listed", "total": 2})
            progress({"event": "item_started", "index": 0, "total": 2, "audio_id": "a"})
            progress({"event": "item_complete", "index": 0, "total": 2,
                      "audio_id": "a", "status": "skipped"})
            progress({"event": "item_started", "index": 1, "total": 2, "audio_id": "b"})
            progress({"event": "item_complete", "index": 1, "total": 2,
                      "audio_id": "b", "status": "uploaded"})
            progress({"event": "complete", "total": 2})
            return [{"audio_id": "b", "status": "uploaded"}]

        with TemporaryDirectory() as tmp:
            registry = CompanionJobRegistry(
                DeviceProfile("pj-test", token=self.TOKEN),
                PartnerStore(Path(tmp)),
                FakeBackend(),  # type: ignore[arg-type]
                client_factory=client_factory,  # type: ignore[arg-type]
                sync_runner=sync_runner,
            )
            server = CompanionHTTPServer(("127.0.0.1", 0), _handler_for(registry))
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            base_url = f"http://127.0.0.1:{server.server_address[1]}"
            try:
                status, started = self._request(
                    base_url,
                    "POST",
                    COMPANION_SYNC_PATH,
                    self.TOKEN,
                    action="start",
                    operation_id="pj-test-00000001",
                    generation=1,
                    requested_ms=1000,
                )
                self.assertIn(status, {200, 202})
                deadline = time.monotonic() + 2
                progress = started
                while progress["state"] not in {"succeeded", "failed"}:
                    self.assertLess(time.monotonic(), deadline)
                    _, progress = self._request(
                        base_url,
                        "POST",
                        COMPANION_STATUS_PATH,
                        self.TOKEN,
                        action="status",
                        operation_id="pj-test-00000001",
                        generation=1,
                        requested_ms=1000,
                    )
                    time.sleep(0.01)
                self.assertEqual(progress["state"], "succeeded")
                self.assertEqual(progress["generation"], 1)
                self.assertEqual(progress["total"], 2)
                self.assertEqual(progress["pending"], 0)
                self.assertEqual(progress["transferred"], 2)
                self.assertEqual(progress["failed"], 0)

                _, repeated = self._request(
                    base_url,
                    "POST",
                    COMPANION_SYNC_PATH,
                    self.TOKEN,
                    action="start",
                    operation_id="pj-test-00000001",
                    generation=1,
                    requested_ms=1000,
                )
                self.assertEqual(repeated["operation_id"], "pj-test-00000001")
                self.assertEqual(len(calls), 1)
                self.assertEqual(calls[0], (
                    "http://127.0.0.1:80",
                    scoped_data_token(
                        self.TOKEN, "pj-test", "pj-test-00000001", 1, 1000
                    ),
                ))
            finally:
                server.shutdown()
                server.server_close()
                thread.join(timeout=2)

    def test_listener_rejects_wrong_device_and_token(self) -> None:
        with TemporaryDirectory() as tmp:
            registry = CompanionJobRegistry(
                DeviceProfile("pj-test", token=self.TOKEN),
                PartnerStore(Path(tmp)),
                FakeBackend(),  # type: ignore[arg-type]
                worker_factory=lambda target: None,
            )
            server = CompanionHTTPServer(("127.0.0.1", 0), _handler_for(registry))
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            base_url = f"http://127.0.0.1:{server.server_address[1]}"
            try:
                with self.assertRaises(error.HTTPError) as wrong_token:
                    self._request(
                        base_url, "POST", COMPANION_SYNC_PATH,
                        "wrong-token-0123456789",
                        action="start", operation_id="request-1",
                        generation=1, requested_ms=1000,
                    )
                self.assertEqual(wrong_token.exception.code, 401)
                wrong_token.exception.close()
                with self.assertRaises(error.HTTPError) as wrong_device:
                    self._request(
                        base_url, "POST", COMPANION_SYNC_PATH, self.TOKEN,
                        action="start", operation_id="request-1",
                        generation=1, requested_ms=1000,
                        payload=request_envelope(
                            self.TOKEN, "start", "someone-else", "127.0.0.1",
                            "request-1", 1, 1000, secrets.token_hex(16),
                        ),
                    )
                self.assertEqual(wrong_device.exception.code, 401)
                wrong_device.exception.close()
                valid = request_envelope(
                    self.TOKEN, "start", "pj-test", "127.0.0.1",
                    "request-duplicate", 1, 1000, secrets.token_hex(16),
                )
                duplicate = json.dumps(valid, separators=(",", ":")).replace(
                    "{", "{\"version\":1,", 1
                ).encode("utf-8")
                req = request.Request(
                    base_url + COMPANION_SYNC_PATH, data=duplicate,
                    method="POST", headers={"Content-Type": "application/json"},
                )
                with self.assertRaises(error.HTTPError) as duplicate_error:
                    request.urlopen(req, timeout=2)
                self.assertEqual(duplicate_error.exception.code, 400)
                duplicate_error.exception.close()
            finally:
                server.shutdown()
                server.server_close()
                thread.join(timeout=2)

    def test_listener_reports_initial_replay_store_failure_as_retryable(self) -> None:
        class FailingStore(PartnerStore):
            def save_companion_state(self, device_id, state):  # type: ignore[no-untyped-def]
                _ = device_id, state
                raise OSError("disk unavailable")

        with TemporaryDirectory() as tmp:
            registry = CompanionJobRegistry(
                DeviceProfile("pj-test", token=self.TOKEN),
                FailingStore(Path(tmp)), FakeBackend(),  # type: ignore[arg-type]
                worker_factory=lambda target: None,
            )
            server = CompanionHTTPServer(
                ("127.0.0.1", 0), _handler_for(registry)
            )
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                with self.assertRaises(error.HTTPError) as failed:
                    self._request(
                        f"http://127.0.0.1:{server.server_address[1]}",
                        "POST", COMPANION_SYNC_PATH, self.TOKEN,
                        action="start", operation_id="store-failed",
                        generation=1, requested_ms=1000,
                    )
                self.assertEqual(failed.exception.code, 503)
                failed.exception.close()
            finally:
                server.shutdown()
                server.server_close()
                thread.join(timeout=2)

    def test_failed_item_is_not_pending_for_device_display(self) -> None:
        def sync_runner(device_id, client, store, backend, *, progress):
            _ = device_id, client, store, backend
            progress({"event": "listed", "total": 1})
            progress({"event": "item_started", "index": 0, "total": 1, "audio_id": "bad"})
            progress({"event": "item_complete", "index": 0, "total": 1,
                      "audio_id": "bad", "status": "failed"})
            progress({"event": "complete", "total": 1})
            return [{"audio_id": "bad", "status": "failed"}]

        with TemporaryDirectory() as tmp:
            registry = CompanionJobRegistry(
                DeviceProfile("pj-test", token=self.TOKEN),
                PartnerStore(Path(tmp)),
                FakeBackend(),  # type: ignore[arg-type]
                client_factory=lambda base_url, token: object(),  # type: ignore[arg-type]
                sync_runner=sync_runner,
                worker_factory=lambda target: target(),
            )
            job, created = registry.start(
                "failed-request", "192.0.2.10", 4, 4000
            )

        self.assertTrue(created)
        self.assertEqual(job.snapshot()["state"], "failed")
        self.assertEqual(job.snapshot()["pending"], 0)
        self.assertEqual(job.snapshot()["failed"], 1)

    def test_duplicate_concurrent_start_creates_one_worker(self) -> None:
        workers: list[object] = []
        barrier = threading.Barrier(3)
        results: list[tuple[object, bool]] = []

        def start_worker(target):
            workers.append(target)

        with TemporaryDirectory() as tmp:
            registry = CompanionJobRegistry(
                DeviceProfile("pj-test", token=self.TOKEN),
                PartnerStore(Path(tmp)),
                FakeBackend(),  # type: ignore[arg-type]
                worker_factory=start_worker,
            )

            def start() -> None:
                barrier.wait()
                results.append(registry.start(
                    "same-request", "192.0.2.20", 7, 7000
                ))

            threads = [threading.Thread(target=start) for _ in range(2)]
            for thread in threads:
                thread.start()
            barrier.wait()
            for thread in threads:
                thread.join(timeout=2)

        self.assertEqual(len(workers), 1)
        self.assertEqual(sorted(created for _, created in results), [False, True])
        self.assertIs(results[0][0], results[1][0])

    def test_durable_replay_high_water_survives_restart(self) -> None:
        profile = DeviceProfile("pj-test", token=self.TOKEN)
        nonce_1 = "01" * 16
        nonce_2 = "02" * 16
        nonce_3 = "03" * 16
        with TemporaryDirectory() as tmp:
            store = PartnerStore(Path(tmp))
            first_workers: list[object] = []
            first = CompanionJobRegistry(
                profile, store, FakeBackend(),  # type: ignore[arg-type]
                worker_factory=lambda target: first_workers.append(target),
            )
            original, created = first.start(
                "pj-test-00000007", "192.0.2.10", 7, 7000, nonce_1
            )
            replayed, replay_created = first.start(
                "pj-test-00000007", "192.0.2.10", 7, 7000, nonce_1
            )
            self.assertTrue(created)
            self.assertFalse(replay_created)
            self.assertIs(replayed, original)
            self.assertEqual(len(first_workers), 1)

            restarted = CompanionJobRegistry(
                profile, store, FakeBackend(),  # type: ignore[arg-type]
                worker_factory=lambda target: None,
            )
            with self.assertRaisesRegex(ValueError, "replayed start"):
                restarted.start(
                    "pj-test-00000007", "192.0.2.10", 7, 7000, nonce_1
                )
            resumed = restarted.resume_status(
                "pj-test-00000007", "192.0.2.10", 7, 7000, nonce_2
            )
            self.assertIsNotNone(resumed)

            newer = CompanionJobRegistry(
                profile, store, FakeBackend(),  # type: ignore[arg-type]
                worker_factory=lambda target: None,
            )
            _, newer_created = newer.start(
                "pj-test-00000008", "192.0.2.10", 8, 8000, nonce_3
            )
            self.assertTrue(newer_created)
            final_restart = CompanionJobRegistry(
                profile, store, FakeBackend(),  # type: ignore[arg-type]
                worker_factory=lambda target: None,
            )
            with self.assertRaisesRegex(ValueError, "stale sync generation"):
                final_restart.start(
                    "pj-test-00000007", "192.0.2.10", 7, 7000, "04" * 16
                )

    def test_durable_terminal_replays_without_worker(self) -> None:
        profile = DeviceProfile("pj-test", token=self.TOKEN)
        with TemporaryDirectory() as tmp:
            store = PartnerStore(Path(tmp))
            first = CompanionJobRegistry(
                profile, store, FakeBackend(),  # type: ignore[arg-type]
                sync_runner=lambda *args, **kwargs: [],
                worker_factory=lambda target: target(),
            )
            terminal, created = first.start(
                "pj-test-00000009", "192.0.2.10", 9, 9000, "09" * 16
            )
            self.assertTrue(created)
            self.assertEqual(terminal.snapshot()["state"], "succeeded")
            workers: list[object] = []
            restarted = CompanionJobRegistry(
                profile, store, FakeBackend(),  # type: ignore[arg-type]
                worker_factory=lambda target: workers.append(target),
            )
            restored = restarted.resume_status(
                "pj-test-00000009", "192.0.2.10", 9, 9000, "0a" * 16
            )
            self.assertIsNotNone(restored)
            assert restored is not None
            self.assertEqual(restored.snapshot(), terminal.snapshot())
            self.assertEqual(workers, [])

    def test_terminal_is_not_exposed_until_replay_state_is_durable(self) -> None:
        class FailingTerminalStore(PartnerStore):
            def __init__(self, root: Path) -> None:
                super().__init__(root)
                self.save_calls = 0

            def save_companion_state(self, device_id, state):  # type: ignore[no-untyped-def]
                self.save_calls += 1
                if self.save_calls > 1:
                    raise OSError("disk unavailable")
                return super().save_companion_state(device_id, state)

        profile = DeviceProfile("pj-test", token=self.TOKEN)
        runs: list[str] = []

        def sync_runner(*args, **kwargs):  # type: ignore[no-untyped-def]
            _ = args, kwargs
            runs.append("run")
            return []

        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            failing_store = FailingTerminalStore(root)
            first = CompanionJobRegistry(
                profile, failing_store, FakeBackend(),  # type: ignore[arg-type]
                sync_runner=sync_runner,
                worker_factory=lambda target: target(),
            )
            job, _ = first.start(
                "pj-test-00000010", "192.0.2.10", 10, 10000, "10" * 16
            )
            self.assertEqual(job.snapshot()["state"], "running")
            self.assertIsNotNone(job.terminal_candidate)
            with self.assertRaisesRegex(RuntimeError, "not durable"):
                first.response_snapshot(job)
            durable = PartnerStore(root).load_companion_state("pj-test")
            self.assertIsNotNone(durable)
            assert durable is not None
            self.assertIsNone(durable["terminal"])

            restarted = CompanionJobRegistry(
                profile, PartnerStore(root), FakeBackend(),  # type: ignore[arg-type]
                sync_runner=sync_runner,
                worker_factory=lambda target: target(),
            )
            resumed = restarted.resume_status(
                "pj-test-00000010", "192.0.2.10", 10, 10000, "11" * 16
            )
            self.assertIsNotNone(resumed)
            assert resumed is not None
            self.assertEqual(
                restarted.response_snapshot(resumed)["state"], "succeeded"
            )
            self.assertEqual(runs, ["run", "run"])

    def test_lan_status_after_restart_resumes_then_replays_terminal(self) -> None:
        profile = DeviceProfile("pj-test", token=self.TOKEN)
        operation_id = "pj-test-00000012"
        with TemporaryDirectory() as tmp:
            store = PartnerStore(Path(tmp))
            first = CompanionJobRegistry(
                profile, store, FakeBackend(),  # type: ignore[arg-type]
                worker_factory=lambda target: None,
            )
            first.start(
                operation_id, "127.0.0.1", 12, 12000, "12" * 16
            )

            resumed = CompanionJobRegistry(
                profile, store, FakeBackend(),  # type: ignore[arg-type]
                sync_runner=lambda *args, **kwargs: [],
                worker_factory=lambda target: target(),
            )
            server = CompanionHTTPServer(
                ("127.0.0.1", 0), _handler_for(resumed)
            )
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                _, completed = self._request(
                    f"http://127.0.0.1:{server.server_address[1]}", "POST",
                    COMPANION_STATUS_PATH, self.TOKEN, action="status",
                    operation_id=operation_id, generation=12,
                    requested_ms=12000,
                )
                self.assertEqual(completed["state"], "succeeded")
            finally:
                server.shutdown()
                server.server_close()
                thread.join(timeout=2)

            replay_workers: list[object] = []
            replay = CompanionJobRegistry(
                profile, store, FakeBackend(),  # type: ignore[arg-type]
                worker_factory=lambda target: replay_workers.append(target),
            )
            server = CompanionHTTPServer(
                ("127.0.0.1", 0), _handler_for(replay)
            )
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                _, completed_again = self._request(
                    f"http://127.0.0.1:{server.server_address[1]}", "POST",
                    COMPANION_STATUS_PATH, self.TOKEN, action="status",
                    operation_id=operation_id, generation=12,
                    requested_ms=12000,
                )
                for field in (
                    "device_id", "operation_id", "generation", "requested_ms",
                    "state", "total", "pending", "transferred", "failed",
                    "error",
                ):
                    self.assertEqual(completed_again[field], completed[field])
                self.assertEqual(replay_workers, [])
            finally:
                server.shutdown()
                server.server_close()
                thread.join(timeout=2)

    def test_corrupt_durable_replay_state_fails_closed(self) -> None:
        profile = DeviceProfile("pj-test", token=self.TOKEN)
        with TemporaryDirectory() as tmp:
            store = PartnerStore(Path(tmp))
            path = store.companion_state_path(profile.device_id)
            path.parent.mkdir(parents=True)
            path.write_text("{not-json", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unreadable"):
                CompanionJobRegistry(
                    profile, store, FakeBackend(),  # type: ignore[arg-type]
                )

    def test_reprovisioned_token_starts_a_new_replay_epoch(self) -> None:
        with TemporaryDirectory() as tmp:
            store = PartnerStore(Path(tmp))
            old = CompanionJobRegistry(
                DeviceProfile("pj-test", token=self.TOKEN),
                store, FakeBackend(),  # type: ignore[arg-type]
                worker_factory=lambda target: None,
            )
            old.start(
                "pj-test-00000019", "192.0.2.10", 19, 19000, "19" * 16
            )
            replacement = CompanionJobRegistry(
                DeviceProfile("pj-test", token="replacement-token-987654321"),
                store, FakeBackend(),  # type: ignore[arg-type]
                worker_factory=lambda target: None,
            )
            _, created = replacement.start(
                "pj-test-00000001", "192.0.2.10", 1, 1000, "01" * 16
            )
            self.assertTrue(created)
            durable = store.load_companion_state("pj-test")
            self.assertIsNotNone(durable)
            assert durable is not None
            self.assertEqual(durable["generation"], 1)

    def test_incomplete_sync_result_is_terminal_failure(self) -> None:
        def sync_runner(device_id, client, store, backend, *, progress):
            _ = device_id, client, store, backend
            progress({"event": "listed", "total": 2})
            progress({"event": "item_complete", "status": "uploaded"})
            return [{"audio_id": "a", "status": "uploaded"}]

        with TemporaryDirectory() as tmp:
            registry = CompanionJobRegistry(
                DeviceProfile("pj-test", token=self.TOKEN),
                PartnerStore(Path(tmp)), FakeBackend(),  # type: ignore[arg-type]
                client_factory=lambda base_url, token: object(),  # type: ignore[arg-type]
                sync_runner=sync_runner,
                worker_factory=lambda target: target(),
            )
            job, _ = registry.start(
                "incomplete", "192.0.2.10", 11, 11000, "0b" * 16
            )
            snapshot = job.snapshot()
            self.assertEqual(snapshot["state"], "failed")
            self.assertEqual(snapshot["pending"], 1)
            self.assertIn("unresolved", snapshot["error"])

    def test_usb_poller_claims_and_reports_exact_generation(self) -> None:
        class FakeSerial:
            def __init__(self) -> None:
                self.progress: list[tuple[object, ...]] = []

            def companion_sync_status(self):
                return {
                    "device_id": "pj-test", "request_pending": True,
                    "claim_generation": 9, "operation_id": "pj-test-00000009",
                    "claim_requested_ms": 9000,
                }

            def companion_sync_claim(self, generation, operation_id):
                self.claim = (generation, operation_id)
                return {"claim_result": "started"}

            def companion_sync_progress(self, *args):
                self.progress.append(args)
                return {"request_pending": False}

            def list_audio(self):
                return []

        serial = FakeSerial()
        with TemporaryDirectory() as tmp:
            registry = CompanionJobRegistry(
                DeviceProfile("pj-test", token=self.TOKEN),
                PartnerStore(Path(tmp)),
                FakeBackend(),  # type: ignore[arg-type]
                sync_runner=lambda *args, **kwargs: [],
                worker_factory=lambda target: target(),
            )
            poller = CompanionUSBPoller(
                registry.profile,
                registry,
                port_resolver=lambda port: "/dev/cu.test",
                client_factory=lambda port: serial,  # type: ignore[arg-type]
            )
            result = poller.poll_once()

        self.assertIsNotNone(result)
        self.assertEqual(serial.claim, (9, "pj-test-00000009"))
        self.assertEqual(serial.progress[-1][0:3],
                         (9, "pj-test-00000009", "succeeded"))

    def test_usb_terminal_waits_for_companion_state_persistence(self) -> None:
        class TransientStore(PartnerStore):
            def __init__(self, root: Path) -> None:
                super().__init__(root)
                self.save_calls = 0

            def save_companion_state(self, device_id, state):  # type: ignore[no-untyped-def]
                self.save_calls += 1
                if self.save_calls in {2, 3}:
                    raise OSError("disk unavailable")
                return super().save_companion_state(device_id, state)

        class FakeSerial:
            def __init__(self) -> None:
                self.progress: list[tuple[object, ...]] = []

            def companion_sync_status(self):  # type: ignore[no-untyped-def]
                return {
                    "device_id": "pj-test", "request_pending": True,
                    "claim_generation": 13,
                    "operation_id": "pj-test-00000013",
                    "claim_requested_ms": 13000,
                }

            def companion_sync_claim(self, generation, operation_id):  # type: ignore[no-untyped-def]
                _ = generation, operation_id
                return {"claim_result": "attached"}

            def companion_sync_progress(self, *args):  # type: ignore[no-untyped-def]
                self.progress.append(args)
                return {"request_pending": False}

        serial = FakeSerial()
        with TemporaryDirectory() as tmp:
            store = TransientStore(Path(tmp))
            registry = CompanionJobRegistry(
                DeviceProfile("pj-test", token=self.TOKEN), store,
                FakeBackend(),  # type: ignore[arg-type]
                sync_runner=lambda *args, **kwargs: [],
                worker_factory=lambda target: target(),
            )
            poller = CompanionUSBPoller(
                registry.profile, registry,
                port_resolver=lambda port: "/dev/cu.test",
                client_factory=lambda port: serial,  # type: ignore[arg-type]
            )
            with self.assertRaisesRegex(DeviceError, "temporarily unavailable"):
                poller.poll_once()
            self.assertEqual(serial.progress, [])
            completed = poller.poll_once()
            self.assertIsNotNone(completed)
            assert completed is not None
            self.assertEqual(completed["state"], "succeeded")
            self.assertEqual(serial.progress[-1][2], "succeeded")
            durable = PartnerStore(Path(tmp)).load_companion_state("pj-test")
            self.assertIsNotNone(durable)
            assert durable is not None
            self.assertEqual(durable["terminal"]["state"], "succeeded")

    def test_companion_restart_reattaches_same_usb_operation(self) -> None:
        class FakeSerial:
            def __init__(self) -> None:
                self.terminal = False
                self.claims: list[tuple[int, str]] = []

            def companion_sync_status(self):
                return {
                    "device_id": "pj-test",
                    "request_pending": not self.terminal,
                    "claim_generation": 3 if not self.terminal else 0,
                    "claim_requested_ms": 3000,
                    "operation_id": "pj-test-00000003" if not self.terminal else "",
                }

            def companion_sync_claim(self, generation, operation_id):
                self.claims.append((generation, operation_id))
                return {"claim_result": "started" if len(self.claims) == 1 else "attached"}

            def companion_sync_progress(self, generation, operation_id, state,
                                        total, pending, transferred, failed,
                                        error=""):
                if state in {"succeeded", "failed"}:
                    self.terminal = True
                return {"request_pending": not self.terminal}

        serial = FakeSerial()
        profile = DeviceProfile("pj-test", token=self.TOKEN)
        with TemporaryDirectory() as tmp:
            first = CompanionJobRegistry(
                profile, PartnerStore(Path(tmp)), FakeBackend(),  # type: ignore[arg-type]
                worker_factory=lambda target: None,
            )
            CompanionUSBPoller(
                profile, first, port_resolver=lambda port: "/dev/cu.test",
                client_factory=lambda port: serial,  # type: ignore[arg-type]
            ).poll_once()
            restarted = CompanionJobRegistry(
                profile, PartnerStore(Path(tmp)), FakeBackend(),  # type: ignore[arg-type]
                sync_runner=lambda *args, **kwargs: [],
                worker_factory=lambda target: target(),
            )
            CompanionUSBPoller(
                profile, restarted, port_resolver=lambda port: "/dev/cu.test",
                client_factory=lambda port: serial,  # type: ignore[arg-type]
            ).poll_once()

        self.assertEqual(serial.claims, [
            (3, "pj-test-00000003"),
            (3, "pj-test-00000003"),
        ])
        self.assertTrue(serial.terminal)

    def test_registry_close_retains_blocked_worker_until_retry(self) -> None:
        started = threading.Event()
        release = threading.Event()

        def sync_runner(*args, **kwargs):  # type: ignore[no-untyped-def]
            _ = args, kwargs
            started.set()
            release.wait(2)
            return []

        with TemporaryDirectory() as tmp:
            registry = CompanionJobRegistry(
                DeviceProfile("pj-test", token=self.TOKEN),
                PartnerStore(Path(tmp)),
                FakeBackend(),  # type: ignore[arg-type]
                client_factory=lambda base_url, token: object(),  # type: ignore[arg-type]
                sync_runner=sync_runner,
            )
            registry.start("blocked", "192.0.2.20", 8, 8000)
            self.assertTrue(started.wait(1))
            self.assertFalse(registry.close(timeout=0.01))
            self.assertTrue(any(worker.is_alive() for worker in registry._workers))
            release.set()
            self.assertTrue(registry.close(timeout=1))

    def test_registry_close_cancels_active_serial_client_before_join(self) -> None:
        started = threading.Event()

        class CancellableSerial:
            def __init__(self) -> None:
                self.cancelled = threading.Event()
                self.close_calls = 0

            def close(self) -> None:
                self.close_calls += 1
                self.cancelled.set()

        serial = CancellableSerial()

        def sync_runner(device_id, client, store, backend, progress):  # type: ignore[no-untyped-def]
            _ = device_id, store, backend, progress
            started.set()
            self.assertTrue(client.cancelled.wait(2))
            raise DeviceError("serial transfer cancelled")

        with TemporaryDirectory() as tmp:
            registry = CompanionJobRegistry(
                DeviceProfile("pj-test", token=self.TOKEN),
                PartnerStore(Path(tmp)), FakeBackend(),  # type: ignore[arg-type]
                sync_runner=sync_runner,
            )
            registry.start_usb("usb-cancel", 9, 9000, serial)  # type: ignore[arg-type]
            self.assertTrue(started.wait(1))

            before = time.monotonic()
            self.assertTrue(registry.close(timeout=1))
            elapsed = time.monotonic() - before

        self.assertEqual(serial.close_calls, 1)
        self.assertLess(elapsed, 0.5)

    def test_service_close_propagates_incomplete_shutdown_and_can_retry(self) -> None:
        class RetryClose:
            def __init__(self) -> None:
                self.calls = 0

            def close(self):  # type: ignore[no-untyped-def]
                self.calls += 1
                return self.calls > 1

            def request_close(self) -> None:
                pass

        class Closed:
            def close(self):  # type: ignore[no-untyped-def]
                return True

        class Server:
            def __init__(self) -> None:
                self.closed = False

            def server_close(self) -> None:
                self.closed = True

        service = object.__new__(CompanionService)
        service._closed = False
        service._closing = False
        service._close_lock = threading.RLock()
        service._serve_thread = None
        service._stop = threading.Event()
        service._serve_join_timeout = 1.0
        service.usb_poller = RetryClose()
        service.registry = Closed()
        service._zeroconf = None
        service._service_info = None
        service.server = Server()
        with self.assertRaisesRegex(RuntimeError, "shutdown is incomplete"):
            service.close()
        self.assertFalse(service._closed)
        self.assertFalse(service.server.closed)
        service.close()
        self.assertTrue(service._closed)
        self.assertTrue(service.server.closed)

    def test_external_close_stops_live_server_loop(self) -> None:
        errors: list[BaseException] = []
        with TemporaryDirectory() as tmp:
            service = CompanionService(
                DeviceProfile("pj-test", token=self.TOKEN),
                PartnerStore(Path(tmp)), FakeBackend(),  # type: ignore[arg-type]
                host="127.0.0.1", port=0, advertise=False, usb=False,
            )

            def serve() -> None:
                try:
                    service.serve_forever()
                except BaseException as exc:
                    errors.append(exc)

            thread = threading.Thread(target=serve)
            thread.start()
            deadline = time.monotonic() + 2
            while service._serve_thread is None:
                self.assertLess(time.monotonic(), deadline)
                time.sleep(0.001)
            service.close()
            thread.join(timeout=2)
            self.assertFalse(thread.is_alive())
            self.assertTrue(service._closed)
            self.assertEqual(errors, [])

    def test_external_close_during_failed_startup_is_bounded_and_retryable(self) -> None:
        entered = threading.Event()
        release = threading.Event()
        errors: list[BaseException] = []
        with TemporaryDirectory() as tmp:
            service = CompanionService(
                DeviceProfile("pj-test", token=self.TOKEN),
                PartnerStore(Path(tmp)), FakeBackend(),  # type: ignore[arg-type]
                host="127.0.0.1", port=0, advertise=False, usb=False,
            )
            service._serve_join_timeout = 0.05

            def fail_advertising() -> None:
                entered.set()
                release.wait(2)
                raise RuntimeError("advertising failed")

            service._start_advertising = fail_advertising  # type: ignore[method-assign]

            def serve() -> None:
                try:
                    service.serve_forever()
                except BaseException as exc:
                    errors.append(exc)

            thread = threading.Thread(target=serve)
            thread.start()
            self.assertTrue(entered.wait(1))
            with self.assertRaisesRegex(RuntimeError, "startup or request loop"):
                service.close()
            self.assertFalse(service._closed)
            release.set()
            thread.join(timeout=2)
            self.assertFalse(thread.is_alive())
            service.close()
            self.assertTrue(service._closed)
            self.assertEqual(len(errors), 1)
            self.assertRegex(str(errors[0]), "advertising failed")

    def test_usb_close_finishes_inflight_command_without_reopening_port(self) -> None:
        entered = threading.Event()
        release = threading.Event()
        opens: list[str] = []
        claims: list[tuple[int, str]] = []

        class BlockingSerial:
            def companion_sync_status(self):  # type: ignore[no-untyped-def]
                entered.set()
                release.wait(2)
                return {
                    "device_id": "pj-test",
                    "request_pending": True,
                    "claim_generation": 12,
                    "claim_requested_ms": 12000,
                    "operation_id": "pj-test-00000012",
                }

            def companion_sync_claim(self, generation, operation_id):  # type: ignore[no-untyped-def]
                claims.append((generation, operation_id))
                return {"claim_result": "started"}

        with TemporaryDirectory() as tmp:
            registry = CompanionJobRegistry(
                DeviceProfile("pj-test", token=self.TOKEN),
                PartnerStore(Path(tmp)), FakeBackend(),  # type: ignore[arg-type]
            )

            def client_factory(port: str):  # type: ignore[no-untyped-def]
                opens.append(port)
                return BlockingSerial()

            poller = CompanionUSBPoller(
                registry.profile, registry,
                poll_interval=0.25,
                port_resolver=lambda port: "/dev/cu.test",
                client_factory=client_factory,  # type: ignore[arg-type]
            )
            poller.start()
            self.assertTrue(entered.wait(1))
            close_results: list[bool] = []
            closer = threading.Thread(
                target=lambda: close_results.append(poller.close())
            )
            closer.start()
            deadline = time.monotonic() + 1
            while not poller._closed:
                self.assertLess(time.monotonic(), deadline)
                time.sleep(0.001)
            release.set()
            closer.join(timeout=2)
            self.assertEqual(close_results, [True])
            opened_at_close = len(opens)
            time.sleep(0.35)
            self.assertEqual(len(opens), opened_at_close)
            self.assertEqual(opened_at_close, 1)
            self.assertEqual(claims, [])
            self.assertTrue(registry.close(timeout=1))

    def test_cli_exposes_foreground_companion_listener(self) -> None:
        args = cli.build_parser().parse_args(
            ["companion", "serve", "--device", "pj-test", "--port", "9000"]
        )
        self.assertIs(args.func, cli.cmd_companion_serve)
        self.assertEqual(args.device, "pj-test")
        self.assertEqual(args.port, 9000)
        self.assertFalse(args.no_mdns)
        self.assertFalse(args.no_usb)

    def test_local_address_discovery_never_advertises_wildcard_or_loopback(self) -> None:
        for address in local_ipv4_addresses():
            self.assertNotEqual(address, "0.0.0.0")
            self.assertFalse(address.startswith("127."))


if __name__ == "__main__":
    unittest.main()
