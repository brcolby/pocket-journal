from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from datetime import datetime, timedelta, timezone
from io import StringIO
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import Mock, patch
import json
import unittest

from pocket_journal_partner import cli
from pocket_journal_partner.config import DeviceProfile, PartnerConfig
from pocket_journal_partner.device import (
    DeviceClient,
    DeviceError,
    DeviceRequestTimeout,
    SerialDeviceClient,
    SerialPortAmbiguous,
    SerialPortNotFound,
)


def _write_config(root: str, *profiles: DeviceProfile) -> None:
    PartnerConfig(devices={profile.device_id: profile for profile in profiles}).save(
        Path(root) / "config.json"
    )


class DeviceDiscoveryTests(unittest.TestCase):
    def test_single_configured_device_is_selected_without_network_scan(self) -> None:
        with TemporaryDirectory() as tmp:
            _write_config(tmp, DeviceProfile("pj-only", "http://192.0.2.2", "paired-token"))
            args = cli.build_parser().parse_args(["settings", "get", "--data-dir", tmp])
            with patch("pocket_journal_partner.cli._discovered_lan_devices") as discover:
                device_id, client = cli._client_from_args(args)

        self.assertEqual(device_id, "pj-only")
        self.assertIsInstance(client, DeviceClient)
        self.assertEqual(client.base_url, "http://192.0.2.2")
        discover.assert_not_called()

    def test_mdns_completes_a_usb_provisioned_profile(self) -> None:
        with TemporaryDirectory() as tmp:
            _write_config(tmp, DeviceProfile("pj-network", token="paired-token"))
            args = cli.build_parser().parse_args(["recordings", "list", "--data-dir", tmp])
            with patch("pocket_journal_partner.cli._discovered_lan_devices", return_value=[{
                "device_id": "pj-network",
                "base_url": "http://192.0.2.3",
                "name": "pj-network._pocket-journal._tcp.local",
                "host": "pj-network.local",
            }]):
                device_id, client = cli._client_from_args(args)

        self.assertEqual(device_id, "pj-network")
        self.assertEqual(client.base_url, "http://192.0.2.3")

    def test_network_only_discovery_uses_an_explicit_token(self) -> None:
        with TemporaryDirectory() as tmp:
            args = cli.build_parser().parse_args([
                "settings", "get", "--data-dir", tmp, "--token", "one-command-token",
            ])
            with patch("pocket_journal_partner.cli._discovered_lan_devices", return_value=[{
                "device_id": "pj-mdns",
                "base_url": "http://192.0.2.4",
            }]):
                device_id, client = cli._client_from_args(args)

        self.assertEqual(device_id, "pj-mdns")
        self.assertEqual(client.token, "one-command-token")

    def test_explicit_device_wins_over_other_profiles(self) -> None:
        with TemporaryDirectory() as tmp:
            _write_config(
                tmp,
                DeviceProfile("pj-a", "http://192.0.2.5", "token-a"),
                DeviceProfile("pj-b", "http://192.0.2.6", "token-b"),
            )
            args = cli.build_parser().parse_args([
                "settings", "get", "--data-dir", tmp, "--device", "pj-b",
            ])
            with patch("pocket_journal_partner.cli._discovered_lan_devices") as discover:
                device_id, client = cli._client_from_args(args)

        self.assertEqual(device_id, "pj-b")
        self.assertEqual(client.base_url, "http://192.0.2.6")
        discover.assert_not_called()

    def test_multiple_configured_devices_have_deterministic_error(self) -> None:
        with TemporaryDirectory() as tmp:
            _write_config(
                tmp,
                DeviceProfile("pj-z", "http://192.0.2.7", "token-z"),
                DeviceProfile("pj-a", "http://192.0.2.8", "token-a"),
            )
            args = cli.build_parser().parse_args(["settings", "get", "--data-dir", tmp])
            with patch("pocket_journal_partner.cli._discovered_lan_devices", return_value=[]):
                with self.assertRaisesRegex(DeviceError, "pj-a, pj-z"):
                    cli._client_from_args(args)

    def test_no_usb_or_lan_candidate_is_actionable(self) -> None:
        with TemporaryDirectory() as tmp:
            args = cli.build_parser().parse_args(["device", "status", "--data-dir", tmp])
            with patch("pocket_journal_partner.cli.resolve_serial_port", side_effect=SerialPortNotFound("none")):
                with patch("pocket_journal_partner.cli._discovered_lan_devices", return_value=[]):
                    with self.assertRaisesRegex(DeviceError, "no paired or discoverable"):
                        cli._control_client_from_args(args)

    def test_control_command_falls_back_to_single_configured_lan_device(self) -> None:
        with TemporaryDirectory() as tmp:
            _write_config(tmp, DeviceProfile("pj-lan", "http://192.0.2.9", "paired-token"))
            args = cli.build_parser().parse_args(["device", "status", "--data-dir", tmp])
            with patch("pocket_journal_partner.cli.resolve_serial_port", side_effect=SerialPortNotFound("none")):
                device_id, client = cli._control_client_from_args(args)

        self.assertEqual(device_id, "pj-lan")
        self.assertIsInstance(client, DeviceClient)

    def test_explicit_usb_transport_does_not_fall_back_to_lan(self) -> None:
        with TemporaryDirectory() as tmp:
            _write_config(tmp, DeviceProfile("pj-lan", "http://192.0.2.9", "paired-token"))
            args = cli.build_parser().parse_args([
                "device", "status", "--transport", "usb", "--data-dir", tmp,
            ])
            with patch(
                "pocket_journal_partner.cli.resolve_serial_port",
                side_effect=SerialPortNotFound("no USB-C device found"),
            ):
                with self.assertRaisesRegex(SerialPortNotFound, "no USB-C"):
                    cli._control_client_from_args(args)

    def test_ambiguous_usb_ports_do_not_silently_fall_back_to_lan(self) -> None:
        with TemporaryDirectory() as tmp:
            _write_config(tmp, DeviceProfile("pj-lan", "http://192.0.2.10", "paired-token"))
            args = cli.build_parser().parse_args(["device", "status", "--data-dir", tmp])
            with patch(
                "pocket_journal_partner.cli.resolve_serial_port",
                side_effect=SerialPortAmbiguous("multiple USB serial ports found"),
            ):
                with self.assertRaisesRegex(DeviceError, "multiple USB"):
                    cli._control_client_from_args(args)


