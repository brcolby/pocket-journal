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
        local = datetime(2026, 7, 14, 8, 42, tzinfo=timezone(timedelta(hours=-7)))
        client = Mock(spec=SerialDeviceClient)
        client.put_time.return_value = {
            "hour": 8,
            "minute": 42,
            "year": 2026,
            "month": 7,
            "day": 14,
        }

        result = cli._sync_time_from_host(client, local)

        client.put_time.assert_called_once_with(8, 42, 7, 14, 2026)
        self.assertEqual(result["state"], "synced")
        self.assertEqual(result["host_utc"], "2026-07-14T15:42:00Z")
        self.assertEqual(result["utc_offset_minutes"], -420)

    def test_sync_rejects_an_unvalidated_device_response(self) -> None:
        local = datetime(2026, 7, 14, 8, 42, tzinfo=timezone.utc)
        client = Mock(spec=SerialDeviceClient)
        client.put_time.return_value = {"hour": 7, "minute": 0}

        with self.assertRaisesRegex(DeviceError, "could not be validated"):
            cli._sync_time_from_host(client, local)

    def test_usb_provisioning_syncs_time_automatically(self) -> None:
        with TemporaryDirectory() as tmp:
            serial = Mock(spec=SerialDeviceClient)
            serial.provision_wifi.return_value = {"device_id": "pj-usb"}
            serial.put_time.side_effect = lambda hour, minute, month, day, year: {
                "hour": hour,
                "minute": minute,
                "year": year,
                "month": month,
                "day": day,
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

    def test_time_failure_preserves_provisioned_profile_for_retry(self) -> None:
        with TemporaryDirectory() as tmp:
            serial = Mock(spec=SerialDeviceClient)
            serial.provision_wifi.return_value = {"device_id": "pj-usb"}
            serial.put_time.side_effect = DeviceError("time transport unavailable")
            stdout = StringIO()
            with patch("pocket_journal_partner.cli.resolve_serial_port", return_value="/dev/cu.test"):
                with patch("pocket_journal_partner.cli.SerialDeviceClient", return_value=serial):
                    with redirect_stdout(stdout):
                        exit_code = cli.main([
                            "provision", "--ssid", "Lab", "--password", "test-password",
                            "--data-dir", tmp,
                        ])
            stored = json.loads((Path(tmp) / "config.json").read_text(encoding="utf-8"))

        self.assertEqual(exit_code, 0)
        self.assertIn("pj-usb", stored["devices"])
        result = json.loads(stdout.getvalue())
        self.assertEqual(result["time_sync"]["state"], "failed")
        self.assertNotIn("test-password", stdout.getvalue())


class CalendarDeprecationTests(unittest.TestCase):
    def test_primary_help_omits_calendar(self) -> None:
        help_text = cli.build_parser().format_help()

        self.assertNotIn("calendar", help_text)

    def test_calendar_sync_remains_compatible_and_warns(self) -> None:
        session = SimpleNamespace(
            device_id="pj-test",
            client=SimpleNamespace(upload_calendar_today=Mock()),
            require=Mock(),
            envelope=lambda result: {"device_id": "pj-test", "transport": "lan", "result": result},
        )
        with TemporaryDirectory() as tmp:
            fixture = Path(tmp) / "events.json"
            fixture.write_text("[]\n", encoding="utf-8")
            stdout = StringIO()
            stderr = StringIO()
            with patch("pocket_journal_partner.cli._lan_session_from_args", return_value=session):
                with redirect_stdout(stdout), redirect_stderr(stderr):
                    exit_code = cli.main([
                        "calendar", "sync", "--device", "pj-test", "--fixture", str(fixture),
                    ])

        self.assertEqual(exit_code, 0)
        self.assertIn("deprecated", stderr.getvalue())
        self.assertIn(cli.CALENDAR_REMOVAL_VERSION, stderr.getvalue())
        session.client.upload_calendar_today.assert_called_once()
        self.assertEqual(json.loads(stdout.getvalue())["result"]["uploaded"], 0)


if __name__ == "__main__":
    unittest.main()
