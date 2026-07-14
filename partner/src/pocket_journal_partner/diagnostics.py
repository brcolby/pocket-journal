from __future__ import annotations

from datetime import datetime
from ipaddress import ip_address
from typing import Any
import re


_SENSITIVE_KEYS = {"credential", "credentials", "password", "ssid", "token"}
_DISCONNECT_REASON_RE = re.compile(r"\breason\s+(\d+)\b", re.IGNORECASE)

_AUTH_FAILURE_REASONS = {2, 6, 7, 15, 23, 24, 202, 204}
_AP_UNAVAILABLE_REASONS = {200, 201, 210, 211, 212}
_ASSOCIATION_FAILURE_REASONS = {4, 5, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 203}
_WIFI_STATES = {"connected", "connecting", "error", "failed", "ready", "starting", "unavailable", "unknown"}
_WIFI_PHASES = {
    "access_point_unavailable",
    "association_failed",
    "authentication_failed",
    "connected",
    "connecting",
    "dhcp",
    "dhcp_failed",
    "disconnected",
    "failed",
    "unprovisioned",
}
_DHCP_STATES = {"bound", "failed", "pending", "requesting", "timeout", "unknown"}
_RETRY_STATES = {"backoff", "disabled", "firmware_managed", "idle", "retrying", "unknown"}


def credential_safe_status(payload: Any) -> Any:
    """Return a status payload that cannot echo pairing or Wi-Fi secrets."""
    if isinstance(payload, dict):
        safe: dict[str, Any] = {}
        for key, value in payload.items():
            normalized_key = key.lower().replace("-", "_")
            if any(part in _SENSITIVE_KEYS for part in normalized_key.split("_")):
                safe[key] = "[redacted]"
            elif normalized_key == "last_error" and isinstance(value, str):
                safe[key] = _safe_firmware_message(value)
            else:
                safe[key] = credential_safe_status(value)
        return safe
    if isinstance(payload, list):
        return [credential_safe_status(value) for value in payload]
    return payload


def wifi_diagnostics(status: dict[str, Any]) -> dict[str, Any]:
    """Normalize current and future firmware Wi-Fi status into a safe contract."""
    details = status.get("wifi_diagnostics")
    if not isinstance(details, dict):
        details = {}

    provisioned = _as_bool(_first(details, status, "provisioned", "wifi_provisioned"))
    wifi_state = _safe_enum(_first(details, status, "state", "wifi"), _WIFI_STATES, "unknown")
    ip_address = _safe_ip(_first(details, status, "ip", "ip_address"))
    connected = wifi_state in {"ready", "connected"} and ip_address not in {"", "0.0.0.0"}

    reason_code = _as_int(_first(details, status, "last_disconnect_reason", "wifi_disconnect_reason"))
    if reason_code is None:
        last_error = status.get("last_error")
        if isinstance(last_error, str):
            match = _DISCONNECT_REASON_RE.search(last_error)
            if match is not None:
                reason_code = int(match.group(1))
    reason = _classify_disconnect(reason_code)

    explicit_phase = _safe_enum(_first(details, status, "phase", "wifi_phase"), _WIFI_PHASES, "")
    dhcp_state = _safe_enum(_first(details, status, "dhcp_state", "wifi_dhcp_state"), _DHCP_STATES, "")
    phase = explicit_phase or _infer_phase(
        provisioned=provisioned,
        connected=connected,
        wifi_state=wifi_state,
        reason=reason,
        dhcp_state=dhcp_state,
        last_error=status.get("last_error"),
    )
    if not dhcp_state:
        if connected:
            dhcp_state = "bound"
        elif phase in {"dhcp", "dhcp_failed"}:
            dhcp_state = "failed" if phase == "dhcp_failed" else "pending"
        else:
            dhcp_state = "unknown"

    retry_count = _as_int(_first(details, status, "retry_count", "wifi_retry_count"))
    backoff_ms = _as_int(_first(details, status, "backoff_ms", "wifi_backoff_ms"))
    retry_state = _safe_enum(_first(details, status, "retry_state", "wifi_retry_state"), _RETRY_STATES, "")
    if not retry_state:
        retry_state = "idle" if connected else "firmware_managed"

    ap_visible = _as_optional_bool(_first(details, status, "ap_visible", "wifi_ap_visible"))
    rssi_dbm = _as_int(_first(details, status, "rssi_dbm", "wifi_rssi_dbm"))
    channel = _as_int(_first(details, status, "channel", "wifi_channel"))
    last_success = _safe_optional_text(
        _first(details, status, "last_success_utc", "wifi_last_success_utc")
    )

    return {
        "provisioned": provisioned,
        "phase": phase,
        "connection": {
            "state": wifi_state,
            "connected": connected,
            "ip": ip_address,
            "dhcp": dhcp_state,
        },
        "access_point": {
            "visible": ap_visible,
            "rssi_dbm": rssi_dbm,
            "channel": channel,
        },
        "last_disconnect": {
            "reason_code": reason_code,
            "classification": reason,
        },
        "retry": {
            "state": retry_state,
            "count": retry_count,
            "backoff_ms": backoff_ms,
        },
        "last_success_utc": last_success,
        "action": _recommended_action(phase),
    }


