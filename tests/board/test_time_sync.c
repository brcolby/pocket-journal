#include "pj_time_sync.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define VALID_EPOCH_S 1784041200ll

static void test_waits_for_network_then_starts(void)
{
    pj_time_sync_state_t state;
    pj_time_sync_init(&state, 0, 100);
    assert(state.state == PJ_TIME_SYNC_WAITING_NETWORK);
    assert(state.failure == PJ_TIME_SYNC_FAILURE_NETWORK);
    assert(pj_time_sync_tick(&state, UINT64_MAX) == PJ_TIME_SYNC_ACTION_NONE);

    pj_time_sync_on_ip(&state, 200);
    assert(state.state == PJ_TIME_SYNC_SCHEDULED);
    assert(pj_time_sync_tick(&state, 200) == PJ_TIME_SYNC_ACTION_START);
    assert(state.state == PJ_TIME_SYNC_SYNCHRONIZING);
    assert(state.attempt_count == 1);
    assert(state.attempt_deadline_ms == 200 + PJ_TIME_SYNC_TIMEOUT_MS);
}

static void test_success_and_refresh(void)
{
    pj_time_sync_state_t state;
    pj_time_sync_init(&state, 0, 0);
    pj_time_sync_on_ip(&state, 10);
    assert(pj_time_sync_tick(&state, 10) == PJ_TIME_SYNC_ACTION_START);
    assert(pj_time_sync_on_success(&state, VALID_EPOCH_S, 0, 0, 20));
    assert(state.state == PJ_TIME_SYNC_SYNCHRONIZED);
    assert(state.failure == PJ_TIME_SYNC_FAILURE_NONE);
    assert(state.publication == PJ_TIME_SYNC_PUBLICATION_TIMEZONE_REQUIRED);
    assert(state.correction == PJ_TIME_SYNC_CORRECTION_INITIAL);
    assert(state.last_success_utc_s == VALID_EPOCH_S);
    assert(state.last_success_monotonic_ms == 20);
    assert(state.attempt_count == 0);
    assert(pj_time_sync_tick(&state, 20 + PJ_TIME_SYNC_REFRESH_MS - 1) ==
           PJ_TIME_SYNC_ACTION_NONE);
    assert(pj_time_sync_tick(&state, 20 + PJ_TIME_SYNC_REFRESH_MS) ==
           PJ_TIME_SYNC_ACTION_START);
}

static void test_timeout_uses_bounded_backoff(void)
{
    pj_time_sync_state_t state;
    pj_time_sync_init(&state, 0, 0);
    pj_time_sync_on_ip(&state, 1);
    uint64_t now = 1;
    const uint32_t expected[] = {15000, 30000, 60000, 120000, 300000, 900000, 900000};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        assert(pj_time_sync_tick(&state, now) == PJ_TIME_SYNC_ACTION_START);
        now += PJ_TIME_SYNC_TIMEOUT_MS;
        assert(pj_time_sync_tick(&state, now) == PJ_TIME_SYNC_ACTION_NONE);
        assert(state.state == PJ_TIME_SYNC_FAILED);
        assert(state.failure == PJ_TIME_SYNC_FAILURE_TIMEOUT);
        assert(state.backoff_ms == expected[i]);
        now += state.backoff_ms;
    }
}

static void test_start_failure_and_disconnect_cancel_retry(void)
{
    pj_time_sync_state_t state;
    pj_time_sync_init(&state, 1, 0);
    pj_time_sync_on_ip(&state, 100);
    assert(pj_time_sync_tick(&state, 100) == PJ_TIME_SYNC_ACTION_START);
    pj_time_sync_on_start_failed(&state, 110);
    assert(state.failure == PJ_TIME_SYNC_FAILURE_START);
    assert(state.backoff_ms == 15000);
    pj_time_sync_on_network_lost(&state, 120);
    assert(state.state == PJ_TIME_SYNC_WAITING_NETWORK);
    assert(state.failure == PJ_TIME_SYNC_FAILURE_NETWORK);
    assert(state.next_attempt_ms == 0);
    assert(pj_time_sync_tick(&state, UINT64_MAX) == PJ_TIME_SYNC_ACTION_NONE);

    pj_time_sync_on_system_clock_failed(&state, 130);
    assert(state.state == PJ_TIME_SYNC_FAILED);
    assert(state.failure == PJ_TIME_SYNC_FAILURE_SYSTEM_CLOCK);
}

static void test_invalid_time_is_rejected(void)
{
    pj_time_sync_state_t state;
    pj_time_sync_init(&state, 0, 0);
    pj_time_sync_on_ip(&state, 1);
    assert(pj_time_sync_tick(&state, 1) == PJ_TIME_SYNC_ACTION_START);
    assert(!pj_time_sync_on_success(&state, 0, 0, 0, 2));
    assert(state.state == PJ_TIME_SYNC_FAILED);
    assert(state.failure == PJ_TIME_SYNC_FAILURE_INVALID_TIME);
    assert(state.correction == PJ_TIME_SYNC_CORRECTION_REJECTED);
    assert(!pj_time_sync_epoch_valid(1704067199));
    assert(pj_time_sync_epoch_valid(1704067200));
    assert(pj_time_sync_epoch_valid(4102444799ll));
    assert(!pj_time_sync_epoch_valid(4102444800ll));
}

