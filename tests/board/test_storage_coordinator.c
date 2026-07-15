#include "pj_storage_coordinator.h"

#include <assert.h>

static void test_shared_and_maintenance_exclusion(void)
{
    pj_storage_coordinator_t coordinator;
    pj_storage_coordinator_init(&coordinator);
    assert(pj_storage_idle(&coordinator));
    assert(pj_storage_shared_try_acquire(&coordinator));
    assert(!pj_storage_idle(&coordinator));
    assert(!pj_storage_recovery_try_begin(&coordinator));
    assert(pj_storage_wipe_request(&coordinator, 1, 0, NULL, NULL) == PJ_WIPE_START_STORAGE_BUSY);
    pj_storage_shared_release(&coordinator);
    assert(pj_storage_idle(&coordinator));
    assert(pj_storage_sleep_try_begin(&coordinator));
    assert(!pj_storage_shared_try_acquire(&coordinator));
    assert(!pj_storage_recovery_try_begin(&coordinator));
    pj_storage_sleep_finish(&coordinator);
    assert(pj_storage_idle(&coordinator));
    assert(pj_storage_recovery_try_begin(&coordinator));
    assert(!pj_storage_shared_try_acquire(&coordinator));
    pj_storage_recovery_finish(&coordinator);
    assert(pj_storage_ota_try_begin(&coordinator));
    assert(!pj_storage_shared_try_acquire(&coordinator));
    assert(!pj_storage_sleep_try_begin(&coordinator));
    assert(pj_storage_wipe_request(&coordinator, 1, 0, NULL, NULL) ==
           PJ_WIPE_START_STORAGE_BUSY);
    pj_storage_ota_finish(&coordinator);
    assert(pj_storage_shared_try_acquire(&coordinator));
    pj_storage_shared_release(&coordinator);
}

static void test_wipe_duplicate_and_repeated_starts(void)
{
    pj_storage_coordinator_t coordinator;
    pj_wipe_status_t first;
    pj_wipe_status_t duplicate;
    pj_storage_coordinator_init(&coordinator);

    assert(pj_storage_wipe_request(&coordinator, 1, 0, "host-a", &first) == PJ_WIPE_START_STARTED);
    assert(first.id != 0U);
    assert(first.state == PJ_WIPE_STATE_QUEUED);
    assert(pj_storage_wipe_request(&coordinator, 1, 0, "host-a", &duplicate) == PJ_WIPE_START_ATTACHED);
    assert(duplicate.id == first.id);
    assert(!pj_storage_shared_try_acquire(&coordinator));

    pj_storage_wipe_mark_running(&coordinator);
    assert(pj_storage_wipe_status(&coordinator).state == PJ_WIPE_STATE_RUNNING);
    assert(pj_storage_wipe_request(&coordinator, 1, 0, "host-a", &duplicate) == PJ_WIPE_START_ATTACHED);
    assert(duplicate.id == first.id);
    pj_storage_wipe_finish(&coordinator, 2U, 3U, 4U, PJ_WIPE_CODE_NONE, 0);
    pj_wipe_status_t complete = pj_storage_wipe_status(&coordinator);
    assert(complete.state == PJ_WIPE_STATE_SUCCEEDED);
    assert(complete.audio_deleted == 2U);
    assert(pj_storage_wipe_request(&coordinator, 1, 0, "host-a", &duplicate) ==
           PJ_WIPE_START_ATTACHED);
    assert(duplicate.id == first.id);
    assert(duplicate.state == PJ_WIPE_STATE_SUCCEEDED);
    assert(pj_storage_shared_try_acquire(&coordinator));
    pj_storage_shared_release(&coordinator);

    pj_wipe_status_t repeated;
    assert(pj_storage_wipe_request(&coordinator, 1, 0, NULL, &repeated) == PJ_WIPE_START_STARTED);
    assert(repeated.id != first.id);
    pj_storage_wipe_mark_running(&coordinator);
    pj_storage_wipe_finish(&coordinator, 5U, 0U, 0U, PJ_WIPE_CODE_NONE, 0);

    pj_wipe_status_t history[PJ_STORAGE_WIPE_HISTORY_CAPACITY];
    size_t history_count = pj_storage_wipe_history(
        &coordinator, history, PJ_STORAGE_WIPE_HISTORY_CAPACITY);
    assert(history_count == 2U);
    assert(history[0].id == repeated.id);
    assert(history[1].id == first.id);

    assert(pj_storage_wipe_request(&coordinator, 1, 0, "host-a", &duplicate) ==
           PJ_WIPE_START_ATTACHED);
    assert(duplicate.id == first.id);
    assert(duplicate.state == PJ_WIPE_STATE_SUCCEEDED);

    assert(pj_storage_wipe_request(&coordinator, 1, 0, "host-b", &repeated) ==
           PJ_WIPE_START_STARTED);
}

static void test_wipe_rejections_and_failure(void)
{
    pj_storage_coordinator_t coordinator;
    pj_storage_coordinator_init(&coordinator);
    assert(pj_storage_wipe_request(&coordinator, 0, 0, NULL, NULL) ==
           PJ_WIPE_START_STORAGE_UNAVAILABLE);
    assert(pj_storage_wipe_request(&coordinator, 1, 1, NULL, NULL) ==
           PJ_WIPE_START_AUDIO_ACTIVE);

    assert(pj_storage_wipe_request(&coordinator, 1, 0, NULL, NULL) == PJ_WIPE_START_STARTED);
    pj_storage_wipe_finish(&coordinator, 1U, 0U, 0U,
                           PJ_WIPE_CODE_INCOMPLETE, 1);
    pj_wipe_status_t failed = pj_storage_wipe_status(&coordinator);
    assert(failed.state == PJ_WIPE_STATE_FAILED);
    assert(failed.code == PJ_WIPE_CODE_INCOMPLETE);
    assert(failed.retryable);
    assert(pj_storage_shared_try_acquire(&coordinator));
    pj_storage_shared_release(&coordinator);

    pj_wipe_status_t retry;
    assert(pj_storage_wipe_request(&coordinator, 1, 0, NULL, &retry) == PJ_WIPE_START_STARTED);
    assert(retry.id != failed.id);
    pj_storage_wipe_finish(&coordinator, 0U, 0U, 0U, PJ_WIPE_CODE_NONE, 0);
}

int main(void)
{
    test_shared_and_maintenance_exclusion();
    test_wipe_duplicate_and_repeated_starts();
    test_wipe_rejections_and_failure();
    return 0;
}
