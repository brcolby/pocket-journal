from __future__ import annotations

from dataclasses import dataclass
from datetime import date, datetime, time, timezone
from typing import Any


@dataclass
class CalendarEvent:
    source_id: str
    title: str
    start: str
    end: str
    all_day: bool
    location: str
    updated: str


def normalize_google_event(event: dict[str, Any]) -> CalendarEvent:
    start_raw = event.get("start", {})
    end_raw = event.get("end", {})
    all_day = "date" in start_raw
    start = start_raw.get("dateTime") or start_raw.get("date", "")
    end = end_raw.get("dateTime") or end_raw.get("date", "")
    return CalendarEvent(
        source_id=event.get("id", ""),
        title=event.get("summary", "(untitled)"),
        start=start,
        end=end,
        all_day=all_day,
        location=event.get("location", ""),
        updated=event.get("updated", ""),
    )


def calendar_payload_for_day(day: date, events: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "date": day.isoformat(),
        "events": [normalize_google_event(event).__dict__ for event in events],
    }


def day_bounds(day: date, tz: timezone | None = None) -> tuple[str, str]:
    zone = tz or datetime.now().astimezone().tzinfo or timezone.utc
    start = datetime.combine(day, time.min, tzinfo=zone)
    end = datetime.combine(day, time.max, tzinfo=zone)
    return start.isoformat(), end.isoformat()


def fetch_google_events_for_day(day: date) -> dict[str, Any]:
    _ = day
    raise NotImplementedError("Google OAuth flow is staged until credentials are configured")

