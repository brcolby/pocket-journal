from __future__ import annotations

from typing import Any, Mapping
import hashlib
import hmac
import ipaddress
import re


PROTOCOL_VERSION = 1
MAX_ERROR_BYTES = 127
MAX_COUNT = 0x7FFFFFFF
MAX_REQUESTED_MS = (1 << 53) - 1
DEFAULT_DEVELOPMENT_TOKEN = "dev-token"

_DEVICE_ID = re.compile(r"^[A-Za-z0-9_-]{1,31}$")
_OPERATION_ID = re.compile(r"^[A-Za-z0-9_-]{1,64}$")
_NONCE = re.compile(r"^[0-9a-f]{32}$")
_ACTIONS = {"start", "status"}
_STATES = {"queued", "running", "succeeded", "failed"}
_REQUEST_FIELDS = {
    "version", "action", "device_id", "device_ip", "operation_id", "generation",
    "requested_ms", "nonce", "mac",
}
_RESPONSE_FIELDS = {
    "version", "action", "nonce", "device_id", "operation_id", "generation", "requested_ms",
    "state", "total", "pending", "transferred", "failed", "error", "mac",
}

_REQUEST_KEY_DOMAIN = b"PJ-COMPANION-SYNC-REQUEST-KEY-V1"
_RESPONSE_KEY_DOMAIN = b"PJ-COMPANION-SYNC-RESPONSE-KEY-V1"
_DATA_KEY_DOMAIN = b"PJ-COMPANION-SYNC-DATA-KEY-V1"
_EPOCH_KEY_DOMAIN = b"PJ-COMPANION-SYNC-EPOCH-V1"
_REQUEST_DOMAIN = b"PJ-COMPANION-SYNC-REQUEST-V1\n"
_RESPONSE_DOMAIN = b"PJ-COMPANION-SYNC-RESPONSE-V1\n"
_DATA_DOMAIN = b"PJ-COMPANION-SYNC-DATA-V1\n"


class CompanionProtocolError(ValueError):
    pass


class CompanionAuthenticationError(CompanionProtocolError):
    pass


def provisioned_token(token: Any) -> bool:
    if not isinstance(token, str) or token == DEFAULT_DEVELOPMENT_TOKEN:
        return False
    try:
        encoded = token.encode("utf-8")
    except UnicodeEncodeError:
        return False
    return 16 <= len(encoded) <= 63 and "\x00" not in token


def normalize_error(value: Any, fallback: str = "Sync failed") -> str:
    def cleaned_text(candidate: Any) -> str:
        if not isinstance(candidate, str):
            return ""
        return " ".join(
            "".join(
                character if character.isascii() and character.isprintable() else " "
                for character in candidate
            ).split()
        )

    try:
        text = value if isinstance(value, str) else str(value)
    except Exception:
        text = ""
    cleaned = cleaned_text(text)
    if not cleaned:
        if not fallback:
            return ""
        cleaned = cleaned_text(fallback) or "Sync failed"
    normalized = cleaned[:MAX_ERROR_BYTES].strip()
    return normalized or "Sync failed"


def _key(token: str, domain: bytes) -> bytes:
    if not provisioned_token(token):
        raise CompanionAuthenticationError("paired token is not provisioned")
    return hmac.new(token.encode("utf-8"), domain, hashlib.sha256).digest()


def _text(name: str, value: str) -> bytes:
    encoded = value.encode("utf-8")
    return name.encode("ascii") + b"=" + str(len(encoded)).encode("ascii") + b":" + encoded + b"\n"


def _number(name: str, value: int) -> bytes:
    return name.encode("ascii") + b"=" + str(value).encode("ascii") + b"\n"


def _uint(value: Any, name: str, maximum: int, *, positive: bool = False) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise CompanionProtocolError(f"{name} must be an integer")
    minimum = 1 if positive else 0
    if not minimum <= value <= maximum:
        raise CompanionProtocolError(f"{name} is out of range")
    return value


def _identity_fields(
    device_id: Any, operation_id: Any, generation: Any, requested_ms: Any,
) -> tuple[str, str, int, int]:
    if not isinstance(device_id, str) or not _DEVICE_ID.fullmatch(device_id):
        raise CompanionProtocolError("invalid device_id")
    if not isinstance(operation_id, str) or not _OPERATION_ID.fullmatch(operation_id):
        raise CompanionProtocolError("invalid operation_id")
    return (
        device_id,
        operation_id,
        _uint(generation, "generation", 0xFFFFFFFF, positive=True),
        _uint(requested_ms, "requested_ms", MAX_REQUESTED_MS),
    )


