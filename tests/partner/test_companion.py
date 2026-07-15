from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
from urllib import error, request
import json
import threading
import time
import unittest

from pocket_journal_partner import cli
from pocket_journal_partner.companion import (
    COMPANION_SERVICE_TYPE,
    COMPANION_SYNC_PATH,
    CompanionHTTPServer,
    CompanionJobRegistry,
    CompanionUSBPoller,
    _handler_for,
    local_ipv4_addresses,
)
from pocket_journal_partner.config import DeviceProfile
from pocket_journal_partner.storage import PartnerStore


class FakeBackend:
    def fingerprint(self) -> dict[str, str]:
        return {"backend": "fake"}

    def transcribe(self, audio_path: Path) -> dict[str, str]:
        _ = audio_path
        return {"text": "test"}


class CompanionTests(unittest.TestCase):
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
        payload: dict[str, object] | None = None,
    ) -> tuple[int, dict[str, object]]:
        data = None if payload is None else json.dumps(payload).encode("utf-8")
        req = request.Request(
            base_url + path,
            data=data,
            method=method,
            headers={
                "Authorization": f"Bearer {token}",
                "Content-Type": "application/json",
            },
        )
        with request.urlopen(req, timeout=2) as response:
            return response.status, json.loads(response.read())

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
                DeviceProfile("pj-test", token="paired-token"),
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
                    "paired-token",
                    {"device_id": "pj-test", "generation": 1,
                     "request_id": "pj-test-00000001"},
                )
                self.assertIn(status, {200, 202})
                deadline = time.monotonic() + 2
                progress = started
                while progress["state"] not in {"succeeded", "failed"}:
                    self.assertLess(time.monotonic(), deadline)
                    _, progress = self._request(
                        base_url,
                        "GET",
                        "/v1/sync/pj-test-00000001",
                        "paired-token",
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
                    "paired-token",
                    {"device_id": "pj-test", "generation": 1,
                     "request_id": "pj-test-00000001"},
                )
                self.assertEqual(repeated["operation_id"], "pj-test-00000001")
                self.assertEqual(len(calls), 1)
                self.assertEqual(calls[0], ("http://127.0.0.1:80", "paired-token"))
            finally:
                server.shutdown()
                server.server_close()
                thread.join(timeout=2)

    def test_listener_rejects_wrong_device_and_token(self) -> None:
        with TemporaryDirectory() as tmp:
            registry = CompanionJobRegistry(
                DeviceProfile("pj-test", token="paired-token"),
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
                        base_url, "POST", COMPANION_SYNC_PATH, "wrong",
                        {"device_id": "pj-test", "generation": 1,
                         "request_id": "request-1"},
                    )
                self.assertEqual(wrong_token.exception.code, 401)
                wrong_token.exception.close()
                with self.assertRaises(error.HTTPError) as wrong_device:
                    self._request(
                        base_url, "POST", COMPANION_SYNC_PATH, "paired-token",
                        {"device_id": "someone-else", "generation": 1,
                         "request_id": "request-1"},
                    )
                self.assertEqual(wrong_device.exception.code, 404)
                wrong_device.exception.close()
            finally:
                server.shutdown()
                server.server_close()
                thread.join(timeout=2)

    def test_failed_item_remains_pending_for_device_display(self) -> None:
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
                DeviceProfile("pj-test", token="paired-token"),
                PartnerStore(Path(tmp)),
                FakeBackend(),  # type: ignore[arg-type]
                client_factory=lambda base_url, token: object(),  # type: ignore[arg-type]
                sync_runner=sync_runner,
                worker_factory=lambda target: target(),
            )
            job, created = registry.start("failed-request", "192.0.2.10", 4)

        self.assertTrue(created)
        self.assertEqual(job.snapshot()["state"], "failed")
        self.assertEqual(job.snapshot()["pending"], 1)
        self.assertEqual(job.snapshot()["failed"], 1)

    def test_duplicate_concurrent_start_creates_one_worker(self) -> None:
        workers: list[object] = []
        barrier = threading.Barrier(3)
        results: list[tuple[object, bool]] = []

        def start_worker(target):
            workers.append(target)

        with TemporaryDirectory() as tmp:
            registry = CompanionJobRegistry(
                DeviceProfile("pj-test", token="paired-token"),
                PartnerStore(Path(tmp)),
                FakeBackend(),  # type: ignore[arg-type]
                worker_factory=start_worker,
            )

            def start() -> None:
                barrier.wait()
                results.append(registry.start("same-request", "192.0.2.20", 7))

            threads = [threading.Thread(target=start) for _ in range(2)]
            for thread in threads:
                thread.start()
            barrier.wait()
            for thread in threads:
                thread.join(timeout=2)

        self.assertEqual(len(workers), 1)
        self.assertEqual(sorted(created for _, created in results), [False, True])
        self.assertIs(results[0][0], results[1][0])

    def test_usb_poller_claims_and_reports_exact_generation(self) -> None:
        class FakeSerial:
            def __init__(self) -> None:
                self.progress: list[tuple[object, ...]] = []

            def companion_sync_status(self):
                return {
                    "device_id": "pj-test", "request_pending": True,
                    "claim_generation": 9, "operation_id": "pj-test-00000009",
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
                DeviceProfile("pj-test", token="paired-token"),
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
                    "operation_id": "pj-test-00000003" if not self.terminal else "",
                }

            def companion_sync_claim(self, generation, operation_id):
                self.claims.append((generation, operation_id))
                return {"claim_result": "started" if len(self.claims) == 1 else "attached"}

            def companion_sync_progress(self, generation, operation_id, state,
                                        pending, transferred, failed, error=""):
                if state in {"succeeded", "failed"}:
                    self.terminal = True
                return {"request_pending": not self.terminal}

        serial = FakeSerial()
        profile = DeviceProfile("pj-test", token="paired-token")
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
