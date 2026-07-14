#include "pj_wifi_state.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_unprovisioned_is_disabled(void)
{
    pj_wifi_state_t state;
    pj_wifi_state_init(&state, 0, 100);
    assert(state.phase == PJ_WIFI_PHASE_UNPROVISIONED);
    assert(state.retry_state == PJ_WIFI_RETRY_DISABLED);
    assert(pj_wifi_state_tick(&state, UINT64_MAX) == PJ_WIFI_ACTION_NONE);
}

static void test_connect_associate_and_dhcp_success(void)
{
    pj_wifi_state_t state;
    pj_wifi_state_init(&state, 1, 100);
    assert(state.phase == PJ_WIFI_PHASE_DISCONNECTED);
    assert(pj_wifi_state_tick(&state, 99) == PJ_WIFI_ACTION_NONE);
    assert(pj_wifi_state_tick(&state, 100) == PJ_WIFI_ACTION_CONNECT);
    assert(state.phase == PJ_WIFI_PHASE_CONNECTING);
    assert(state.retry_state == PJ_WIFI_RETRY_RETRYING);
    assert(state.connect_deadline_ms == 100 + PJ_WIFI_CONNECT_TIMEOUT_MS);

    pj_wifi_state_on_associated(&state, 200);
    assert(state.phase == PJ_WIFI_PHASE_DHCP);
    assert(state.dhcp_state == PJ_WIFI_DHCP_REQUESTING);
    assert(state.connect_deadline_ms == 0);
    assert(state.dhcp_deadline_ms == 200 + PJ_WIFI_DHCP_TIMEOUT_MS);

    pj_wifi_state_on_got_ip(&state, 400, -57, 11);
    assert(state.phase == PJ_WIFI_PHASE_CONNECTED);
    assert(state.has_ip == 1);
    assert(state.dhcp_state == PJ_WIFI_DHCP_BOUND);
    assert(state.retry_state == PJ_WIFI_RETRY_IDLE);
    assert(state.retry_count == 0);
    assert(state.rssi_dbm == -57);
    assert(state.channel == 11);
    assert(state.last_success_monotonic_ms == 400);
    assert(strcmp(pj_wifi_phase_name(state.phase), "connected") == 0);
}

static void test_missing_driver_event_times_out_and_reconnects(void)
{
    pj_wifi_state_t state;
    pj_wifi_state_init(&state, 1, 100);
    assert(pj_wifi_state_tick(&state, 100) == PJ_WIFI_ACTION_CONNECT);
    uint64_t deadline = 100 + PJ_WIFI_CONNECT_TIMEOUT_MS;

    assert(pj_wifi_state_tick(&state, deadline - 1) == PJ_WIFI_ACTION_NONE);
    assert(state.phase == PJ_WIFI_PHASE_CONNECTING);
    assert(state.retry_count == 0);

    assert(pj_wifi_state_tick(&state, deadline) == PJ_WIFI_ACTION_NONE);
    assert(state.phase == PJ_WIFI_PHASE_CONNECT_TIMEOUT);
    assert(state.retry_state == PJ_WIFI_RETRY_BACKOFF);
    assert(state.connect_deadline_ms == 0);
    assert(state.retry_count == 1);
    assert(state.backoff_ms == 2000);
    assert(strcmp(pj_wifi_phase_name(state.phase), "connect_timeout") == 0);

    assert(pj_wifi_state_tick(&state, deadline + 1999) ==
           PJ_WIFI_ACTION_NONE);
    assert(pj_wifi_state_tick(&state, deadline + 2000) ==
           PJ_WIFI_ACTION_RECONNECT);
    assert(state.connect_deadline_ms ==
           deadline + 2000 + PJ_WIFI_CONNECT_TIMEOUT_MS);

    uint64_t second_deadline = state.connect_deadline_ms;
    assert(pj_wifi_state_tick(&state, second_deadline) ==
           PJ_WIFI_ACTION_NONE);
    assert(state.phase == PJ_WIFI_PHASE_CONNECT_TIMEOUT);
    assert(state.retry_count == 2);
    assert(state.backoff_ms == 4000);
}

static void test_disconnect_classification_and_backoff(void)
{
    struct {
        unsigned reason;
        pj_wifi_disconnect_class_t classification;
        pj_wifi_phase_t phase;
        int visible;
    } cases[] = {
        {202, PJ_WIFI_DISCONNECT_AUTHENTICATION,
         PJ_WIFI_PHASE_AUTHENTICATION_FAILED, 1},
        {201, PJ_WIFI_DISCONNECT_ACCESS_POINT_UNAVAILABLE,
         PJ_WIFI_PHASE_ACCESS_POINT_UNAVAILABLE, 0},
        {203, PJ_WIFI_DISCONNECT_ASSOCIATION,
         PJ_WIFI_PHASE_ASSOCIATION_FAILED, 1},
        {999, PJ_WIFI_DISCONNECT_OTHER,
         PJ_WIFI_PHASE_DISCONNECTED, -1},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        pj_wifi_state_t state;
        pj_wifi_state_init(&state, 1, 0);
        assert(pj_wifi_state_tick(&state, 0) == PJ_WIFI_ACTION_CONNECT);
        pj_wifi_state_on_disconnected(&state, cases[i].reason, 500);
        assert(pj_wifi_disconnect_classify(cases[i].reason) ==
               cases[i].classification);
        assert(state.phase == cases[i].phase);
        assert(state.ap_visible == cases[i].visible);
        assert(state.last_disconnect_reason ==
               (cases[i].reason > UINT16_MAX ? UINT16_MAX : cases[i].reason));
        assert(state.connect_deadline_ms == 0);
        assert(state.retry_state == PJ_WIFI_RETRY_BACKOFF);
        assert(state.retry_count == 1);
        assert(state.backoff_ms == 2000);
        assert(pj_wifi_state_tick(&state, 2499) == PJ_WIFI_ACTION_NONE);
        assert(pj_wifi_state_tick(&state, 2500) == PJ_WIFI_ACTION_CONNECT);
    }
}

