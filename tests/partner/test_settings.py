import json
import unittest
from unittest.mock import MagicMock, patch

from pocket_journal_partner import cli
from pocket_journal_partner.cli import _parse_settings_assignments
from pocket_journal_partner.device import DeviceClient, DeviceError, SerialDeviceClient


def settings_payload(**overrides: object) -> dict[str, object]:
    payload: dict[str, object] = {
        "theme": "light",
        "volume": 8,
        "alarm_enabled": False,
        "alarm_hour": 7,
        "alarm_minute": 30,
        "timer_seconds": 300,
        "interval_seconds": 90,
        "clock_24h": True,
        "temperature_unit": "c",
        "transcript_font_size": 3,
        "sync_pending": 0,
        "sync_transferred": 0,
        "generation": 12,
    }
    payload.update(overrides)
    return payload


class SettingsCliTests(unittest.TestCase):
    def test_get_defaults_to_usb_first_session(self) -> None:
        client = MagicMock()
        client.get_settings.return_value = settings_payload()
        session = MagicMock(client=client)
        session.envelope.side_effect = lambda payload: payload

        with patch.object(cli, "_session_from_args", return_value=session) as factory, \
             patch.object(cli, "_print_json"):
            self.assertEqual(cli.main(["settings", "get"]), 0)

        factory.assert_called_once()
        session.require.assert_called_once_with("settings.read")

    def test_assignments_use_api_scalar_types(self) -> None:
        self.assertEqual(
            _parse_settings_assignments(
                ["volume=8", "theme=dark", "alarm_enabled=true", "timer_seconds=600",
                 "clock_24h=false", "temperature_unit=f", "transcript_font_size=3"]
            ),
            {"volume": 8, "theme": "dark", "alarm_enabled": True, "timer_seconds": 600,
             "clock_24h": False, "temperature_unit": "f", "transcript_font_size": 3},
        )

    def test_rejects_unsupported_and_malformed_assignments(self) -> None:
        for assignments in (
            ["volume"],
            ["unknown=1"],
            ["volume=loud"],
            ["volume=11"],
            ["alarm_hour=24"],
            ["alarm_minute=-1"],
            ["timer_seconds=29"],
            ["interval_seconds=86401"],
            ["theme=blue"],
            ["alarm_enabled=yes"],
            ["clock_24h=yes"],
            ["temperature_unit=k"],
            ["transcript_font_size=4"],
        ):
            with self.subTest(assignments=assignments), self.assertRaises(SystemExit):
                _parse_settings_assignments(assignments)

    def test_usb_get_uses_bounded_retriable_request(self) -> None:
        client = SerialDeviceClient("/dev/cu.test")
        calls: list[tuple[str, dict[str, object]]] = []

        def request(command: str, **kwargs: object) -> dict[str, object]:
            calls.append((command, kwargs))
            return settings_payload()

        client._request = request  # type: ignore[method-assign]
        self.assertEqual(client.get_settings()["generation"], 12)
        self.assertEqual(calls[0][0], "PJ_SETTINGS_GET")
        self.assertEqual(calls[0][1]["max_attempts"], 2)
        self.assertTrue(calls[0][1]["request_id"])

    def test_usb_set_pins_generation_and_hex_encodes_canonical_json(self) -> None:
        client = SerialDeviceClient("/dev/cu.test")
        calls: list[tuple[str, dict[str, object]]] = []

        def request(command: str, **kwargs: object) -> dict[str, object]:
            calls.append((command, kwargs))
            if command == "PJ_SETTINGS_GET":
                return settings_payload()
            return settings_payload(volume=9, generation=13, changed=True)

        client._request = request  # type: ignore[method-assign]
        result = client.put_settings({"volume": 9, "theme": "dark"})
        self.assertEqual(result["generation"], 13)
        command, options = calls[1]
        self.assertIn("expected_generation=12", command)
        encoded = command.split("payload_hex=", 1)[1]
        self.assertEqual(
            json.loads(bytes.fromhex(encoded).decode("ascii")),
            {"theme": "dark", "volume": 9},
        )
        self.assertEqual(options["max_attempts"], 2)
        self.assertTrue(options["request_id"])

    def test_lan_set_pins_generation_from_immediate_read(self) -> None:
        client = DeviceClient("http://device", "token")
        calls: list[tuple[str, str, object | None]] = []

        def request(method: str, path: str, body: object | None = None) -> dict[str, object]:
            calls.append((method, path, body))
            if method == "GET":
                return settings_payload(generation=41)
            return settings_payload(volume=4, generation=42, changed=True)

        client._request = request  # type: ignore[method-assign]
        self.assertEqual(client.put_settings({"volume": 4})["generation"], 42)
        self.assertEqual(calls[1][2], {"volume": 4, "expected_generation": 41})

    def test_invalid_generation_is_rejected(self) -> None:
        client = SerialDeviceClient("/dev/cu.test")
        client._request = lambda *args, **kwargs: settings_payload(generation=-1)  # type: ignore[method-assign]
        with self.assertRaisesRegex(DeviceError, "settings generation"):
            client.get_settings()


if __name__ == "__main__":
    unittest.main()
