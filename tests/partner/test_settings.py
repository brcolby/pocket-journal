import unittest

from pocket_journal_partner.cli import _parse_settings_assignments


class SettingsCliTests(unittest.TestCase):
    def test_assignments_use_api_scalar_types(self) -> None:
        self.assertEqual(
            _parse_settings_assignments(
                ["volume=8", "theme=dark", "alarm_enabled=true", "timer_seconds=600"]
            ),
            {"volume": 8, "theme": "dark", "alarm_enabled": True, "timer_seconds": 600},
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
        ):
            with self.subTest(assignments=assignments), self.assertRaises(SystemExit):
                _parse_settings_assignments(assignments)


if __name__ == "__main__":
    unittest.main()
