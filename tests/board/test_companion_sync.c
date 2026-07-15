#include <assert.h>
#include <string.h>

#include "pj_companion_sync.h"

static void restore_after_reboot(pj_companion_sync_state_t *state)
{
    pj_companion_sync_record_t record;
    pj_companion_sync_record_from_state(state, &record);
    memset(state, 0xa5, sizeof(*state));
    assert(pj_companion_sync_state_from_record(state, &record));
}

int main(void)
{
    pj_companion_sync_state_t state;
    pj_companion_sync_state_init(&state);
    assert(strcmp(pj_companion_sync_phase_name(state.phase), "idle") == 0);
    assert(!pj_companion_sync_state_pending(&state));

    assert(pj_companion_sync_state_request(&state, "pj-34211"));
    assert(state.phase == PJ_COMPANION_SYNC_PENDING);
    assert(state.requested_generation == 1U);
    assert(strcmp(state.operation_id, "pj-34211-00000001") == 0);
    assert(pj_companion_sync_state_pending(&state));

    restore_after_reboot(&state);
    assert(state.phase == PJ_COMPANION_SYNC_PENDING);
    assert(state.online == 0);
    assert(state.requested_generation == 1U);

    assert(pj_companion_sync_state_claim(
        &state, 1U, "pj-34211-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_LAN) == PJ_COMPANION_SYNC_CLAIM_STARTED);
    assert(pj_companion_sync_state_claim(
        &state, 1U, "pj-34211-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB) == PJ_COMPANION_SYNC_CLAIM_BUSY);
    assert(pj_companion_sync_state_claim(
        &state, 1U, "pj-34211-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_LAN) == PJ_COMPANION_SYNC_CLAIM_ATTACHED);
    assert(pj_companion_sync_state_discovered(&state, 1U));
    assert(state.phase == PJ_COMPANION_SYNC_REQUESTING);

    assert(pj_companion_sync_state_progress(
        &state, 1U, "pj-34211-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_LAN, "running", 2, 0, 0, NULL,
        "pj-34211") == PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(state.pending == 2);

    /* A later device action is durable and cannot be cleared by generation 1. */
    assert(pj_companion_sync_state_request(&state, "pj-34211"));
    assert(state.requested_generation == 2U);
    assert(state.active_generation == 1U);
    assert(pj_companion_sync_state_progress(
        &state, 1U, "pj-34211-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_LAN, "succeeded", 0, 2, 0, NULL,
        "pj-34211") == PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(state.acknowledged_generation == 1U);
    assert(state.requested_generation == 2U);
    assert(state.phase == PJ_COMPANION_SYNC_PENDING);
    assert(strcmp(state.operation_id, "pj-34211-00000002") == 0);

    /* Lost terminal responses are idempotent and do not consume generation 2. */
    assert(pj_companion_sync_state_progress(
        &state, 1U, "pj-34211-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_LAN, "succeeded", 0, 2, 0, NULL,
        "pj-34211") == PJ_COMPANION_SYNC_APPLY_REPLAY);
    assert(state.acknowledged_generation == 1U);
    assert(pj_companion_sync_state_pending(&state));

    assert(pj_companion_sync_state_claim(
        &state, 2U, "pj-34211-00000002",
        PJ_COMPANION_SYNC_TRANSPORT_USB) == PJ_COMPANION_SYNC_CLAIM_STARTED);
    restore_after_reboot(&state);
    assert(state.active_generation == 2U);
    assert(state.phase == PJ_COMPANION_SYNC_PENDING);
    assert(pj_companion_sync_state_claim(
        &state, 2U, "pj-34211-00000002",
        PJ_COMPANION_SYNC_TRANSPORT_USB) == PJ_COMPANION_SYNC_CLAIM_STARTED);
    assert(pj_companion_sync_state_progress(
        &state, 2U, "pj-34211-00000002",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "failed", 1, 0, 1,
        "microphone noise", "pj-34211") == PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(state.phase == PJ_COMPANION_SYNC_FAILED);
    assert(!pj_companion_sync_state_pending(&state));
    assert(state.acknowledged_generation == 2U);
    assert(strcmp(state.error, "microphone noise") == 0);

    assert(pj_companion_sync_state_request(&state, "pj-34211"));
    assert(pj_companion_sync_state_claim(
        &state, 3U, "pj-34211-00000003",
        PJ_COMPANION_SYNC_TRANSPORT_LAN) == PJ_COMPANION_SYNC_CLAIM_STARTED);
    assert(pj_companion_sync_state_attempt_failed(
        &state, 3U, PJ_COMPANION_SYNC_TRANSPORT_LAN,
        "Wi-Fi is not connected"));
    assert(state.phase == PJ_COMPANION_SYNC_FAILED);
    assert(pj_companion_sync_state_pending(&state));
    restore_after_reboot(&state);
    assert(pj_companion_sync_state_pending(&state));
    assert(strcmp(state.error, "Wi-Fi is not connected") == 0);

    pj_companion_sync_record_t corrupt;
    pj_companion_sync_record_from_state(&state, &corrupt);
    assert(!pj_companion_sync_state_from_record_bytes(
        &state, &corrupt, sizeof(corrupt) - 1U));
    corrupt.requested_generation++;
    assert(!pj_companion_sync_state_from_record(&state, &corrupt));

    pj_companion_sync_state_init(&state);
    state.requested_generation = UINT32_MAX;
    state.acknowledged_generation = UINT32_MAX;
    assert(!pj_companion_sync_state_request(&state, "pj-34211"));
    assert(state.requested_generation == UINT32_MAX);
    return 0;
}