def canonical_request(
    action: str, device_id: str, device_ip: str, operation_id: str,
    generation: int, requested_ms: int, nonce: str,
) -> bytes:
    device_id, operation_id, generation, requested_ms = _identity_fields(
        device_id, operation_id, generation, requested_ms
    )
    if not isinstance(action, str) or action not in _ACTIONS:
        raise CompanionProtocolError("invalid sync action")
    if not isinstance(nonce, str) or not _NONCE.fullmatch(nonce):
        raise CompanionProtocolError("invalid request nonce")
    try:
        parsed_ip = ipaddress.IPv4Address(device_ip)
    except (ipaddress.AddressValueError, TypeError) as exc:
        raise CompanionProtocolError("invalid device_ip") from exc
    if str(parsed_ip) != device_ip:
        raise CompanionProtocolError("device_ip must be canonical IPv4 text")
    return b"".join((
        _REQUEST_DOMAIN,
        _number("version", PROTOCOL_VERSION),
        _text("action", action),
        _text("device_id", device_id),
        _text("device_ip", device_ip),
        _text("operation_id", operation_id),
        _number("generation", generation),
        _number("requested_ms", requested_ms),
        _text("nonce", nonce),
    ))


def request_envelope(
    token: str, action: str, device_id: str, device_ip: str, operation_id: str,
    generation: int, requested_ms: int, nonce: str,
) -> dict[str, Any]:
    canonical = canonical_request(
        action, device_id, device_ip, operation_id, generation, requested_ms,
        nonce,
    )
    mac = hmac.new(_key(token, _REQUEST_KEY_DOMAIN), canonical, hashlib.sha256).hexdigest()
    return {
        "version": PROTOCOL_VERSION,
        "action": action,
        "device_id": device_id,
        "device_ip": device_ip,
        "operation_id": operation_id,
        "generation": generation,
        "requested_ms": requested_ms,
        "nonce": nonce,
        "mac": mac,
    }


def verify_request_envelope(
    payload: Any, token: str, *, expected_action: str, expected_device_id: str,
    expected_peer_ip: str,
) -> dict[str, Any]:
    if not isinstance(payload, dict) or set(payload) != _REQUEST_FIELDS:
        raise CompanionProtocolError("invalid request envelope")
    if (
        type(payload.get("version")) is not int
        or payload.get("version") != PROTOCOL_VERSION
        or payload.get("action") != expected_action
    ):
        raise CompanionProtocolError("unsupported request envelope")
    canonical = canonical_request(
        payload["action"], payload["device_id"], payload["device_ip"],
        payload["operation_id"], payload["generation"], payload["requested_ms"],
        payload["nonce"],
    )
    supplied = payload.get("mac")
    if not isinstance(supplied, str) or not re.fullmatch(r"[0-9a-f]{64}", supplied):
        raise CompanionAuthenticationError("invalid request authentication")
    expected = hmac.new(
        _key(token, _REQUEST_KEY_DOMAIN), canonical, hashlib.sha256
    ).hexdigest()
    if not hmac.compare_digest(supplied, expected):
        raise CompanionAuthenticationError("invalid request authentication")
    if payload["device_id"] != expected_device_id:
        raise CompanionAuthenticationError("request is scoped to another device")
    try:
        peer_ip = str(ipaddress.IPv4Address(expected_peer_ip))
    except ipaddress.AddressValueError as exc:
        raise CompanionAuthenticationError("request peer is not IPv4") from exc
    if payload["device_ip"] != peer_ip:
        raise CompanionAuthenticationError("request peer does not match device_ip")
    return dict(payload)


def _validated_progress(payload: Mapping[str, Any]) -> tuple[str, int, int, int, int, str]:
    state = payload.get("state")
    if not isinstance(state, str) or state not in _STATES:
        raise CompanionProtocolError("invalid sync state")
    total = _uint(payload.get("total"), "total", MAX_COUNT)
    pending = _uint(payload.get("pending"), "pending", MAX_COUNT)
    transferred = _uint(payload.get("transferred"), "transferred", MAX_COUNT)
    failed = _uint(payload.get("failed"), "failed", MAX_COUNT)
    if transferred + failed > total or pending != total - transferred - failed:
        raise CompanionProtocolError("inconsistent sync progress counts")
    raw_error = payload.get("error")
    if not isinstance(raw_error, str) or normalize_error(raw_error, "") != raw_error:
        raise CompanionProtocolError("invalid sync error")
    if len(raw_error.encode("utf-8")) > MAX_ERROR_BYTES:
        raise CompanionProtocolError("sync error is too large")
    if state == "succeeded" and (pending != 0 or failed != 0 or raw_error):
        raise CompanionProtocolError("inconsistent successful sync outcome")
    return state, total, pending, transferred, failed, raw_error


