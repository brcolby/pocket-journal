from __future__ import annotations

from typing import Any


MAX_SLOTS = 4
MAX_TITLE_LENGTH = 24
MAX_LABEL_LENGTH = 12
SUPPORTED_ICONS = frozenset({
    "alarm", "document_audio", "microphone", "notebook", "play", "read_me",
    "repeat", "settings", "time", "timer", "volume_up", "wifi",
})
SUPPORTED_DESTINATIONS = frozenset({
    "notes", "record", "listen", "read", "time", "alarm", "stopwatch",
    "timer", "interval", "settings", "sync", "volume", "calendar",
})


def _display_text(value: Any, field: str, maximum: int) -> str:
    if not isinstance(value, str) or not value or value != value.strip():
        raise ValueError(f"{field} must be a non-empty string without outer whitespace")
    if len(value) > maximum or any(ord(char) < 0x20 or ord(char) > 0x7E for char in value):
        raise ValueError(f"{field} must be printable ASCII and at most {maximum} characters")
    return value


def normalize_home_design(payload: Any) -> dict[str, object]:
    if not isinstance(payload, dict) or set(payload) - {"title", "slots"}:
        raise ValueError("home design must be an object containing only title and slots")
    title = _display_text(payload.get("title", "Pocket Journal"), "title", MAX_TITLE_LENGTH)
    slots = payload.get("slots")
    if not isinstance(slots, list) or not 1 <= len(slots) <= MAX_SLOTS:
        raise ValueError("home design must contain between one and four slots")
    normalized_slots: list[dict[str, str]] = []
    for slot in slots:
        if not isinstance(slot, dict) or set(slot) != {"label", "icon", "state"}:
            raise ValueError("each home slot must contain exactly label, icon, and state")
        label = _display_text(slot["label"], "slot label", MAX_LABEL_LENGTH)
        icon = slot["icon"]
        state = slot["state"]
        if not isinstance(icon, str) or icon not in SUPPORTED_ICONS:
            raise ValueError(f"unsupported home icon: {icon}")
        if not isinstance(state, str) or state not in SUPPORTED_DESTINATIONS:
            raise ValueError(f"unsupported home destination: {state}")
        normalized_slots.append({"label": label, "icon": icon, "state": state})
    return {"title": title, "slots": normalized_slots}
