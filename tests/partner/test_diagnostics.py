from __future__ import annotations

from contextlib import redirect_stdout
from io import StringIO
from unittest.mock import patch
import json
import unittest

from pocket_journal_partner import cli
from pocket_journal_partner.diagnostics import credential_safe_status, wifi_diagnostics
from pocket_journal_partner.device import SerialDeviceClient
from pocket_journal_partner.operations import DeviceSession


class WifiDiagnosticsTests(unittest.TestCase):
    def test_infers_provisioned_disconnected_state(self) -> None:
        result = wifi_diagnostics({
            "wifi_provisioned": True,
            "wifi": "unavailable",
            "ip": "0.0.0.0",
            "last_error": "Wi-Fi credentials stored",
        })

        self.assertEqual(result["phase"], "disconnected")
        self.assertFalse(result["connection"]["connected"])
        self.assertEqual(result["retry"]["state"], "firmware_managed")
        self.assertIn("retry", result["action"])

    def test_classifies_authentication_and_ap_visibility_failures(self) -> None:
        auth = wifi_diagnostics({
            "wifi_provisioned": True,
            "wifi": "unavailable",
            "ip": "0.0.0.0",
            "last_error": "Wi-Fi disconnected (reason 202); reconnecting",
        })
        unavailable = wifi_diagnostics({
            "wifi_provisioned": True,
            "wifi": "unavailable",
            "ip": "0.0.0.0",
            "last_error": "Wi-Fi disconnected (reason 201); reconnecting",
        })

        self.assertEqual(auth["phase"], "authentication_failed")
        self.assertEqual(auth["last_disconnect"]["reason_code"], 202)
        self.assertIn("password", auth["action"])
        self.assertEqual(unavailable["phase"], "access_point_unavailable")
        self.assertIn("visible", unavailable["action"])

    def test_reports_structured_dhcp_failure_and_retry_backoff(self) -> None:
        result = wifi_diagnostics({
            "wifi_provisioned": True,
            "wifi": "unavailable",
            "ip": "0.0.0.0",
            "wifi_diagnostics": {
                "phase": "dhcp_failed",
                "dhcp_state": "failed",
                "ap_visible": True,
                "rssi_dbm": -61,
                "channel": 11,
                "retry_state": "backoff",
                "retry_count": 4,
                "backoff_ms": 8000,
                "last_success_utc": "2026-07-14T03:20:00Z",
            },
        })

        self.assertEqual(result["phase"], "dhcp_failed")
        self.assertEqual(result["connection"]["dhcp"], "failed")
        self.assertEqual(result["access_point"], {"visible": True, "rssi_dbm": -61, "channel": 11})
        self.assertEqual(result["retry"], {"state": "backoff", "count": 4, "backoff_ms": 8000})
        self.assertEqual(result["last_success_utc"], "2026-07-14T03:20:00Z")

    def test_connected_state_is_concise(self) -> None:
        result = wifi_diagnostics({
            "wifi_provisioned": True,
            "wifi": "ready",
            "ip": "192.0.2.8",
        })

        self.assertEqual(result["phase"], "connected")
        self.assertEqual(result["connection"]["dhcp"], "bound")
        self.assertEqual(result["action"], "none")

    def test_status_and_diagnostics_never_echo_sensitive_values(self) -> None:
        status = {
            "wifi_provisioned": True,
            "wifi": "unavailable",
            "ip": "0.0.0.0",
            "ssid": "private-network",
            "token": "private-token",
            "nested": {"wifi_password": "private-password"},
            "last_error": "Wi-Fi connecting to private-network",
        }

        safe = credential_safe_status(status)
        diagnostic = wifi_diagnostics(status)
        encoded = json.dumps({"status": safe, "diagnostic": diagnostic})

        self.assertNotIn("private-network", encoded)
        self.assertNotIn("private-token", encoded)
        self.assertNotIn("private-password", encoded)
        self.assertEqual(safe["last_error"], "Wi-Fi connecting")

    def test_cli_emits_diagnostic_transport_envelope(self) -> None:
        client = SerialDeviceClient("/dev/cu.test")
        client.status = lambda: {  # type: ignore[method-assign]
            "device_id": "pj-test",
            "wifi_provisioned": True,
            "wifi": "ready",
            "ip": "192.0.2.8",
        }
        stdout = StringIO()

        with patch(
            "pocket_journal_partner.cli._session_from_args",
            return_value=DeviceSession("pj-test", client),
        ):
            with redirect_stdout(stdout):
                exit_code = cli.main(["device", "wifi-diagnostics"])

        self.assertEqual(exit_code, 0)
        payload = json.loads(stdout.getvalue())
        self.assertEqual(payload["device_id"], "pj-test")
        self.assertEqual(payload["transport"], "usb")
        self.assertEqual(payload["result"]["phase"], "connected")

    def test_general_status_omits_verbose_nested_connectivity_details(self) -> None:
        raw_status = {
            "device_id": "pj-test",
            "wifi_provisioned": True,
            "wifi": "ready",
            "ip": "192.0.2.8",
            "time_set": True,
            "wifi_diagnostics": {
                "phase": "connected",
                "retry_count": 7,
            },
            "time_sync": {
                "state": "synced",
                "server": "pool.ntp.org",
                "attempt_count": 4,
            },
        }
        client = SerialDeviceClient("/dev/cu.test")
        client.status = lambda: raw_status  # type: ignore[method-assign]
        stdout = StringIO()

        with patch(
            "pocket_journal_partner.cli._session_from_args",
            return_value=DeviceSession("pj-test", client),
        ):
            with redirect_stdout(stdout):
                exit_code = cli.main(["device", "status"])

        self.assertEqual(exit_code, 0)
        result = json.loads(stdout.getvalue())["result"]
        self.assertEqual(result["wifi"], "ready")
        self.assertTrue(result["time_set"])
        self.assertNotIn("wifi_diagnostics", result)
        self.assertNotIn("time_sync", result)
        self.assertIn("wifi_diagnostics", raw_status)


if __name__ == "__main__":
    unittest.main()
