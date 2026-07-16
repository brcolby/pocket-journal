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

static void test_upload_swap_terminal_barrier(void)
{
    pj_companion_sync_state_t state;
    pj_companion_sync_state_init(&state);
    assert(pj_companion_sync_state_request(
        &state, "pj-mutation", 1000U));
    assert(pj_companion_sync_state_claim(
        &state, 1U, "pj-mutation-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB) == PJ_COMPANION_SYNC_CLAIM_STARTED);

    pj_companion_sync_mutation_barrier_t barrier;
    pj_companion_sync_mutation_barrier_init(&barrier);
    assert(pj_companion_sync_mutation_barrier_bind(&barrier, 1U));
    assert(pj_companion_sync_mutation_barrier_terminal_current(
        &barrier, 1U));

    /* The raw upload finishes before enhancement replaces its source WAV. */
    assert(pj_companion_sync_state_progress(
        &state, 1U, "pj-mutation-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "running", 1, 0, 1, 0, "",
        "pj-mutation") == PJ_COMPANION_SYNC_APPLY_CHANGED);
    persist_calls = 0;
    persist_result = 1;
    assert(pj_companion_sync_prepare_inventory_mutation_transactional(
        &barrier, &state, 1, "pj-mutation", 2000U,
        persist_test_state, NULL) == PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(persist_calls == 1);
    assert(state.active_generation == 1U);
    assert(state.requested_generation == 2U);
    assert(!pj_companion_sync_mutation_barrier_terminal_current(
        &barrier, 1U));

    /* More swaps coalesce into the already durable next generation. */
    assert(pj_companion_sync_prepare_inventory_mutation_transactional(
        &barrier, &state, 1, "pj-mutation", 3000U,
        persist_test_state, NULL) == PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(persist_calls == 1);
    assert(state.requested_generation == 2U);

    /* A failed stale-terminal commit preserves the active, stale claim. */
    pj_companion_sync_state_t active_before = state;
    persist_result = 0;
    assert(pj_companion_sync_state_progress_transactional(
        &state, 1U, "pj-mutation-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "succeeded", 1, 0, 1, 0, "",
        "pj-mutation", persist_test_state, NULL) ==
        PJ_COMPANION_SYNC_APPLY_STORE_FAILED);
    assert(memcmp(&state, &active_before, sizeof(state)) == 0);
    assert(state.acknowledged_generation == 0U);
    assert(!pj_companion_sync_mutation_barrier_terminal_current(
        &barrier, 1U));

    persist_result = 1;
    assert(pj_companion_sync_state_progress_transactional(
        &state, 1U, "pj-mutation-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "succeeded", 1, 0, 1, 0, "",
        "pj-mutation", persist_test_state, NULL) ==
        PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(state.phase == PJ_COMPANION_SYNC_PENDING);
    assert(state.active_generation == 0U);
    assert(state.acknowledged_generation == 1U);
    assert(state.acknowledged_phase == PJ_COMPANION_SYNC_SUCCEEDED);
    assert(strcmp(state.operation_id, "pj-mutation-00000002") == 0);
    assert(pj_companion_sync_state_progress_transactional(
        &state, 1U, "pj-mutation-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "succeeded", 1, 0, 1, 0, "",
        "pj-mutation", persist_test_state, NULL) ==
        PJ_COMPANION_SYNC_APPLY_REPLAY);
    restore_after_reboot(&state);
    assert(state.phase == PJ_COMPANION_SYNC_PENDING);
    assert(state.requested_generation == 2U);

    pj_companion_sync_mutation_barrier_release(&barrier, 1U);
    assert(pj_companion_sync_state_claim(
        &state, 2U, "pj-mutation-00000002",
        PJ_COMPANION_SYNC_TRANSPORT_USB) == PJ_COMPANION_SYNC_CLAIM_STARTED);
    assert(pj_companion_sync_mutation_barrier_bind(&barrier, 2U));
    assert(pj_companion_sync_mutation_barrier_terminal_current(
        &barrier, 2U));
    assert(pj_companion_sync_state_progress(
        &state, 2U, "pj-mutation-00000002",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "succeeded", 1, 0, 1, 0, "",
        "pj-mutation") == PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(state.phase == PJ_COMPANION_SYNC_SUCCEEDED);
}

static void test_terminal_then_swap_requeues_once(void)
{
    pj_companion_sync_state_t state;
    pj_companion_sync_state_init(&state);
    assert(pj_companion_sync_state_request(
        &state, "pj-after", 1000U));
    assert(pj_companion_sync_state_claim(
        &state, 1U, "pj-after-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB) == PJ_COMPANION_SYNC_CLAIM_STARTED);
    assert(pj_companion_sync_state_progress(
        &state, 1U, "pj-after-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "succeeded", 1, 0, 1, 0, "",
        "pj-after") == PJ_COMPANION_SYNC_APPLY_CHANGED);

    pj_companion_sync_mutation_barrier_t barrier;
    pj_companion_sync_mutation_barrier_init(&barrier);
    persist_calls = 0;
    persist_result = 1;
    assert(pj_companion_sync_prepare_inventory_mutation_transactional(
        &barrier, &state, 1, "pj-after", 2000U,
        persist_test_state, NULL) == PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(persist_calls == 1);
    assert(state.phase == PJ_COMPANION_SYNC_PENDING);
    assert(state.requested_generation == 2U);
    assert(pj_companion_sync_prepare_inventory_mutation_transactional(
        &barrier, &state, 1, "pj-after", 3000U,
        persist_test_state, NULL) == PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(persist_calls == 1);
    assert(state.requested_generation == 2U);
}

static void test_mutation_prepare_persist_failure_blocks_publication(void)
{
    pj_companion_sync_state_t state;
    pj_companion_sync_state_init(&state);
    assert(pj_companion_sync_state_request(
        &state, "pj-store", 1000U));
    assert(pj_companion_sync_state_claim(
        &state, 1U, "pj-store-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB) == PJ_COMPANION_SYNC_CLAIM_STARTED);
    pj_companion_sync_mutation_barrier_t barrier;
    pj_companion_sync_mutation_barrier_init(&barrier);
    assert(pj_companion_sync_mutation_barrier_bind(&barrier, 1U));

    pj_companion_sync_state_t state_before = state;
    pj_companion_sync_mutation_barrier_t barrier_before = barrier;
    persist_calls = 0;
    persist_result = 0;
    assert(pj_companion_sync_prepare_inventory_mutation_transactional(
        &barrier, &state, 1, "pj-store", 2000U,
        persist_test_state, NULL) == PJ_COMPANION_SYNC_APPLY_STORE_FAILED);
    assert(persist_calls == 1);
    assert(memcmp(&state, &state_before, sizeof(state)) == 0);
    assert(memcmp(&barrier, &barrier_before, sizeof(barrier)) == 0);
    assert(pj_companion_sync_mutation_barrier_terminal_current(
        &barrier, 1U));
}

static void test_idle_inventory_mutations_do_not_autoqueue(void)
{
    pj_companion_sync_state_t state;
    pj_companion_sync_state_init(&state);
    pj_companion_sync_mutation_barrier_t barrier;
    pj_companion_sync_mutation_barrier_init(&barrier);
    uint64_t version = barrier.version;
    persist_calls = 0;
    persist_result = 1;

    assert(pj_companion_sync_prepare_inventory_mutation_transactional(
        &barrier, &state, 0, "pj-idle", 1000U,
        persist_test_state, NULL) == PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(barrier.version == version + 1U);
    assert(state.requested_generation == 0U);
    assert(state.active_generation == 0U);
    assert(persist_calls == 0);

    assert(pj_companion_sync_state_request(&state, "pj-idle", 2000U));
    version = barrier.version;
    assert(pj_companion_sync_prepare_inventory_mutation_transactional(
        &barrier, &state, 0, "pj-idle", 3000U,
        persist_test_state, NULL) == PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(barrier.version == version + 1U);
    assert(state.requested_generation == 1U);
    assert(state.active_generation == 0U);
    assert(state.requested_ms == 2000U);
    assert(persist_calls == 0);
}

static void test_restored_active_queues_exactly_one_durable_successor(void)
{
    pj_companion_sync_state_t state;
    pj_companion_sync_state_init(&state);
    assert(pj_companion_sync_state_request(
        &state, "pj-restore", 1000U));
    assert(pj_companion_sync_state_claim(
        &state, 1U, "pj-restore-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB) ==
        PJ_COMPANION_SYNC_CLAIM_STARTED);
    assert(pj_companion_sync_state_progress(
        &state, 1U, "pj-restore-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "running", 2, 1, 1, 0, "",
        "pj-restore") == PJ_COMPANION_SYNC_APPLY_CHANGED);

    pj_companion_sync_state_t active_before = state;
    persist_calls = 0;
    persist_result = 1;
    assert(pj_companion_sync_restore_active_successor_transactional(
        &state, 2000U, persist_test_state, NULL) ==
        PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(persist_calls == 1);
    assert(state.requested_generation == 2U);
    assert(state.requested_ms == 2000U);
    assert(state.active_generation == active_before.active_generation);
    assert(state.active_requested_ms == active_before.active_requested_ms);
    assert(state.phase == active_before.phase);
    assert(state.transport == active_before.transport);
    assert(state.total == active_before.total);
    assert(state.pending == active_before.pending);
    assert(state.transferred == active_before.transferred);
    assert(strcmp(state.operation_id, active_before.operation_id) == 0);

    restore_after_reboot(&state);
    assert(pj_companion_sync_restore_active_successor_transactional(
        &state, 3000U, persist_test_state, NULL) ==
        PJ_COMPANION_SYNC_APPLY_REPLAY);
    assert(persist_calls == 1);
    assert(state.requested_generation == 2U);
    assert(state.requested_ms == 2000U);

    assert(pj_companion_sync_state_claim(
        &state, 1U, "pj-restore-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB) ==
        PJ_COMPANION_SYNC_CLAIM_ATTACHED);
    assert(pj_companion_sync_state_progress_transactional(
        &state, 1U, "pj-restore-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "succeeded", 2, 0, 2, 0, "",
        "pj-restore", persist_test_state, NULL) ==
        PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(state.acknowledged_generation == 1U);
    assert(state.requested_generation == 2U);
    assert(state.phase == PJ_COMPANION_SYNC_PENDING);
    assert(pj_companion_sync_state_progress_transactional(
        &state, 1U, "pj-restore-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "succeeded", 2, 0, 2, 0, "",
        "pj-restore", persist_test_state, NULL) ==
        PJ_COMPANION_SYNC_APPLY_REPLAY);
}

static void test_restored_active_successor_failures_are_atomic(void)
{
    pj_companion_sync_state_t state;
    pj_companion_sync_state_init(&state);
    assert(pj_companion_sync_state_request(
        &state, "pj-restore-fail", 1000U));
    assert(pj_companion_sync_state_claim(
        &state, 1U, "pj-restore-fail-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB) ==
        PJ_COMPANION_SYNC_CLAIM_STARTED);
    pj_companion_sync_state_t before = state;

    persist_calls = 0;
    persist_result = 0;
    assert(pj_companion_sync_restore_active_successor_transactional(
        &state, 2000U, persist_test_state, NULL) ==
        PJ_COMPANION_SYNC_APPLY_STORE_FAILED);
    assert(persist_calls == 1);
    assert(memcmp(&state, &before, sizeof(state)) == 0);
    assert(pj_companion_sync_restore_active_successor_transactional(
        &state, 2000U, NULL, NULL) ==
        PJ_COMPANION_SYNC_APPLY_STORE_FAILED);
    assert(memcmp(&state, &before, sizeof(state)) == 0);

    state.requested_generation = UINT32_MAX;
    state.active_generation = UINT32_MAX;
    state.acknowledged_generation = UINT32_MAX - 1U;
    before = state;
    assert(pj_companion_sync_restore_active_successor_transactional(
        &state, 2000U, persist_test_state, NULL) ==
        PJ_COMPANION_SYNC_APPLY_REJECTED);
    assert(memcmp(&state, &before, sizeof(state)) == 0);

    state.requested_generation = 1U;
    state.active_generation = 2U;
    state.acknowledged_generation = 0U;
    before = state;
    assert(pj_companion_sync_restore_active_successor_transactional(
        &state, 2000U, persist_test_state, NULL) ==
        PJ_COMPANION_SYNC_APPLY_REJECTED);
    assert(memcmp(&state, &before, sizeof(state)) == 0);

    pj_companion_sync_state_init(&state);
    assert(pj_companion_sync_restore_active_successor_transactional(
        &state, 2000U, persist_test_state, NULL) ==
        PJ_COMPANION_SYNC_APPLY_REPLAY);
}

static void test_restored_active_claim_rebinds_barrier(void)
{
    pj_companion_sync_state_t state;
    pj_companion_sync_state_init(&state);
    assert(pj_companion_sync_state_request(
        &state, "pj-reboot", 1000U));
    assert(pj_companion_sync_state_claim(
        &state, 1U, "pj-reboot-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB) == PJ_COMPANION_SYNC_CLAIM_STARTED);
    assert(pj_companion_sync_state_queue_inventory_mutation(
        &state, "pj-reboot", 2000U) == 1);
    assert(pj_companion_sync_state_attempt_failed(
        &state, 1U, PJ_COMPANION_SYNC_TRANSPORT_USB,
        PJ_COMPANION_SYNC_OFFLINE, "reboot"));
    restore_after_reboot(&state);
    assert(state.active_generation == 1U);
    assert(state.requested_generation == 2U);
    assert(pj_companion_sync_state_claim(
        &state, 1U, "pj-reboot-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB) == PJ_COMPANION_SYNC_CLAIM_ATTACHED);

    pj_companion_sync_mutation_barrier_t barrier;
    pj_companion_sync_mutation_barrier_init(&barrier);
    assert(pj_companion_sync_mutation_barrier_bind(&barrier, 1U));
    assert(pj_companion_sync_mutation_barrier_terminal_current(
        &barrier, 1U));
    assert(pj_companion_sync_state_progress(
        &state, 1U, "pj-reboot-00000001",
        PJ_COMPANION_SYNC_TRANSPORT_USB, "succeeded", 1, 0, 1, 0, "",
        "pj-reboot") == PJ_COMPANION_SYNC_APPLY_CHANGED);
    assert(state.phase == PJ_COMPANION_SYNC_PENDING);
    assert(state.requested_generation == 2U);
}

int main(void)
{
    test_upload_swap_terminal_barrier();
    test_terminal_then_swap_requeues_once();
    test_mutation_prepare_persist_failure_blocks_publication();
    test_idle_inventory_mutations_do_not_autoqueue();
    test_restored_active_queues_exactly_one_durable_successor();
    test_restored_active_successor_failures_are_atomic();
    test_restored_active_claim_rebinds_barrier();

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
