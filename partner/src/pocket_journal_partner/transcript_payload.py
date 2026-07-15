from __future__ import annotations

from typing import Any, Mapping
import json


DEVICE_TRANSCRIPT_SCHEMA_VERSION = 1
DEVICE_TRANSCRIPT_MAX_BYTES = 60 * 1024
DEVICE_TITLE_MAX_BYTES = 95


def _device_title(value: Any) -> str:
    if not isinstance(value, str):
        raise ValueError("device title must be text")
    normalized = " ".join(
        "".join(character if character.isprintable() else " " for character in value).split()
    )
    if not normalized:
        raise ValueError("device title must not be empty")
    output: list[str] = []
    used = 0
    for character in normalized:
        encoded = character.encode("utf-8")
        if used + len(encoded) > DEVICE_TITLE_MAX_BYTES:
            break
        output.append(character)
        used += len(encoded)
    title = "".join(output).strip()
    if not title:
        raise ValueError("device title must contain displayable UTF-8 text")
    return title


def serialize_device_transcript(payload: Mapping[str, Any]) -> bytes:
    """Serialize exactly as DeviceClient does for an application/json request."""
    try:
        return json.dumps(
            payload,
            ensure_ascii=False,
            separators=(",", ":"),
            sort_keys=True,
            allow_nan=False,
        ).encode("utf-8")
    except (TypeError, ValueError, UnicodeError) as exc:
        raise ValueError("transcript payload must contain valid JSON metadata") from exc


def build_device_transcript_payload(
    transcript: Mapping[str, Any],
    source: Mapping[str, Any],
    transcription_identity: Mapping[str, Any],
    *,
    title: str,
) -> dict[str, Any]:
    """Build a complete, bounded transcript document for device storage.

    Backend-specific fields are retained at the top level so optional language,
    segment, timestamp, and future metadata survive the device round trip.
    Reserved provenance fields are always derived from verified sync inputs.
    """
    if not isinstance(transcript, Mapping):
        raise ValueError("transcript must be a JSON object")
    text = transcript.get("text")
    if not isinstance(text, str) or not text.strip():
        raise ValueError("transcript text must be a non-empty string")
    if not isinstance(source, Mapping):
        raise ValueError("transcript source must be a JSON object")
    digest = source.get("sha256")
    byte_length = source.get("bytes")
    if not isinstance(digest, str) or not digest:
        raise ValueError("transcript source sha256 must be a non-empty string")
    if not isinstance(byte_length, int) or isinstance(byte_length, bool) or byte_length < 0:
        raise ValueError("transcript source bytes must be a non-negative integer")
    if not isinstance(transcription_identity, Mapping):
        raise ValueError("transcription identity must be a JSON object")

    payload = dict(transcript)
    payload.pop("sync", None)
    payload["schema_version"] = DEVICE_TRANSCRIPT_SCHEMA_VERSION
    payload["text"] = text.strip()
    payload["title"] = _device_title(title)
    payload["source"] = dict(source)
    payload["transcription"] = dict(transcription_identity)

    encoded = serialize_device_transcript(payload)
    if len(encoded) > DEVICE_TRANSCRIPT_MAX_BYTES:
        raise ValueError(
            f"transcript exceeds the {DEVICE_TRANSCRIPT_MAX_BYTES}-byte device upload limit"
        )
    # Detach nested caller-owned values while proving the returned document is JSON-safe.
    return json.loads(encoded.decode("utf-8"))
