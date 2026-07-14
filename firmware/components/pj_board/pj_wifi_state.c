#include "pj_wifi_state.h"

#include <stddef.h>
#include <string.h>

static uint64_t deadline_after(uint64_t now_ms, uint32_t delay_ms)
{
    return UINT64_MAX - now_ms < delay_ms ? UINT64_MAX : now_ms + delay_ms;
}

static uint32_t retry_delay(unsigned retry_count)
{
    static const uint32_t delays[] = {2000u, 4000u, 8000u, 15000u, 30000u, 60000u};
    if (retry_count == 0) {
        return delays[0];
    }
    size_t index = retry_count - 1u;
    if (index >= sizeof(delays) / sizeof(delays[0])) {
        index = sizeof(delays) / sizeof(delays[0]) - 1u;
    }
    return delays[index];
}

static void schedule_retry(pj_wifi_state_t *state, uint64_t now_ms)
{
    if (state->retry_count < UINT32_MAX) {
        state->retry_count++;
    }
    state->backoff_ms = retry_delay(state->retry_count);
    state->next_retry_ms = deadline_after(now_ms, state->backoff_ms);
    state->retry_state = PJ_WIFI_RETRY_BACKOFF;
}

void pj_wifi_state_init(pj_wifi_state_t *state, int provisioned,
                        uint64_t now_ms)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->ap_visible = -1;
    state->rssi_dbm = -127;
    pj_wifi_state_set_provisioned(state, provisioned, now_ms);
}

void pj_wifi_state_set_provisioned(pj_wifi_state_t *state, int provisioned,
                                   uint64_t now_ms)
{
    if (state == NULL) {
        return;
    }
    state->provisioned = provisioned != 0;
    state->has_ip = 0;
    state->dhcp_state = PJ_WIFI_DHCP_UNKNOWN;
    state->dhcp_deadline_ms = 0;
    state->retry_count = 0;
    state->backoff_ms = 0;
    state->last_disconnect_reason = 0;
    state->ap_visible = -1;
    state->rssi_dbm = -127;
    state->channel = 0;
    state->last_success_monotonic_ms = 0;
    state->last_success_utc_s = 0;
    if (!state->provisioned) {
        state->phase = PJ_WIFI_PHASE_UNPROVISIONED;
        state->retry_state = PJ_WIFI_RETRY_DISABLED;
        state->next_retry_ms = 0;
        return;
    }
    state->phase = PJ_WIFI_PHASE_DISCONNECTED;
    state->retry_state = PJ_WIFI_RETRY_BACKOFF;
    state->next_retry_ms = now_ms;
}

void pj_wifi_state_on_driver_started(pj_wifi_state_t *state,
                                     uint64_t now_ms)
{
    if (state == NULL || !state->provisioned) {
        return;
    }
    state->phase = PJ_WIFI_PHASE_DISCONNECTED;
    state->retry_state = PJ_WIFI_RETRY_BACKOFF;
    state->backoff_ms = 0;
    state->next_retry_ms = now_ms;
}

void pj_wifi_state_on_associated(pj_wifi_state_t *state, uint64_t now_ms)
{
    if (state == NULL || !state->provisioned) {
        return;
    }
    state->phase = PJ_WIFI_PHASE_DHCP;
    state->dhcp_state = PJ_WIFI_DHCP_REQUESTING;
    state->retry_state = PJ_WIFI_RETRY_IDLE;
    state->backoff_ms = 0;
    state->next_retry_ms = 0;
    state->dhcp_deadline_ms = deadline_after(now_ms, PJ_WIFI_DHCP_TIMEOUT_MS);
    state->ap_visible = 1;
}

void pj_wifi_state_on_got_ip(pj_wifi_state_t *state, uint64_t now_ms,
                             int rssi_dbm, unsigned channel)
{
    if (state == NULL || !state->provisioned) {
        return;
    }
    state->phase = PJ_WIFI_PHASE_CONNECTED;
    state->dhcp_state = PJ_WIFI_DHCP_BOUND;
    state->retry_state = PJ_WIFI_RETRY_IDLE;
    state->has_ip = 1;
    state->ap_visible = 1;
    state->rssi_dbm = (int16_t)rssi_dbm;
    state->channel = channel > UINT8_MAX ? UINT8_MAX : (uint8_t)channel;
    state->retry_count = 0;
    state->backoff_ms = 0;
    state->next_retry_ms = 0;
    state->dhcp_deadline_ms = 0;
    state->last_success_monotonic_ms = now_ms;
}

void pj_wifi_state_on_lost_ip(pj_wifi_state_t *state, uint64_t now_ms)
{
    if (state == NULL || !state->provisioned) {
        return;
    }
    state->has_ip = 0;
    state->phase = PJ_WIFI_PHASE_DHCP_FAILED;
    state->dhcp_state = PJ_WIFI_DHCP_TIMEOUT;
    state->dhcp_deadline_ms = 0;
    schedule_retry(state, now_ms);
}

pj_wifi_disconnect_class_t pj_wifi_disconnect_classify(unsigned reason)
{
    switch (reason) {
    case 2:
    case 6:
    case 7:
    case 15:
    case 23:
    case 24:
    case 202:
    case 204:
        return PJ_WIFI_DISCONNECT_AUTHENTICATION;
    case 200:
    case 201:
    case 210:
    case 211:
    case 212:
        return PJ_WIFI_DISCONNECT_ACCESS_POINT_UNAVAILABLE;
    case 4:
    case 5:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 16:
    case 17:
    case 18:
    case 203:
        return PJ_WIFI_DISCONNECT_ASSOCIATION;
    default:
        return PJ_WIFI_DISCONNECT_OTHER;
    }
}

