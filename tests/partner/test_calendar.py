from __future__ import annotations

from datetime import date
import unittest

from pocket_journal_partner.calendar import calendar_payload_for_day, normalize_google_event


class CalendarTests(unittest.TestCase):
    def test_normalizes_timed_event(self) -> None:
        event = normalize_google_event({
            "id": "abc",
            "summary": "Review",
            "start": {"dateTime": "2026-06-06T09:00:00-07:00"},
            "end": {"dateTime": "2026-06-06T09:30:00-07:00"},
            "location": "Office",
            "updated": "2026-06-05T20:00:00Z",
        })
        self.assertEqual(event.source_id, "abc")
        self.assertEqual(event.title, "Review")
        self.assertFalse(event.all_day)

    def test_payload_for_day(self) -> None:
        payload = calendar_payload_for_day(date(2026, 6, 6), [{
            "id": "all-day",
            "summary": "Ship",
            "start": {"date": "2026-06-06"},
            "end": {"date": "2026-06-07"},
        }])
        self.assertEqual(payload["date"], "2026-06-06")
        self.assertEqual(payload["events"][0]["all_day"], True)


if __name__ == "__main__":
    unittest.main()

