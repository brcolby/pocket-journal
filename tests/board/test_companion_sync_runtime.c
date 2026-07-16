#include <assert.h>
#include <string.h>

#include "pj_board.h"

static void test_target_generation_binding(void)
{
    pj_companion_sync_state_t snapshot = {0};
    assert(!pj_board_companion_sync_snapshot_matches_target(NULL, 1U));
    assert(!pj_board_companion_sync_snapshot_matches_target(&snapshot, 0U));

    snapshot.requested_generation = 2U;
    snapshot.acknowledged_generation = 0U;
    snapshot.active_generation = 1U;
    snapshot.phase = PJ_COMPANION_SYNC_RUNNING;
    assert(!pj_board_companion_sync_snapshot_matches_target(&snapshot, 2U));
    assert(!pj_board_companion_sync_snapshot_target_succeeded(&snapshot, 2U));

    snapshot.acknowledged_generation = 1U;
    snapshot.active_generation = 0U;
    snapshot.phase = PJ_COMPANION_SYNC_PENDING;
    assert(pj_board_companion_sync_snapshot_matches_target(&snapshot, 2U));
    assert(!pj_board_companion_sync_snapshot_target_succeeded(&snapshot, 2U));

    snapshot.active_generation = 2U;
    snapshot.phase = PJ_COMPANION_SYNC_RUNNING;
    assert(pj_board_companion_sync_snapshot_matches_target(&snapshot, 2U));
    assert(!pj_board_companion_sync_snapshot_target_succeeded(&snapshot, 2U));

    snapshot.active_generation = 0U;
    snapshot.acknowledged_generation = 2U;
    snapshot.phase = PJ_COMPANION_SYNC_SUCCEEDED;
    snapshot.acknowledged_phase = PJ_COMPANION_SYNC_SUCCEEDED;
    assert(pj_board_companion_sync_snapshot_matches_target(&snapshot, 2U));
    assert(pj_board_companion_sync_snapshot_target_succeeded(&snapshot, 2U));

    snapshot.requested_generation = 3U;
    assert(!pj_board_companion_sync_snapshot_matches_target(&snapshot, 2U));
    assert(!pj_board_companion_sync_snapshot_target_succeeded(&snapshot, 2U));
}

static void test_host_stubs_fail_closed_and_clear_outputs(void)
{
    pj_companion_sync_state_t snapshot;
    memset(&snapshot, 0xa5, sizeof(snapshot));
    assert(!pj_board_companion_sync_start_snapshot(&snapshot));
    pj_companion_sync_state_t zero = {0};
    assert(memcmp(&snapshot, &zero, sizeof(snapshot)) == 0);

    memset(&snapshot, 0xa5, sizeof(snapshot));
    assert(!pj_board_consume_companion_sync_update_snapshot(&snapshot));
    assert(memcmp(&snapshot, &zero, sizeof(snapshot)) == 0);
}

int main(void)
{
    test_target_generation_binding();
    test_host_stubs_fail_closed_and_clear_outputs();
    return 0;
}