void pj_wifi_state_on_disconnected(pj_wifi_state_t *state, unsigned reason,
                                   uint64_t now_ms)
{
    if (state == NULL || !state->provisioned) {
        return;
    }
    state->has_ip = 0;
    state->dhcp_state = PJ_WIFI_DHCP_UNKNOWN;
    state->dhcp_deadline_ms = 0;
    state->last_disconnect_reason = reason > UINT16_MAX ? UINT16_MAX : (uint16_t)reason;
    switch (pj_wifi_disconnect_classify(reason)) {
    case PJ_WIFI_DISCONNECT_AUTHENTICATION:
        state->phase = PJ_WIFI_PHASE_AUTHENTICATION_FAILED;
        state->ap_visible = 1;
        break;
    case PJ_WIFI_DISCONNECT_ACCESS_POINT_UNAVAILABLE:
        state->phase = PJ_WIFI_PHASE_ACCESS_POINT_UNAVAILABLE;
        state->ap_visible = 0;
        break;
    case PJ_WIFI_DISCONNECT_ASSOCIATION:
        state->phase = PJ_WIFI_PHASE_ASSOCIATION_FAILED;
        state->ap_visible = 1;
        break;
    case PJ_WIFI_DISCONNECT_OTHER:
    default:
        state->phase = PJ_WIFI_PHASE_DISCONNECTED;
        state->ap_visible = -1;
        break;
    }
    schedule_retry(state, now_ms);
}

void pj_wifi_state_on_connect_request_failed(pj_wifi_state_t *state,
                                             uint64_t now_ms)
{
    if (state == NULL || !state->provisioned) {
        return;
    }
    state->has_ip = 0;
    state->phase = PJ_WIFI_PHASE_FAILED;
    state->dhcp_state = PJ_WIFI_DHCP_UNKNOWN;
    state->ap_visible = -1;
    schedule_retry(state, now_ms);
}

void pj_wifi_state_set_last_success_utc(pj_wifi_state_t *state,
                                        int64_t epoch_s)
{
    if (state != NULL && epoch_s > 0 && state->last_success_monotonic_ms != 0) {
        state->last_success_utc_s = epoch_s;
    }
}

pj_wifi_action_t pj_wifi_state_tick(pj_wifi_state_t *state, uint64_t now_ms)
{
    if (state == NULL || !state->provisioned) {
        return PJ_WIFI_ACTION_NONE;
    }
    if (state->phase == PJ_WIFI_PHASE_DHCP && state->dhcp_deadline_ms != 0 &&
        now_ms >= state->dhcp_deadline_ms) {
        state->has_ip = 0;
        state->phase = PJ_WIFI_PHASE_DHCP_FAILED;
        state->dhcp_state = PJ_WIFI_DHCP_TIMEOUT;
        state->dhcp_deadline_ms = 0;
        schedule_retry(state, now_ms);
    }
    if (state->retry_state != PJ_WIFI_RETRY_BACKOFF ||
        now_ms < state->next_retry_ms) {
        return PJ_WIFI_ACTION_NONE;
    }
    pj_wifi_action_t action = state->phase == PJ_WIFI_PHASE_DHCP_FAILED
        ? PJ_WIFI_ACTION_RECONNECT : PJ_WIFI_ACTION_CONNECT;
    state->phase = PJ_WIFI_PHASE_CONNECTING;
    state->dhcp_state = PJ_WIFI_DHCP_UNKNOWN;
    state->retry_state = PJ_WIFI_RETRY_RETRYING;
    state->next_retry_ms = 0;
    return action;
}

const char *pj_wifi_phase_name(pj_wifi_phase_t phase)
{
    switch (phase) {
    case PJ_WIFI_PHASE_UNPROVISIONED: return "unprovisioned";
    case PJ_WIFI_PHASE_DISCONNECTED: return "disconnected";
    case PJ_WIFI_PHASE_CONNECTING: return "connecting";
    case PJ_WIFI_PHASE_AUTHENTICATION_FAILED: return "authentication_failed";
    case PJ_WIFI_PHASE_ACCESS_POINT_UNAVAILABLE: return "access_point_unavailable";
    case PJ_WIFI_PHASE_ASSOCIATION_FAILED: return "association_failed";
    case PJ_WIFI_PHASE_DHCP: return "dhcp";
    case PJ_WIFI_PHASE_DHCP_FAILED: return "dhcp_failed";
    case PJ_WIFI_PHASE_CONNECTED: return "connected";
    case PJ_WIFI_PHASE_FAILED: return "failed";
    default: return "failed";
    }
}

const char *pj_wifi_dhcp_state_name(pj_wifi_dhcp_state_t state)
{
    switch (state) {
    case PJ_WIFI_DHCP_REQUESTING: return "requesting";
    case PJ_WIFI_DHCP_BOUND: return "bound";
    case PJ_WIFI_DHCP_TIMEOUT: return "timeout";
    case PJ_WIFI_DHCP_UNKNOWN:
    default: return "unknown";
    }
}

const char *pj_wifi_retry_state_name(pj_wifi_retry_state_t state)
{
    switch (state) {
    case PJ_WIFI_RETRY_DISABLED: return "disabled";
    case PJ_WIFI_RETRY_IDLE: return "idle";
    case PJ_WIFI_RETRY_BACKOFF: return "backoff";
    case PJ_WIFI_RETRY_RETRYING: return "retrying";
    default: return "disabled";
    }
}