class AutomaticTimeSyncTests(unittest.TestCase):
    def test_sync_uses_explicit_local_time_and_reports_utc_anchor(self) -> None:
        local = datetime(2026, 7, 14, 8, 42, 37,
                         tzinfo=timezone(timedelta(hours=-7)))
        client = Mock(spec=SerialDeviceClient)
        client.put_time.return_value = {
            "hour": 8,
            "minute": 42,
            "second": 37,
            "year": 2026,
            "month": 7,
            "day": 14,
            "utc_offset_minutes": -420,
        }

        result = cli._sync_time_from_host(client, local)

        client.put_time.assert_called_once_with(8, 42, 7, 14, 2026, -420, 37)
        self.assertEqual(result["state"], "synced")
        self.assertTrue(result["validated"])
        self.assertEqual(result["precision_seconds"], 1)
        self.assertEqual(result["host_local"], "2026-07-14T08:42:37-07:00")
        self.assertEqual(result["host_utc"], "2026-07-14T15:42:37Z")
        self.assertEqual(result["utc_offset_minutes"], -420)

    def test_sync_rejects_ambiguous_or_subminute_host_offsets(self) -> None:
        client = Mock(spec=SerialDeviceClient)

        with self.assertRaisesRegex(DeviceError, "must include a UTC offset"):
            cli._sync_time_from_host(client, datetime(2026, 7, 14, 8, 42))
        with self.assertRaisesRegex(DeviceError, "whole minutes"):
            cli._sync_time_from_host(
                client,
                datetime(2026, 7, 14, 8, 42, tzinfo=timezone(timedelta(seconds=30))),
            )

        client.put_time.assert_not_called()

    def test_sync_rejects_an_unvalidated_device_response(self) -> None:
        local = datetime(2026, 7, 14, 8, 42, tzinfo=timezone.utc)
        client = Mock(spec=SerialDeviceClient)
        client.put_time.return_value = {"hour": 7, "minute": 0}

        with self.assertRaisesRegex(DeviceError, "could not be validated"):
            cli._sync_time_from_host(client, local)

    def test_sync_rejects_a_mismatched_echoed_utc_offset(self) -> None:
        local = datetime(2026, 7, 14, 8, 42, tzinfo=timezone(timedelta(hours=-7)))
        client = Mock(spec=SerialDeviceClient)
        client.put_time.return_value = {
            "hour": 8,
            "minute": 42,
            "second": 0,
            "year": 2026,
            "month": 7,
            "day": 14,
            "utc_offset_minutes": 0,
        }

        with self.assertRaisesRegex(DeviceError, "could not be validated"):
            cli._sync_time_from_host(client, local)

    def test_sync_rejects_a_missing_echoed_utc_offset(self) -> None:
        local = datetime(2026, 7, 14, 8, 42, tzinfo=timezone(timedelta(hours=-7)))
        client = Mock(spec=SerialDeviceClient)
        client.put_time.return_value = {
            "hour": 8,
            "minute": 42,
            "second": 0,
            "year": 2026,
            "month": 7,
            "day": 14,
        }

        with self.assertRaisesRegex(DeviceError, "could not be validated"):
            cli._sync_time_from_host(client, local)

    def test_status_repairs_the_exact_legacy_offset_sentinel_once(self) -> None:
        local = datetime(2026, 7, 15, 8, 42, tzinfo=timezone(timedelta(hours=-7)))
        client = Mock(spec=SerialDeviceClient)
        client.put_time.return_value = {
            "hour": 8,
            "minute": 42,
            "second": 0,
            "year": 2026,
            "month": 7,
            "day": 15,
            "utc_offset_minutes": -420,
        }
        status = {
            "capabilities": {"time.write": True},
            "time_sync": {
                "civil_time_semantics": "unconfigured",
                "publication": "timezone_required",
                "utc_offset_minutes": None,
            },
        }

        repair = cli._repair_legacy_time_offset(client, status, local)

        self.assertEqual(repair["state"], "repaired")
        self.assertEqual(repair["reason"], "legacy_missing_utc_offset")
        self.assertEqual(repair["sync"]["utc_offset_minutes"], -420)
        client.put_time.assert_called_once_with(8, 42, 7, 15, 2026, -420, 0)

    def test_status_time_repair_is_a_noop_for_every_other_state(self) -> None:
        cases = (
            {},
            {"time_sync": None},
            {"time_sync": {
                "civil_time_semantics": "unconfigured",
                "publication": "timezone_required",
            }},
            {"time_sync": {
                "civil_time_semantics": "fixed_utc_offset",
                "publication": "timezone_required",
                "utc_offset_minutes": None,
            }},
            {"time_sync": {
                "civil_time_semantics": "unconfigured",
                "publication": "published",
                "utc_offset_minutes": None,
            }},
            {"time_sync": {
                "civil_time_semantics": "unconfigured",
                "publication": "timezone_required",
                "utc_offset_minutes": 0,
            }},
        )

        for status in cases:
            with self.subTest(status=status):
                client = Mock(spec=SerialDeviceClient)

                self.assertIsNone(cli._repair_legacy_time_offset(client, status))

                client.put_time.assert_not_called()

    def test_status_reports_one_failed_repair_without_retrying(self) -> None:
        local = datetime(2026, 7, 15, 8, 42, tzinfo=timezone.utc)
        client = Mock(spec=SerialDeviceClient)
        client.put_time.side_effect = DeviceRequestTimeout("time write timed out")
        status = {
            "capabilities": {"time.write": True},
            "time_sync": {
                "civil_time_semantics": "unconfigured",
                "publication": "timezone_required",
                "utc_offset_minutes": None,
            },
        }

        repair = cli._repair_legacy_time_offset(client, status, local)

        self.assertEqual(repair, {
            "state": "failed",
            "reason": "legacy_missing_utc_offset",
            "error": "time write timed out",
            "retryable": True,
            "retry_command": "pj device sync-time",
        })
        client.put_time.assert_called_once_with(8, 42, 7, 15, 2026, 0, 0)

    def test_status_time_repair_requires_an_explicit_time_write_capability(self) -> None:
        sentinel = {
            "time_sync": {
                "civil_time_semantics": "unconfigured",
                "publication": "timezone_required",
                "utc_offset_minutes": None,
            },
        }
        for capabilities in (None, {}, {"time.write": False}, []):
            with self.subTest(capabilities=capabilities):
                status = dict(sentinel)
                if capabilities is not None:
                    status["capabilities"] = capabilities
                client = Mock(spec=SerialDeviceClient)

                repair = cli._repair_legacy_time_offset(client, status)

                self.assertEqual(repair["state"], "unsupported")
                client.put_time.assert_not_called()

    def test_sync_reanchors_stale_and_already_valid_clocks_without_a_preflight_read(self) -> None:
        local = datetime(2026, 7, 14, 8, 42, tzinfo=timezone.utc)
        for reported_time in (
            {"hour": 1, "minute": 2, "year": 2024, "month": 1, "day": 1},
            {"hour": 8, "minute": 42, "year": 2026, "month": 7, "day": 14},
        ):
            with self.subTest(reported_time=reported_time):
                client = Mock(spec=DeviceClient)
                client.get_time.return_value = reported_time
                client.put_time.return_value = {
                    "hour": 8,
                    "minute": 42,
                    "second": 0,
                    "year": 2026,
                    "month": 7,
                    "day": 14,
                    "utc_offset_minutes": 0,
                }

                result = cli._sync_time_from_host(client, local)

                self.assertEqual(result["state"], "synced")
                client.get_time.assert_not_called()
                client.put_time.assert_called_once_with(8, 42, 7, 14, 2026, 0, 0)

    def test_usb_provisioning_syncs_time_automatically(self) -> None:
        with TemporaryDirectory() as tmp:
            serial = Mock(spec=SerialDeviceClient)
            serial.provision_wifi.return_value = {"device_id": "pj-usb"}
            serial.put_time.side_effect = lambda hour, minute, month, day, year, utc_offset, second: {
                "hour": hour,
                "minute": minute,
                "second": second,
                "year": year,
                "month": month,
                "day": day,
                "utc_offset_minutes": utc_offset,
            }
            stdout = StringIO()
            with patch("pocket_journal_partner.cli.resolve_serial_port", return_value="/dev/cu.test"):
                with patch("pocket_journal_partner.cli.SerialDeviceClient", return_value=serial):
                    with redirect_stdout(stdout):
                        exit_code = cli.main([
                            "provision", "--ssid", "Lab", "--password", "test-password",
                            "--data-dir", tmp,
                        ])

        self.assertEqual(exit_code, 0)
        serial.put_time.assert_called_once()
        self.assertEqual(json.loads(stdout.getvalue())["time_sync"]["state"], "synced")

    def test_time_timeout_preserves_token_and_is_structurally_retryable(self) -> None:
        with TemporaryDirectory() as tmp:
            serial = Mock(spec=SerialDeviceClient)
            serial.provision_wifi.return_value = {"device_id": "pj-usb"}
            serial.put_time.side_effect = DeviceRequestTimeout("time transport unavailable")
            stdout = StringIO()
            with patch("pocket_journal_partner.cli.resolve_serial_port", return_value="/dev/cu.test"):
                with patch("pocket_journal_partner.cli.SerialDeviceClient", return_value=serial):
                    with patch("pocket_journal_partner.cli.secrets.token_urlsafe", return_value="stable-token"):
                        with redirect_stdout(stdout):
                            exit_code = cli.main([
                                "provision", "--ssid", "Lab", "--password", "test-password",
                                "--data-dir", tmp,
                            ])
            stored = json.loads((Path(tmp) / "config.json").read_text(encoding="utf-8"))

        self.assertEqual(exit_code, 0)
        self.assertIn("pj-usb", stored["devices"])
        self.assertEqual(stored["devices"]["pj-usb"]["token"], "stable-token")
        serial.provision_wifi.assert_called_once_with("Lab", "test-password", "stable-token")
        result = json.loads(stdout.getvalue())
        self.assertEqual(result["time_sync"]["state"], "failed")
        self.assertTrue(result["time_sync"]["retryable"])
        self.assertEqual(result["time_sync"]["retry_command"], "pj device sync-time")
        self.assertNotIn("test-password", stdout.getvalue())

    def test_failed_provisioning_time_write_retries_without_rotating_profile(self) -> None:
        serial = Mock(spec=SerialDeviceClient)
        serial.provision_wifi.return_value = {"device_id": "pj-usb"}
        serial.put_time.side_effect = [
            DeviceRequestTimeout("first write timed out"),
            {"hour": 8, "minute": 42, "second": 0, "year": 2026,
             "month": 7, "day": 14, "utc_offset_minutes": -420},
        ]

        with TemporaryDirectory() as tmp:
            stdout = StringIO()
            with patch("pocket_journal_partner.cli.resolve_serial_port", return_value="/dev/cu.test"):
                with patch("pocket_journal_partner.cli.SerialDeviceClient", return_value=serial):
                    with patch("pocket_journal_partner.cli.secrets.token_urlsafe", return_value="stable-token"):
                        with redirect_stdout(stdout):
                            self.assertEqual(cli.main([
                                "provision", "--ssid", "Lab", "--password", "test-password",
                                "--data-dir", tmp,
                            ]), 0)
            before_retry = (Path(tmp) / "config.json").read_bytes()
            retry = cli._sync_time_from_host(
                serial,
                datetime(2026, 7, 14, 8, 42, tzinfo=timezone(timedelta(hours=-7))),
            )
            after_retry = (Path(tmp) / "config.json").read_bytes()

        self.assertEqual(retry["state"], "synced")
        self.assertEqual(before_retry, after_retry)
        serial.provision_wifi.assert_called_once()
        self.assertEqual(serial.put_time.call_count, 2)