static void test_large_correction_policy(void)
{
    const int64_t now_ms = VALID_EPOCH_S * 1000ll;
    assert(pj_time_sync_correction_policy(0, 0, now_ms) ==
           PJ_TIME_SYNC_CORRECTION_INITIAL);
    assert(pj_time_sync_correction_policy(1, now_ms - 1000, now_ms) ==
           PJ_TIME_SYNC_CORRECTION_SMALL);
    assert(pj_time_sync_correction_policy(
               1, now_ms - PJ_TIME_SYNC_LARGE_CORRECTION_MS - 1, now_ms) ==
           PJ_TIME_SYNC_CORRECTION_STEP_FORWARD);
    assert(pj_time_sync_correction_policy(
               1, now_ms + PJ_TIME_SYNC_LARGE_CORRECTION_MS + 1, now_ms) ==
           PJ_TIME_SYNC_CORRECTION_STEP_BACKWARD);
    assert(pj_time_sync_correction_policy(1, now_ms, 0) ==
           PJ_TIME_SYNC_CORRECTION_REJECTED);

    pj_time_sync_state_t state;
    pj_time_sync_init(&state, 1, 0);
    pj_time_sync_on_ip(&state, 10);
    assert(pj_time_sync_tick(&state, 10) == PJ_TIME_SYNC_ACTION_START);
    assert(pj_time_sync_on_success(
        &state, VALID_EPOCH_S, 1,
        now_ms + PJ_TIME_SYNC_LARGE_CORRECTION_MS + 1, 20));
    assert(state.correction == PJ_TIME_SYNC_CORRECTION_STEP_BACKWARD);
    assert(state.last_offset_ms < -PJ_TIME_SYNC_LARGE_CORRECTION_MS);
}

static void test_stale_and_reconnect_policy(void)
{
    pj_time_sync_state_t state;
    pj_time_sync_init(&state, 0, 0);
    pj_time_sync_on_ip(&state, 10);
    assert(pj_time_sync_tick(&state, 10) == PJ_TIME_SYNC_ACTION_START);
    assert(pj_time_sync_on_success(&state, VALID_EPOCH_S, 0, 0, 20));
    pj_time_sync_on_network_lost(&state, 30);
    assert(state.state == PJ_TIME_SYNC_WAITING_NETWORK);
    pj_time_sync_on_ip(&state, 20 + PJ_TIME_SYNC_REFRESH_MS - 1);
    assert(state.state == PJ_TIME_SYNC_SYNCHRONIZED);
    pj_time_sync_on_network_lost(&state, 20 + PJ_TIME_SYNC_REFRESH_MS);
    pj_time_sync_on_ip(&state, 20 + PJ_TIME_SYNC_REFRESH_MS);
    assert(pj_time_sync_tick(&state, 20 + PJ_TIME_SYNC_REFRESH_MS) ==
           PJ_TIME_SYNC_ACTION_START);

    assert(pj_time_sync_on_success(&state, VALID_EPOCH_S + 100,
                                        1, VALID_EPOCH_S * 1000ll,
                                        1000));
    assert(pj_time_sync_tick(&state, 1000 + PJ_TIME_SYNC_STALE_MS) ==
           PJ_TIME_SYNC_ACTION_START);
}

static void test_status_names(void)
{
    assert(strcmp(pj_time_sync_state_name(PJ_TIME_SYNC_SYNCHRONIZED),
                  "synchronized") == 0);
    assert(strcmp(pj_time_sync_failure_name(PJ_TIME_SYNC_FAILURE_TIMEOUT),
                  "timeout") == 0);
    assert(strcmp(pj_time_sync_failure_name(
                      PJ_TIME_SYNC_FAILURE_SYSTEM_CLOCK),
                  "system_clock_failed") == 0);
    assert(strcmp(pj_time_sync_publication_name(
                      PJ_TIME_SYNC_PUBLICATION_TIMEZONE_REQUIRED),
                  "timezone_required") == 0);
    assert(strcmp(pj_time_sync_publication_name(
                      PJ_TIME_SYNC_PUBLICATION_CIVIL_FAILED),
                  "civil_failed") == 0);
    assert(strcmp(pj_time_sync_correction_name(
                      PJ_TIME_SYNC_CORRECTION_STEP_FORWARD),
                  "step_forward") == 0);
}

int main(void)
{
    test_waits_for_network_then_starts();
    test_success_and_refresh();
    test_timeout_uses_bounded_backoff();
    test_start_failure_and_disconnect_cancel_retry();
    test_invalid_time_is_rejected();
    test_large_correction_policy();
    test_stale_and_reconnect_policy();
    test_status_names();
    puts("time sync tests passed");
    return 0;
}