static void test_backoff_is_bounded_and_resets_on_success(void)
{
    pj_wifi_state_t state;
    pj_wifi_state_init(&state, 1, 0);
    assert(pj_wifi_state_tick(&state, 0) == PJ_WIFI_ACTION_CONNECT);
    uint64_t now = 1;
    const uint32_t expected[] = {2000, 4000, 8000, 15000, 30000, 60000, 60000};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        pj_wifi_state_on_disconnected(&state, 200, now);
        assert(state.backoff_ms == expected[i]);
        now += state.backoff_ms;
        assert(pj_wifi_state_tick(&state, now) == PJ_WIFI_ACTION_CONNECT);
        now++;
    }
    assert(state.backoff_ms <= PJ_WIFI_RETRY_MAX_MS);
    pj_wifi_state_on_associated(&state, now);
    pj_wifi_state_on_got_ip(&state, now + 1, -40, 6);
    assert(state.retry_count == 0);
    assert(state.backoff_ms == 0);
}

static void test_dhcp_timeout_reconnects_after_backoff(void)
{
    pj_wifi_state_t state;
    pj_wifi_state_init(&state, 1, 0);
    assert(pj_wifi_state_tick(&state, 0) == PJ_WIFI_ACTION_CONNECT);
    pj_wifi_state_on_associated(&state, 10);
    assert(pj_wifi_state_tick(&state, 10 + PJ_WIFI_DHCP_TIMEOUT_MS - 1) ==
           PJ_WIFI_ACTION_NONE);
    assert(pj_wifi_state_tick(&state, 10 + PJ_WIFI_DHCP_TIMEOUT_MS) ==
           PJ_WIFI_ACTION_NONE);
    assert(state.phase == PJ_WIFI_PHASE_DHCP_FAILED);
    assert(state.dhcp_state == PJ_WIFI_DHCP_TIMEOUT);
    assert(state.backoff_ms == 2000);
    assert(pj_wifi_state_tick(&state,
                              10 + PJ_WIFI_DHCP_TIMEOUT_MS + 2000) ==
           PJ_WIFI_ACTION_RECONNECT);
}

static void test_lost_ip_and_failed_connect_request_retry(void)
{
    pj_wifi_state_t state;
    pj_wifi_state_init(&state, 1, 5);
    assert(pj_wifi_state_tick(&state, 5) == PJ_WIFI_ACTION_CONNECT);
    pj_wifi_state_on_associated(&state, 10);
    pj_wifi_state_on_got_ip(&state, 20, -70, 1);
    pj_wifi_state_on_lost_ip(&state, 30);
    assert(state.phase == PJ_WIFI_PHASE_DHCP_FAILED);
    assert(state.has_ip == 0);
    assert(pj_wifi_state_tick(&state, 2030) == PJ_WIFI_ACTION_RECONNECT);
    pj_wifi_state_on_connect_request_failed(&state, 2040);
    assert(state.phase == PJ_WIFI_PHASE_FAILED);
    assert(state.retry_count == 2);
    assert(state.backoff_ms == 4000);
}

static void test_reprovision_and_success_metadata(void)
{
    pj_wifi_state_t state;
    pj_wifi_state_init(&state, 0, 0);
    pj_wifi_state_set_provisioned(&state, 1, 50);
    assert(pj_wifi_state_tick(&state, 50) == PJ_WIFI_ACTION_CONNECT);
    pj_wifi_state_on_associated(&state, 60);
    pj_wifi_state_on_got_ip(&state, 70, -30, 2000);
    assert(state.channel == UINT8_MAX);
    pj_wifi_state_set_last_success_utc(&state, 1784041200);
    assert(state.last_success_utc_s == 1784041200);
    pj_wifi_state_set_provisioned(&state, 1, 75);
    assert(state.last_success_monotonic_ms == 0);
    assert(state.last_success_utc_s == 0);
    pj_wifi_state_set_provisioned(&state, 0, 80);
    assert(state.phase == PJ_WIFI_PHASE_UNPROVISIONED);
    assert(state.has_ip == 0);
}

int main(void)
{
    test_unprovisioned_is_disabled();
    test_connect_associate_and_dhcp_success();
    test_missing_driver_event_times_out_and_reconnects();
    test_disconnect_classification_and_backoff();
    test_backoff_is_bounded_and_resets_on_success();
    test_dhcp_timeout_reconnects_after_backoff();
    test_lost_ip_and_failed_connect_request_retry();
    test_reprovision_and_success_metadata();
    puts("wifi state tests passed");
    return 0;
}