class RemovedLegacyCommandTests(unittest.TestCase):
    def test_primary_help_lists_only_supported_commands(self) -> None:
        help_text = cli.build_parser().format_help()

        self.assertIn(
            "{provision,discover,device,firmware,sync,companion,library,transcription,settings,recordings}",
            help_text,
        )
        for command in ("calendar", "home", "static-art"):
            self.assertNotIn(command, help_text)

    def test_removed_commands_are_rejected(self) -> None:
        parser = cli.build_parser()
        for command in ("calendar", "home", "static-art"):
            with self.subTest(command=command), redirect_stderr(StringIO()):
                with self.assertRaisesRegex(SystemExit, "2"):
                    parser.parse_args([command])


class TransportSelectionTests(unittest.TestCase):
    def test_discover_reports_usb_and_lan_without_opening_usb(self) -> None:
        stdout = StringIO()
        with patch("pocket_journal_partner.cli.discover_serial_ports", return_value=["/dev/cu.test"]):
            with patch("pocket_journal_partner.cli.discover_mdns", return_value=[{
                "device_id": "pj-lan", "base_url": "http://192.0.2.4"
            }]):
                with redirect_stdout(stdout):
                    self.assertEqual(cli.main(["discover"]), 0)

        payload = json.loads(stdout.getvalue())
        self.assertEqual(payload["count"], 2)
        self.assertEqual(payload["devices"][0], {"transport": "usb", "port": "/dev/cu.test"})
        self.assertEqual(payload["devices"][1]["transport"], "lan")

    def test_explicit_lan_transport_never_probes_usb(self) -> None:
        with TemporaryDirectory() as tmp:
            _write_config(tmp, DeviceProfile("pj-lan", "http://192.0.2.9", "paired-token"))
            args = cli.build_parser().parse_args([
                "device", "status", "--transport", "lan", "--data-dir", tmp,
            ])
            with patch("pocket_journal_partner.cli.resolve_serial_port") as resolve:
                device_id, client = cli._control_client_from_args(args)

        self.assertEqual(device_id, "pj-lan")
        self.assertIsInstance(client, DeviceClient)
        resolve.assert_not_called()

    def test_explicit_usb_transport_rejects_network_credentials(self) -> None:
        args = cli.build_parser().parse_args([
            "device", "status", "--transport", "usb", "--base-url", "http://device",
            "--token", "secret",
        ])

        with self.assertRaisesRegex(DeviceError, "cannot be used"):
            cli._control_client_from_args(args)

    def test_sync_defaults_to_cpu_only_whisper_cpp_and_identifies_usb_device(self) -> None:
        args = cli.build_parser().parse_args(["sync", "--transport", "usb"])
        client = Mock(spec=SerialDeviceClient)
        client.status.return_value = {"api_version": 1, "device_id": "pj-usb"}

        self.assertEqual(args.backend, "whisper-cpp")
        with patch(
            "pocket_journal_partner.cli._session_from_args",
            return_value=SimpleNamespace(
                device_id="usb",
                client=client,
                status=lambda: client.status(),
            ),
        ):
            session = cli._sync_session_from_args(args)
        self.assertEqual(session.device_id, "pj-usb")
        self.assertIs(session.client, client)


