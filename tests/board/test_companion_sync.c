#include <assert.h>
#include <string.h>

#include "pj_companion_sync.h"

static int persist_calls;
static int persist_result;

static int persist_test_state(void *context)
{
    (void)context;
    persist_calls++;
    return persist_result;
}

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
    assert(pj_companion_sync_scope_allowed("GET", "/v1/audio"));
    assert(pj_companion_sync_scope_allowed("GET", "/v1/audio/note.wav"));
    assert(pj_companion_sync_scope_allowed(
        "PUT", "/v1/transcripts/note.wav"));
    assert(!pj_companion_sync_scope_allowed("DELETE", "/v1/audio"));
    assert(!pj_companion_sync_scope_allowed("PUT", "/v1/audio"));
    assert(!pj_companion_sync_scope_allowed("GET", "/v1/audioevil"));
    assert(!pj_companion_sync_scope_allowed("GET", "/v1/audio/../secret"));
    assert(!pj_companion_sync_scope_allowed("get", "/v1/audio"));
    assert(!pj_companion_sync_scope_allowed("GET", "/v1/audio/note.wav?x=1"));
    assert(!pj_companion_sync_scope_allowed("DELETE", "/v1/audio/note.wav"));
    assert(!pj_companion_sync_scope_allowed(
        "PUT", "/v1/transcripts/note.wav/other"));

    assert(pj_companion_sync_state_request(&state, "pj-34211", 1000U));
    assert(state.phase == PJ_COMPANION_SYNC_PENDING);
    assert(state.requested_generation == 1U);
    assert(state.requested_ms == 1000U);
    assert(strcmp(state.operation_id, "pj-34211-00000001") == 0);

    restore_after_reboot(&state);
    assert(state.phase == PJ_COMPANION_SYNC_PENDING);
    assert(state.requested_generation == 1U);

    assert(pj_companion_sync_state_claim(
        &state, 1U, "pj-34211-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_LAN) == PJ_COMPANION_SYNC_CLAIM_STARTED);
    assert(state.active_requested_ms == 1000U);
    assert(pj_companion_sync_state_claim(
        &state, 1U, "pj-34211-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB) == PJ_COMPANION_SYNC_CLAIM_BUSY);
    assert(pj_companion_sync_state_discovered(&state, 1U));
    assert(pj_companion_sync_state_progress(
        &state, 1U, "pj-34211-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_LAN, "running", 2, 2, 0, 0, "",
        "pj-34211") == PJ_COMPANION_SYNC_APPLY_CHANGED);

    /* A later tap cannot replace the active operation identity. */
    assert(pj_companion_sync_state_request(&state, "pj-34211", 2000U));
    assert(state.requested_generation == 2U);
    assert(state.requested_ms == 2000U);
    assert(state.active_generation == 1U);
    assert(state.active_requested_ms == 1000U);
    assert(strcmp(state.operation_id, "pj-34211-00000001") == 0);

    /* A transient loss releases only transport ownership, not the claim. */
    assert(pj_companion_sync_state_attempt_failed(
        &state, 1U, PJ_COMPANION_SYNC_TRANSPORT_LAN,
        PJ_COMPANION_SYNC_OFFLINE, "Wi-Fi is not connected"));
    assert(state.active_generation == 1U);
    assert(state.transport == PJ_COMPANION_SYNC_TRANSPORT_NONE);
    assert(state.phase == PJ_COMPANION_SYNC_OFFLINE);
    assert(pj_companion_sync_state_claim_generation(&state) == 1U);
    restore_after_reboot(&state);
    assert(state.active_generation == 1U);
    assert(state.requested_generation == 2U);
    assert(strcmp(state.operation_id, "pj-34211-00000001") == 0);
    assert(pj_companion_sync_state_claim(
        &state, 1U, "pj-34211-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_LAN) == PJ_COMPANION_SYNC_CLAIM_ATTACHED);
    assert(pj_companion_sync_state_discovered(&state, 1U));

    assert(pj_companion_sync_state_progress(
        &state, 1U, "pj-34211-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_LAN, "succeeded", 2, 0, 2, 0, "",
        "pj-34211") == PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(state.acknowledged_generation == 1U);
    assert(state.acknowledged_requested_ms == 1000U);
    assert(state.requested_generation == 2U);
    assert(state.phase == PJ_COMPANION_SYNC_PENDING);
    assert(strcmp(state.operation_id, "pj-34211-00000002") == 0);

    /* A terminal replay must match outcome, all counts, and error exactly. */
    assert(pj_companion_sync_state_progress(
        &state, 1U, "pj-34211-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_LAN, "succeeded", 2, 0, 2, 0, "",
        "pj-34211") == PJ_COMPANION_SYNC_APPLY_REPLAY);
    assert(pj_companion_sync_state_progress(
        &state, 1U, "pj-34211-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_LAN, "failed", 2, 0, 2, 0,
        "different", "pj-34211") == PJ_COMPANION_SYNC_APPLY_REJECTED);
    restore_after_reboot(&state);
    assert(pj_companion_sync_state_progress(
        &state, 1U, "pj-34211-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "succeeded", 2, 0, 2, 0, "",
        "pj-34211") == PJ_COMPANION_SYNC_APPLY_REPLAY);

    assert(pj_companion_sync_state_claim(
        &state, 2U, "pj-34211-00000002",
        PJ_COMPANION_SYNC_TRANSPORT_USB) == PJ_COMPANION_SYNC_CLAIM_STARTED);
    assert(pj_companion_sync_state_claim(
        &state, 2U, "pj-34211-00000002",
        PJ_COMPANION_SYNC_TRANSPORT_LAN) == PJ_COMPANION_SYNC_CLAIM_BUSY);
    restore_after_reboot(&state);
    assert(state.active_generation == 2U);
    assert(state.active_requested_ms == 2000U);
    assert(pj_companion_sync_state_claim(
        &state, 2U, "pj-34211-00000002",
        PJ_COMPANION_SYNC_TRANSPORT_USB) == PJ_COMPANION_SYNC_CLAIM_ATTACHED);
    assert(pj_companion_sync_state_progress(
        &state, 2U, "pj-34211-00000002",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "failed", 1, 0, 0, 1,
        "microphone noise", "pj-34211") == PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(state.phase == PJ_COMPANION_SYNC_FAILED);
    assert(!pj_companion_sync_state_pending(&state));
    restore_after_reboot(&state);
    assert(pj_companion_sync_state_progress(
        &state, 2U, "pj-34211-00000002",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "failed", 1, 0, 0, 1,
        "microphone noise", "pj-34211") == PJ_COMPANION_SYNC_APPLY_REPLAY);
    assert(pj_companion_sync_state_progress(
        &state, 2U, "pj-34211-00000002",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "succeeded", 1, 0, 1, 0, "",
        "pj-34211") == PJ_COMPANION_SYNC_APPLY_REJECTED);

    /* A retry returns to the byte-identical durable claim, avoiding a write. */
    assert(pj_companion_sync_state_request(&state, "pj-34211", 3000U));
    assert(pj_companion_sync_state_claim(
        &state, 3U, "pj-34211-00000003",
        PJ_COMPANION_SYNC_TRANSPORT_LAN) == PJ_COMPANION_SYNC_CLAIM_STARTED);
    pj_companion_sync_record_t first_claim;
    pj_companion_sync_record_from_state(&state, &first_claim);
    assert(pj_companion_sync_state_attempt_failed(
        &state, 3U, PJ_COMPANION_SYNC_TRANSPORT_LAN,
        PJ_COMPANION_SYNC_OFFLINE, "connection lost"));
    assert(pj_companion_sync_state_claim(
        &state, 3U, "pj-34211-00000003",
        PJ_COMPANION_SYNC_TRANSPORT_LAN) == PJ_COMPANION_SYNC_CLAIM_ATTACHED);
    pj_companion_sync_record_t retry_claim;
    pj_companion_sync_record_from_state(&state, &retry_claim);
    assert(pj_companion_sync_record_equal(&first_claim, &retry_claim));

    assert(pj_companion_sync_state_discovered(&state, 3U));
    assert(pj_companion_sync_state_request(&state, "pj-34211", 4000U));
    restore_after_reboot(&state);
    assert(state.active_generation == 3U);
    assert(state.requested_generation == 4U);
    assert(state.active_requested_ms == 3000U);
    assert(state.requested_ms == 4000U);
    assert(strcmp(state.operation_id, "pj-34211-00000003") == 0);

    assert(pj_companion_sync_state_claim(
        &state, 3U, "pj-34211-00000003",
        PJ_COMPANION_SYNC_TRANSPORT_LAN) == PJ_COMPANION_SYNC_CLAIM_ATTACHED);
    assert(pj_companion_sync_state_discovered(&state, 3U));
    assert(pj_companion_sync_state_progress(
        &state, 3U, "pj-34211-00000003",
        PJ_COMPANION_SYNC_TRANSPORT_LAN, "succeeded", 0, 0, 0, 0, "",
        "pj-34211") == PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(strcmp(state.operation_id, "pj-34211-00000004") == 0);
    assert(state.requested_ms == 4000U);

    /* Count inconsistencies and unsafe terminal text fail closed. */
    assert(pj_companion_sync_state_claim(
        &state, 4U, "pj-34211-00000004",
        PJ_COMPANION_SYNC_TRANSPORT_USB) == PJ_COMPANION_SYNC_CLAIM_STARTED);
    assert(pj_companion_sync_state_progress(
        &state, 4U, "pj-34211-00000004",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "running", 2, 2, 1, 0, "",
        "pj-34211") == PJ_COMPANION_SYNC_APPLY_REJECTED);
    assert(pj_companion_sync_state_progress(
        &state, 4U, "pj-34211-00000004",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "failed", 1, 0, 0, 1,
        "bad\nerror", "pj-34211") == PJ_COMPANION_SYNC_APPLY_REJECTED);

    /* Running progress is RAM-only; a failed terminal commit rolls back. */
    pj_companion_sync_state_t transactional;
    pj_companion_sync_state_init(&transactional);
    assert(pj_companion_sync_state_request(
        &transactional, "pj-commit", 6000U));
    assert(pj_companion_sync_state_claim(
        &transactional, 1U, "pj-commit-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB) == PJ_COMPANION_SYNC_CLAIM_STARTED);
    persist_calls = 0;
    persist_result = 0;
    assert(pj_companion_sync_state_progress_transactional(
        &transactional, 1U, "pj-commit-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "running", 1, 1, 0, 0, "",
        "pj-commit", persist_test_state, NULL) ==
        PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(persist_calls == 0);
    assert(pj_companion_sync_state_progress_transactional(
        &transactional, 1U, "pj-commit-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "succeeded", 1, 0, 1, 0, "",
        "pj-commit", persist_test_state, NULL) ==
        PJ_COMPANION_SYNC_APPLY_STORE_FAILED);
    assert(persist_calls == 1);
    assert(transactional.active_generation == 1U);
    assert(transactional.acknowledged_generation == 0U);
    persist_result = 1;
    assert(pj_companion_sync_state_progress_transactional(
        &transactional, 1U, "pj-commit-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "succeeded", 1, 0, 1, 0, "",
        "pj-commit", persist_test_state, NULL) ==
        PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(persist_calls == 2);
    assert(pj_companion_sync_state_progress_transactional(
        &transactional, 1U, "pj-commit-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "succeeded", 1, 0, 1, 0, "",
        "pj-commit", persist_test_state, NULL) ==
        PJ_COMPANION_SYNC_APPLY_REPLAY);
    assert(persist_calls == 2);

    pj_companion_sync_state_t impossible_terminal = transactional;
    strcpy(impossible_terminal.error, "impossible success error");
    strcpy(impossible_terminal.acknowledged_error,
           "impossible success error");
    pj_companion_sync_record_t impossible_record;
    pj_companion_sync_record_from_state(&impossible_terminal,
                                        &impossible_record);
    assert(!pj_companion_sync_state_from_record(&state, &impossible_record));

    pj_companion_sync_record_t corrupt;
    pj_companion_sync_record_from_state(&state, &corrupt);
    assert(!pj_companion_sync_state_from_record_bytes(
        &state, &corrupt, sizeof(corrupt) - 1U));
    corrupt.requested_generation++;
    assert(!pj_companion_sync_state_from_record(&state, &corrupt));

    pj_companion_sync_state_init(&state);
    state.requested_generation = UINT32_MAX;
    state.acknowledged_generation = UINT32_MAX;
    assert(!pj_companion_sync_state_request(&state, "pj-34211", 5000U));
    assert(state.requested_generation == UINT32_MAX);
    return 0;
}
