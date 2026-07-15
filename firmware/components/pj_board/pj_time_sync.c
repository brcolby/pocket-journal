#include "pj_time_sync.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define PJ_TIME_SYNC_MIN_EPOCH_S 1704067200ll
#define PJ_TIME_SYNC_MAX_EPOCH_S 4102444799ll

static uint64_t deadline_after(uint64_t now_ms, uint64_t delay_ms)
{
    return UINT64_MAX - now_ms < delay_ms ? UINT64_MAX : now_ms + delay_ms;
}

static uint32_t retry_delay(unsigned attempt_count)
{
    static const uint32_t delays[] = {
        15000u, 30000u, 60000u, 120000u, 300000u, 900000u,
    };
    if (attempt_count == 0) {
        return delays[0];
    }
    size_t index = attempt_count - 1u;
    if (index >= sizeof(delays) / sizeof(delays[0])) {
        index = sizeof(delays) / sizeof(delays[0]) - 1u;
    }
    return delays[index];
}

static void schedule_failure_retry(pj_time_sync_state_t *state,
                                   pj_time_sync_failure_t failure,
                                   uint64_t now_ms)
{
    state->state = PJ_TIME_SYNC_FAILED;
    state->failure = failure;
    state->backoff_ms = retry_delay(state->attempt_count);
    state->next_attempt_ms = deadline_after(now_ms, state->backoff_ms);
    state->attempt_deadline_ms = 0;
}

void pj_time_sync_init(pj_time_sync_state_t *state, int time_known,
                       uint64_t now_ms)
{
    (void)now_ms;
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->time_known = time_known != 0;
    state->state = PJ_TIME_SYNC_WAITING_NETWORK;
    state->failure = PJ_TIME_SYNC_FAILURE_NETWORK;
    state->publication = PJ_TIME_SYNC_PUBLICATION_NOT_READY;
    state->correction = PJ_TIME_SYNC_CORRECTION_INITIAL;
}

void pj_time_sync_on_ip(pj_time_sync_state_t *state, uint64_t now_ms)
{
    if (state == NULL) {
        return;
    }
    state->has_ip = 1;
    state->failure = PJ_TIME_SYNC_FAILURE_NONE;
    if (state->last_success_monotonic_ms == 0 ||
        now_ms - state->last_success_monotonic_ms >= PJ_TIME_SYNC_REFRESH_MS) {
        state->state = state->last_success_monotonic_ms == 0
            ? PJ_TIME_SYNC_SCHEDULED : PJ_TIME_SYNC_STALE;
        state->next_attempt_ms = now_ms;
    } else {
        state->state = PJ_TIME_SYNC_SYNCHRONIZED;
    }
}

void pj_time_sync_on_network_lost(pj_time_sync_state_t *state,
                                  uint64_t now_ms)
{
    (void)now_ms;
    if (state == NULL) {
        return;
    }
    state->has_ip = 0;
    state->attempt_deadline_ms = 0;
    state->next_attempt_ms = 0;
    state->backoff_ms = 0;
    state->state = PJ_TIME_SYNC_WAITING_NETWORK;
    state->failure = PJ_TIME_SYNC_FAILURE_NETWORK;
}

void pj_time_sync_on_start_failed(pj_time_sync_state_t *state,
                                  uint64_t now_ms)
{
    if (state == NULL || !state->has_ip) {
        return;
    }
    schedule_failure_retry(state, PJ_TIME_SYNC_FAILURE_START, now_ms);
}

void pj_time_sync_on_system_clock_failed(pj_time_sync_state_t *state,
                                         uint64_t now_ms)
{
    if (state == NULL) {
        return;
    }
    schedule_failure_retry(state, PJ_TIME_SYNC_FAILURE_SYSTEM_CLOCK, now_ms);
}

int pj_time_sync_epoch_valid(int64_t epoch_s)
{
    return epoch_s >= PJ_TIME_SYNC_MIN_EPOCH_S &&
           epoch_s <= PJ_TIME_SYNC_MAX_EPOCH_S;
}