class UsbRecoveryCliTests(unittest.TestCase):
    def test_stop_interval_uses_usb_and_reports_persistent_reset(self) -> None:
        client = Mock(spec=SerialDeviceClient)
        client.reset_interval.return_value = {
            "command": "PJ_INTERVAL_RESET",
            "silenced": True,
            "reset": True,
            "persisted": True,
        }
        stdout = StringIO()

        with patch("pocket_journal_partner.cli.resolve_serial_port", return_value="/dev/cu.test"):
            with patch("pocket_journal_partner.cli.SerialDeviceClient", return_value=client) as serial_client:
                with redirect_stdout(stdout):
                    exit_code = cli.main([
                        "device", "stop-interval", "--timeout", "4",
                    ])

        self.assertEqual(exit_code, 0)
        serial_client.assert_called_once_with(
            "/dev/cu.test", baudrate=115200, timeout=4.0,
        )
        client.reset_interval.assert_called_once_with()
        payload = json.loads(stdout.getvalue())
        self.assertEqual(payload["transport"], "usb")
        self.assertTrue(payload["result"]["persisted"])

    def test_recovery_autodetects_usb_and_emits_a_credential_safe_report(self) -> None:
        client = Mock(spec=SerialDeviceClient)
        client.recover_usb.return_value = {
            "port": "/dev/cu.test",
            "initial_state": "rom_download",
            "recovery_attempted": True,
            "recovered": True,
            "final_state": "application",
            "status": {"device_id": "pj-test", "token": "private-token"},
            "action": "normal application boot restored",
        }
        stdout = StringIO()

        with patch("pocket_journal_partner.cli.resolve_serial_port", return_value="/dev/cu.test") as resolve:
            with patch("pocket_journal_partner.cli.SerialDeviceClient", return_value=client) as serial_client:
                with redirect_stdout(stdout):
                    exit_code = cli.main(["device", "usb-recover", "--timeout", "4"])

        self.assertEqual(exit_code, 0)
        resolve.assert_called_once_with(None)
        serial_client.assert_called_once_with("/dev/cu.test", baudrate=115200, timeout=4.0)
        client.recover_usb.assert_called_once_with(probe_only=False)
        payload = json.loads(stdout.getvalue())
        self.assertEqual(payload["transport"], "usb")
        self.assertEqual(payload["result"]["final_state"], "application")
        self.assertEqual(payload["result"]["status"]["token"], "[redacted]")
        self.assertNotIn("private-token", stdout.getvalue())


if __name__ == "__main__":
    unittest.main()