def canonical_response(payload: Mapping[str, Any]) -> bytes:
    device_id, operation_id, generation, requested_ms = _identity_fields(
        payload.get("device_id"), payload.get("operation_id"),
        payload.get("generation"), payload.get("requested_ms"),
    )
    state, total, pending, transferred, failed, error = _validated_progress(payload)
    action = payload.get("action")
    nonce = payload.get("nonce")
    if (
        type(payload.get("version")) is not int
        or payload.get("version") != PROTOCOL_VERSION
        or not isinstance(action, str) or action not in _ACTIONS
    ):
        raise CompanionProtocolError("unsupported response envelope")
    if not isinstance(nonce, str) or not _NONCE.fullmatch(nonce):
        raise CompanionProtocolError("invalid response nonce")
    return b"".join((
        _RESPONSE_DOMAIN,
        _number("version", PROTOCOL_VERSION),
        _text("action", action),
        _text("nonce", nonce),
        _text("device_id", device_id),
        _text("operation_id", operation_id),
        _number("generation", generation),
        _number("requested_ms", requested_ms),
        _text("state", state),
        _number("total", total),
        _number("pending", pending),
        _number("transferred", transferred),
        _number("failed", failed),
        _text("error", error),
    ))


def response_envelope(
    snapshot: Mapping[str, Any], token: str, *, action: str, nonce: str,
) -> dict[str, Any]:
    payload = {
        "version": PROTOCOL_VERSION,
        "action": action,
        "nonce": nonce,
        "device_id": snapshot.get("device_id"),
        "operation_id": snapshot.get("operation_id"),
        "generation": snapshot.get("generation"),
        "requested_ms": snapshot.get("requested_ms"),
        "state": snapshot.get("state"),
        "total": snapshot.get("total"),
        "pending": snapshot.get("pending"),
        "transferred": snapshot.get("transferred"),
        "failed": snapshot.get("failed"),
        "error": normalize_error(snapshot.get("error", ""), "Sync failed")
        if snapshot.get("error") else "",
    }
    canonical = canonical_response(payload)
    payload["mac"] = hmac.new(
        _key(token, _RESPONSE_KEY_DOMAIN), canonical, hashlib.sha256
    ).hexdigest()
    return payload


def verify_response_envelope(
    payload: Any,
    token: str,
    *,
    expected_device_id: str | None = None,
    expected_operation_id: str | None = None,
    expected_generation: int | None = None,
    expected_requested_ms: int | None = None,
    expected_action: str | None = None,
    expected_nonce: str | None = None,
) -> dict[str, Any]:
    if not isinstance(payload, dict) or set(payload) != _RESPONSE_FIELDS:
        raise CompanionProtocolError("invalid response envelope")
    canonical = canonical_response(payload)
    supplied = payload.get("mac")
    if not isinstance(supplied, str) or not re.fullmatch(r"[0-9a-f]{64}", supplied):
        raise CompanionAuthenticationError("invalid response authentication")
    expected = hmac.new(
        _key(token, _RESPONSE_KEY_DOMAIN), canonical, hashlib.sha256
    ).hexdigest()
    if not hmac.compare_digest(supplied, expected):
        raise CompanionAuthenticationError("invalid response authentication")
    expected_identity = (
        ("device_id", expected_device_id),
        ("operation_id", expected_operation_id),
        ("generation", expected_generation),
        ("requested_ms", expected_requested_ms),
        ("action", expected_action),
        ("nonce", expected_nonce),
    )
    if any(expected_value is not None and payload[name] != expected_value
           for name, expected_value in expected_identity):
        raise CompanionAuthenticationError("response is scoped to another request")
    return dict(payload)


def scoped_data_token(
    token: str, device_id: str, operation_id: str, generation: int,
    requested_ms: int,
) -> str:
    device_id, operation_id, generation, requested_ms = _identity_fields(
        device_id, operation_id, generation, requested_ms
    )
    canonical = b"".join((
        _DATA_DOMAIN,
        _number("version", PROTOCOL_VERSION),
        _text("device_id", device_id),
        _text("operation_id", operation_id),
        _number("generation", generation),
        _number("requested_ms", requested_ms),
    ))
    return hmac.new(_key(token, _DATA_KEY_DOMAIN), canonical, hashlib.sha256).hexdigest()


def pairing_epoch(token: str) -> str:
    if not provisioned_token(token):
        return ""
    return hmac.new(
        token.encode("utf-8"), _EPOCH_KEY_DOMAIN, hashlib.sha256
    ).hexdigest()