int pj_time_sync_expected_epoch_ms(const pj_time_sync_state_t *state,
                                   uint64_t now_ms, int64_t *epoch_ms)
{
    if (state == NULL || epoch_ms == NULL ||
        !pj_time_sync_epoch_valid(state->last_success_utc_s) ||
        now_ms < state->last_success_monotonic_ms) {
        return 0;
    }
    int64_t base_ms = state->last_success_utc_s * 1000ll;
    uint64_t elapsed_ms = now_ms - state->last_success_monotonic_ms;
    if (elapsed_ms > (uint64_t)(INT64_MAX - base_ms)) {
        return 0;
    }
    *epoch_ms = base_ms + (int64_t)elapsed_ms;
    return 1;
}

pj_time_sync_correction_t pj_time_sync_correction_policy(int old_time_valid,
                                                         int64_t old_epoch_ms,
                                                         int64_t new_epoch_ms)
{
    if (new_epoch_ms < PJ_TIME_SYNC_MIN_EPOCH_S * 1000ll ||
        new_epoch_ms > PJ_TIME_SYNC_MAX_EPOCH_S * 1000ll) {
        return PJ_TIME_SYNC_CORRECTION_REJECTED;
    }
    if (!old_time_valid) {
        return PJ_TIME_SYNC_CORRECTION_INITIAL;
    }
    int64_t delta = new_epoch_ms - old_epoch_ms;
    if (delta > PJ_TIME_SYNC_LARGE_CORRECTION_MS) {
        return PJ_TIME_SYNC_CORRECTION_STEP_FORWARD;
    }
    if (delta < -PJ_TIME_SYNC_LARGE_CORRECTION_MS) {
        return PJ_TIME_SYNC_CORRECTION_STEP_BACKWARD;
    }
    return PJ_TIME_SYNC_CORRECTION_SMALL;
}

int pj_time_sync_on_success(pj_time_sync_state_t *state, int64_t epoch_s,
                            int old_time_valid, int64_t old_epoch_ms,
                            uint64_t now_ms)
{
    if (state == NULL || !pj_time_sync_epoch_valid(epoch_s)) {
        if (state != NULL) {
            schedule_failure_retry(state, PJ_TIME_SYNC_FAILURE_INVALID_TIME,
                                   now_ms);
            state->correction = PJ_TIME_SYNC_CORRECTION_REJECTED;
        }
        return 0;
    }
    int64_t new_epoch_ms = epoch_s * 1000ll;
    state->correction = pj_time_sync_correction_policy(
        old_time_valid, old_epoch_ms, new_epoch_ms);
    state->last_offset_ms = old_time_valid ? new_epoch_ms - old_epoch_ms : 0;
    state->last_success_utc_s = epoch_s;
    state->last_success_monotonic_ms = now_ms;
    state->time_known = 1;
    state->state = PJ_TIME_SYNC_SYNCHRONIZED;
    state->failure = PJ_TIME_SYNC_FAILURE_NONE;
    state->publication = PJ_TIME_SYNC_PUBLICATION_TIMEZONE_REQUIRED;
    state->attempt_count = 0;
    state->backoff_ms = 0;
    state->attempt_deadline_ms = 0;
    state->next_attempt_ms = deadline_after(now_ms, PJ_TIME_SYNC_REFRESH_MS);
    return 1;
}

void pj_time_sync_set_publication(pj_time_sync_state_t *state,
                                  pj_time_sync_publication_t publication)
{
    if (state != NULL) {
        state->publication = publication;
    }
}