def _first(primary: dict[str, Any], fallback: dict[str, Any], *keys: str) -> Any:
    for source in (primary, fallback):
        for key in keys:
            if key in source:
                return source[key]
    return None


def _as_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        return value.lower() in {"1", "true", "yes"}
    return False


def _as_optional_bool(value: Any) -> bool | None:
    if value is None:
        return None
    return _as_bool(value)


def _as_int(value: Any) -> int | None:
    if isinstance(value, bool):
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _safe_enum(value: Any, allowed: set[str], default: str) -> str:
    if not isinstance(value, str):
        return default
    normalized = value.strip().lower()
    return normalized if normalized in allowed else default


def _safe_ip(value: Any) -> str:
    if not isinstance(value, str):
        return "0.0.0.0"
    try:
        return str(ip_address(value))
    except ValueError:
        return "0.0.0.0"


def _safe_optional_text(value: Any) -> str | None:
    if not isinstance(value, str) or not value or len(value) > 64:
        return None
    try:
        datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None
    return value


def _safe_firmware_message(message: str) -> str:
    if re.match(r"^Wi-Fi connecting to\s+", message, re.IGNORECASE):
        return "Wi-Fi connecting"
    if re.search(r"(?i)\b(password|token|ssid)\b", message):
        return "firmware reported a credential-related error"
    return message


def _classify_disconnect(reason_code: int | None) -> str | None:
    if reason_code is None:
        return None
    if reason_code in _AUTH_FAILURE_REASONS:
        return "authentication_failed"
    if reason_code in _AP_UNAVAILABLE_REASONS:
        return "access_point_unavailable"
    if reason_code in _ASSOCIATION_FAILURE_REASONS:
        return "association_failed"
    return "other"


def _infer_phase(
    *,
    provisioned: bool,
    connected: bool,
    wifi_state: str,
    reason: str | None,
    dhcp_state: str,
    last_error: Any,
) -> str:
    if not provisioned:
        return "unprovisioned"
    if connected:
        return "connected"
    if dhcp_state in {"failed", "timeout"}:
        return "dhcp_failed"
    if dhcp_state in {"requesting", "pending"}:
        return "dhcp"
    if reason == "authentication_failed":
        return "authentication_failed"
    if reason == "access_point_unavailable":
        return "access_point_unavailable"
    if reason == "association_failed":
        return "association_failed"
    if isinstance(last_error, str) and "connecting" in last_error.lower():
        return "connecting"
    if wifi_state in {"error", "failed"}:
        return "failed"
    return "disconnected"


def _recommended_action(phase: str) -> str:
    actions = {
        "unprovisioned": "provision Wi-Fi credentials",
        "authentication_failed": "re-provision and verify the network password and security mode",
        "access_point_unavailable": "verify the access point is visible, in range, and uses a supported band",
        "association_failed": "verify access point security and client admission settings",
        "dhcp_failed": "check the network DHCP server and address pool",
        "failed": "retry, then inspect firmware logs if the failure persists",
        "disconnected": "wait for the firmware retry or re-provision if retries continue failing",
        "connecting": "wait for association and DHCP to complete",
        "dhcp": "wait for DHCP to assign an address",
        "connected": "none",
    }
    return actions.get(phase, "inspect firmware Wi-Fi diagnostics")