pj_time_sync_action_t pj_time_sync_tick(pj_time_sync_state_t *state,
                                        uint64_t now_ms)
{
    if (state == NULL) {
        return PJ_TIME_SYNC_ACTION_NONE;
    }
    if (state->last_success_monotonic_ms != 0 &&
        now_ms - state->last_success_monotonic_ms >= PJ_TIME_SYNC_STALE_MS &&
        state->state != PJ_TIME_SYNC_SYNCHRONIZING) {
        state->state = PJ_TIME_SYNC_STALE;
    }
    if (!state->has_ip) {
        return PJ_TIME_SYNC_ACTION_NONE;
    }
    if (state->state == PJ_TIME_SYNC_SYNCHRONIZING) {
        if (state->attempt_deadline_ms != 0 &&
            now_ms >= state->attempt_deadline_ms) {
            schedule_failure_retry(state, PJ_TIME_SYNC_FAILURE_TIMEOUT,
                                   now_ms);
        }
        return PJ_TIME_SYNC_ACTION_NONE;
    }
    if (state->state == PJ_TIME_SYNC_SYNCHRONIZED &&
        state->last_success_monotonic_ms != 0 &&
        now_ms - state->last_success_monotonic_ms >= PJ_TIME_SYNC_REFRESH_MS) {
        state->state = PJ_TIME_SYNC_SCHEDULED;
        state->next_attempt_ms = now_ms;
    }
    if ((state->state == PJ_TIME_SYNC_SCHEDULED ||
         state->state == PJ_TIME_SYNC_STALE ||
         state->state == PJ_TIME_SYNC_FAILED) &&
        now_ms >= state->next_attempt_ms) {
        state->state = PJ_TIME_SYNC_SYNCHRONIZING;
        state->failure = PJ_TIME_SYNC_FAILURE_NONE;
        if (state->attempt_count < UINT32_MAX) {
            state->attempt_count++;
        }
        state->attempt_deadline_ms = deadline_after(
            now_ms, PJ_TIME_SYNC_TIMEOUT_MS);
        state->next_attempt_ms = 0;
        return PJ_TIME_SYNC_ACTION_START;
    }
    return PJ_TIME_SYNC_ACTION_NONE;
}

const char *pj_time_sync_state_name(pj_time_sync_state_name_t state)
{
    switch (state) {
    case PJ_TIME_SYNC_UNKNOWN: return "unknown";
    case PJ_TIME_SYNC_WAITING_NETWORK: return "waiting_network";
    case PJ_TIME_SYNC_SCHEDULED: return "scheduled";
    case PJ_TIME_SYNC_SYNCHRONIZING: return "synchronizing";
    case PJ_TIME_SYNC_SYNCHRONIZED: return "synchronized";
    case PJ_TIME_SYNC_STALE: return "stale";
    case PJ_TIME_SYNC_FAILED: return "failed";
    default: return "unknown";
    }
}

const char *pj_time_sync_failure_name(pj_time_sync_failure_t failure)
{
    switch (failure) {
    case PJ_TIME_SYNC_FAILURE_NONE: return "none";
    case PJ_TIME_SYNC_FAILURE_NETWORK: return "network_unavailable";
    case PJ_TIME_SYNC_FAILURE_START: return "start_failed";
    case PJ_TIME_SYNC_FAILURE_TIMEOUT: return "timeout";
    case PJ_TIME_SYNC_FAILURE_INVALID_TIME: return "invalid_time";
    case PJ_TIME_SYNC_FAILURE_SYSTEM_CLOCK: return "system_clock_failed";
    default: return "invalid_time";
    }
}

const char *pj_time_sync_publication_name(pj_time_sync_publication_t publication)
{
    switch (publication) {
    case PJ_TIME_SYNC_PUBLICATION_NOT_READY: return "not_ready";
    case PJ_TIME_SYNC_PUBLICATION_TIMEZONE_REQUIRED: return "timezone_required";
    case PJ_TIME_SYNC_PUBLICATION_PUBLISHED: return "published";
    case PJ_TIME_SYNC_PUBLICATION_RTC_FAILED: return "rtc_failed";
    case PJ_TIME_SYNC_PUBLICATION_CIVIL_FAILED: return "civil_failed";
    default: return "not_ready";
    }
}

const char *pj_time_sync_correction_name(pj_time_sync_correction_t correction)
{
    switch (correction) {
    case PJ_TIME_SYNC_CORRECTION_INITIAL: return "initial";
    case PJ_TIME_SYNC_CORRECTION_SMALL: return "small";
    case PJ_TIME_SYNC_CORRECTION_STEP_FORWARD: return "step_forward";
    case PJ_TIME_SYNC_CORRECTION_STEP_BACKWARD: return "step_backward";
    case PJ_TIME_SYNC_CORRECTION_REJECTED: return "rejected";
    default: return "initial";
    }
}
