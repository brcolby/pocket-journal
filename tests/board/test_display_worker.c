#include "pj_display_worker.h"
#include "pj_touch_candidate.h"

#include <assert.h>
#include <stdio.h>

static pj_ui_dirty_region_t partial_region(int x, int y, int width, int height)
{
    return (pj_ui_dirty_region_t) {
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .partial = 1,
    };
}

static pj_ui_dirty_region_t full_region(void)
{
    return (pj_ui_dirty_region_t) {
        .x = 0,
        .y = 0,
        .width = PJ_DISPLAY_WIDTH,
        .height = PJ_DISPLAY_HEIGHT,
        .partial = 0,
    };
}

static uint32_t submit_scene(pj_display_worker_model_t *model,
                             const pj_ui_dirty_region_t *dirty,
                             uint32_t scene_epoch,
                             int *slot)
{
    uint32_t generation = 0;
    assert(pj_display_worker_model_begin_submit(model, slot));
    assert(model->slots[*slot].state == PJ_DISPLAY_WORKER_SLOT_WRITING);
    assert(pj_display_worker_model_commit_submit(
        model, *slot, dirty, scene_epoch, &generation));
    assert(generation != 0);
    return generation;
}

static uint32_t submit(pj_display_worker_model_t *model,
                       const pj_ui_dirty_region_t *dirty,
                       int *slot)
{
    return submit_scene(model, dirty, 1, slot);
}

static void test_uncommitted_slot_cannot_be_displayed(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);

    int slot = -1;
    assert(!pj_display_worker_model_take(
        &model, &slot, &(pj_ui_dirty_region_t){0}, NULL, NULL));
    assert(pj_display_worker_model_begin_submit(&model, &slot));
    assert(!pj_display_worker_model_take(
        &model, &slot, &(pj_ui_dirty_region_t){0}, NULL, NULL));
}

static void test_invalid_submission_releases_reserved_slot(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);

    int slot;
    assert(pj_display_worker_model_begin_submit(&model, &slot));
    assert(!pj_display_worker_model_commit_submit(
        &model, slot, &(pj_ui_dirty_region_t){0}, 1, NULL));
    assert(pj_display_worker_model_is_idle(&model));
}

static void test_latest_frame_coalesces_partial_regions(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t top_left = partial_region(10, 20, 30, 40);
    pj_ui_dirty_region_t bottom_right = partial_region(100, 120, 20, 30);

    int first_slot;
    submit(&model, &top_left, &first_slot);
    int latest_slot;
    submit(&model, &bottom_right, &latest_slot);
    assert(latest_slot == first_slot);

    pj_ui_dirty_region_t dirty;
    uint32_t generation = 0;
    assert(pj_display_worker_model_take(
        &model, &latest_slot, &dirty, &generation, NULL));
    assert(generation == 2);
    assert(dirty.partial);
    assert(dirty.x == 10 && dirty.y == 20);
    assert(dirty.width == 110 && dirty.height == 130);
}

static void test_full_refresh_is_sticky_while_pending(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t full = full_region();
    pj_ui_dirty_region_t partial = partial_region(50, 50, 10, 10);

    int slot;
    submit(&model, &full, &slot);
    submit(&model, &partial, &slot);

    pj_ui_dirty_region_t dirty;
    assert(pj_display_worker_model_take(
        &model, &slot, &dirty, NULL, NULL));
    assert(!dirty.partial);
    assert(dirty.width == PJ_DISPLAY_WIDTH);
    assert(dirty.height == PJ_DISPLAY_HEIGHT);
}

static void test_inflight_and_pending_frames_use_distinct_slots(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t first = partial_region(0, 0, 20, 20);
    pj_ui_dirty_region_t second = partial_region(40, 40, 20, 20);

    int active;
    submit(&model, &first, &active);
    pj_ui_dirty_region_t dirty;
    assert(pj_display_worker_model_take(
        &model, &active, &dirty, NULL, NULL));

    int pending;
    submit(&model, &second, &pending);
    assert(pending != active);
    assert(!pj_display_worker_model_take(
        &model, &pending, &dirty, NULL, NULL));

    pj_display_worker_model_complete(&model, active, 1);
    assert(pj_display_worker_model_take(
        &model, &pending, &dirty, NULL, NULL));
    assert(dirty.partial && dirty.x == 40 && dirty.y == 40);
}

static void test_failed_commit_promotes_latest_pending_frame_to_full(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t first = partial_region(0, 0, 20, 20);
    pj_ui_dirty_region_t latest = partial_region(80, 80, 20, 20);

    int active;
    submit(&model, &first, &active);
    pj_ui_dirty_region_t dirty;
    assert(pj_display_worker_model_take(
        &model, &active, &dirty, NULL, NULL));
    int pending;
    submit(&model, &latest, &pending);

    pj_display_worker_model_complete(&model, active, 0);
    assert(pj_display_worker_model_take(
        &model, &pending, &dirty, NULL, NULL));
    assert(!dirty.partial);
}

static void test_failure_during_copy_forces_committed_frame_full(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t first = partial_region(0, 0, 20, 20);
    pj_ui_dirty_region_t latest = partial_region(80, 80, 20, 20);

    int active;
    submit(&model, &first, &active);
    pj_ui_dirty_region_t dirty;
    assert(pj_display_worker_model_take(
        &model, &active, &dirty, NULL, NULL));

    int writing;
    assert(pj_display_worker_model_begin_submit(&model, &writing));
    pj_display_worker_model_complete(&model, active, 0);
    assert(pj_display_worker_model_commit_submit(
        &model, writing, &latest, 1, NULL));
    assert(pj_display_worker_model_take(
        &model, &writing, &dirty, NULL, NULL));
    assert(!dirty.partial);
}

static void test_failed_frame_retries_full_when_no_newer_frame_exists(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t partial = partial_region(0, 0, 20, 20);

    int slot;
    submit(&model, &partial, &slot);
    pj_ui_dirty_region_t dirty;
    assert(pj_display_worker_model_take(
        &model, &slot, &dirty, NULL, NULL));
    pj_display_worker_model_complete(&model, slot, 0);
    assert(pj_display_worker_model_take(
        &model, &slot, &dirty, NULL, NULL));
    assert(!dirty.partial);
    assert(pj_display_worker_model_status(&model).ordering_errors == 0);
}

static void test_shutdown_discards_pending_and_rejects_submissions(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t dirty = partial_region(0, 0, 20, 20);
    int slot;
    submit(&model, &dirty, &slot);

    pj_display_worker_model_shutdown(&model);
    assert(pj_display_worker_model_is_idle(&model));
    assert(!pj_display_worker_model_begin_submit(&model, &slot));
    assert(!pj_display_worker_model_take(
        &model, &slot, &dirty, NULL, NULL));
}

static void test_scene_is_not_presented_until_physical_commit(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t full = full_region();
    int slot;

    uint32_t generation = submit_scene(&model, &full, 41, &slot);
    assert(generation == 1);
    assert(!pj_display_worker_model_scene_presented(&model, 41));

    pj_ui_dirty_region_t dirty;
    uint32_t taken_generation = 0;
    uint32_t scene_epoch = 0;
    assert(pj_display_worker_model_take(
        &model, &slot, &dirty, &taken_generation, &scene_epoch));
    assert(taken_generation == generation && scene_epoch == 41);
    assert(!pj_display_worker_model_scene_presented(&model, 41));

    pj_display_worker_model_complete(&model, slot, 1);
    assert(pj_display_worker_model_scene_presented(&model, 41));
    pj_display_worker_status_t status =
        pj_display_worker_model_status(&model);
    assert(status.accepted_generation == 1);
    assert(status.started_generation == 1);
    assert(status.committed_generation == 1);
    assert(status.committed_scene_epoch == 41);
    assert(status.ordering_errors == 0);
}

static void test_new_scene_supersedes_pending_hit_map_as_full(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t first = partial_region(0, 0, 40, 40);
    pj_ui_dirty_region_t second = partial_region(160, 160, 40, 40);
    int slot;

    (void)submit_scene(&model, &first, 7, &slot);
    (void)submit_scene(&model, &second, 8, &slot);
    pj_ui_dirty_region_t dirty;
    uint32_t scene_epoch = 0;
    assert(pj_display_worker_model_take(
        &model, &slot, &dirty, NULL, &scene_epoch));
    assert(scene_epoch == 8);
    assert(!dirty.partial);
    assert(dirty.width == PJ_DISPLAY_WIDTH &&
           dirty.height == PJ_DISPLAY_HEIGHT);
    assert(pj_display_worker_model_status(&model).superseded_frames == 1);
}

static void test_slow_display_keeps_generations_and_final_scene_ordered(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t full = full_region();
    int active;
    uint32_t first = submit_scene(&model, &full, 10, &active);
    pj_ui_dirty_region_t dirty;
    uint32_t generation = 0;
    uint32_t scene_epoch = 0;
    assert(pj_display_worker_model_take(
        &model, &active, &dirty, &generation, &scene_epoch));
    assert(generation == first && scene_epoch == 10);

    int pending;
    uint32_t second = submit_scene(&model, &full, 11, &pending);
    uint32_t third = submit_scene(&model, &full, 12, &pending);
    assert(first < second && second < third);
    assert(!pj_display_worker_model_scene_presented(&model, 12));

    pj_display_worker_model_complete(&model, active, 1);
    assert(pj_display_worker_model_scene_presented(&model, 10));
    assert(pj_display_worker_model_take(
        &model, &pending, &dirty, &generation, &scene_epoch));
    assert(generation == third && scene_epoch == 12);
    pj_display_worker_model_complete(&model, pending, 1);

    pj_display_worker_status_t status =
        pj_display_worker_model_status(&model);
    assert(status.committed_generation == third);
    assert(status.committed_scene_epoch == 12);
    assert(status.superseded_frames == 1);
    assert(status.ordering_errors == 0);
}

static void commit_scene_at(pj_display_worker_model_t *model,
                            uint32_t scene_epoch, uint64_t committed_at_ms)
{
    pj_ui_dirty_region_t dirty = full_region();
    int slot;
    (void)submit_scene(model, &dirty, scene_epoch, &slot);
    assert(pj_display_worker_model_take(
        model, &slot, &dirty, NULL, NULL));
    pj_display_worker_model_complete_at(
        model, slot, 1, committed_at_ms);
}

static void test_input_requires_capture_after_first_scene_commit(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_display_worker_status_t status =
        pj_display_worker_model_status(&model);
    assert(!pj_display_worker_status_accepts_input(&status, 1U, 1U));

    commit_scene_at(&model, 41U, 100U);
    status = pj_display_worker_model_status(&model);
    assert(status.committed_scene_started_ms == 100U);
    assert(!pj_display_worker_status_accepts_input(&status, 40U, 101U));
    assert(!pj_display_worker_status_accepts_input(&status, 41U, 99U));
    /* Millisecond equality cannot prove the contact followed the commit. */
    assert(!pj_display_worker_status_accepts_input(&status, 41U, 100U));
    assert(pj_display_worker_status_accepts_input(&status, 41U, 101U));

    commit_scene_at(&model, 41U, 500U);
    status = pj_display_worker_model_status(&model);
    assert(status.committed_scene_started_ms == 100U);
    assert(pj_display_worker_status_accepts_input(&status, 41U, 200U));

    commit_scene_at(&model, 42U, 600U);
    status = pj_display_worker_model_status(&model);
    assert(status.committed_scene_started_ms == 600U);
    assert(!pj_display_worker_status_accepts_input(&status, 42U, 599U));
    assert(pj_display_worker_status_accepts_input(&status, 42U, 601U));

    pj_ui_dirty_region_t dirty = full_region();
    int slot;
    (void)submit_scene(&model, &dirty, 43U, &slot);
    assert(pj_display_worker_model_take(
        &model, &slot, &dirty, NULL, NULL));
    pj_display_worker_model_complete_at(&model, slot, 0, 700U);
    status = pj_display_worker_model_status(&model);
    assert(status.committed_scene_epoch == 42U);
    assert(status.committed_scene_started_ms == 600U);

    assert(pj_display_worker_model_take(
        &model, &slot, &dirty, NULL, NULL));
    pj_display_worker_model_complete_at(&model, slot, 1, 800U);
    status = pj_display_worker_model_status(&model);
    assert(status.committed_scene_epoch == 43U);
    assert(status.committed_scene_started_ms == 800U);
}

static void test_input_capture_remains_ordered_after_32_bit_uptime(void)
{
    pj_display_worker_status_t status = {
        .committed_scene_epoch = 7U,
        .committed_scene_started_ms = 100U,
    };
    assert(pj_display_worker_status_accepts_input(
        &status, 7U, (UINT64_C(1) << 40)));
}

static void test_touch_contact_before_commit_is_rejected_after_stabilizing(void)
{
    pj_touch_candidate_t candidate = {0};
    uint64_t captured_at_ms = 0;
    assert(!pj_touch_candidate_update(
        &candidate, 50U, 50U, 100U, 8U, 2U, &captured_at_ms));

    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    commit_scene_at(&model, 8U, 101U);

    assert(pj_touch_candidate_update(
        &candidate, 51U, 50U, 102U, 8U, 2U, &captured_at_ms));
    assert(captured_at_ms == 100U);
    pj_display_worker_status_t status =
        pj_display_worker_model_status(&model);
    assert(!pj_display_worker_status_accepts_input(
        &status, 8U, captured_at_ms));

    pj_touch_candidate_reset(&candidate);
    assert(!pj_touch_candidate_update(
        &candidate, 50U, 50U, 103U, 8U, 2U, &captured_at_ms));
    assert(pj_touch_candidate_update(
        &candidate, 51U, 50U, 104U, 8U, 2U, &captured_at_ms));
    assert(pj_display_worker_status_accepts_input(
        &status, 8U, captured_at_ms));

    pj_touch_candidate_reset(&candidate);
    assert(!pj_touch_candidate_update(
        &candidate, 10U, 10U, 105U, 8U, 2U, &captured_at_ms));
    assert(!pj_touch_candidate_update(
        &candidate, 30U, 30U, 106U, 8U, 2U, &captured_at_ms));
    assert(pj_touch_candidate_update(
        &candidate, 31U, 30U, 107U, 8U, 2U, &captured_at_ms));
    assert(captured_at_ms == 106U);
}

static void test_partial_rate_limit_is_global_and_size_independent(void)
{
    pj_display_worker_rate_limiter_t limiter;
    pj_display_worker_rate_init(&limiter, 1000);
    pj_ui_dirty_region_t top_left = partial_region(0, 0, 20, 20);
    pj_ui_dirty_region_t overlapping = partial_region(10, 10, 20, 20);
    pj_ui_dirty_region_t adjacent = partial_region(20, 0, 20, 20);
    pj_ui_dirty_region_t other_tile = partial_region(160, 160, 20, 20);
    pj_ui_dirty_region_t full = full_region();

    assert(pj_display_worker_rate_earliest_start(
        &limiter, &top_left, 100) == 100);
    assert(pj_display_worker_rate_record_start(
        &limiter, &top_left, 100));
    assert(pj_display_worker_rate_earliest_start(
        &limiter, &overlapping, 700) == 1100);
    assert(pj_display_worker_rate_earliest_start(
        &limiter, &overlapping, 1100) == 1100);
    assert(pj_display_worker_rate_earliest_start(
        &limiter, &adjacent, 700) == 1100);
    assert(pj_display_worker_rate_earliest_start(
        &limiter, &other_tile, 700) == 1100);
    assert(pj_display_worker_rate_earliest_start(
        &limiter, &full, 700) == 700);
    assert(!pj_display_worker_rate_record_start(
        &limiter, &overlapping, 700));
    assert(pj_display_worker_rate_record_start(
        &limiter, &overlapping, 1100));
}

static void test_deferred_metrics_are_explicit(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_display_worker_model_note_rate_deferred(&model);
    pj_display_worker_model_note_input_deferred(&model);
    pj_display_worker_status_t status =
        pj_display_worker_model_status(&model);
    assert(status.rate_deferred_frames == 1);
    assert(status.input_deferred_events == 1);
}

static void test_generation_order_survives_wrap(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    model.next_generation = UINT32_MAX;
    pj_ui_dirty_region_t dirty = partial_region(0, 0, 8, 8);
    int slot;

    uint32_t before_wrap = submit_scene(&model, &dirty, 1, &slot);
    assert(before_wrap == UINT32_MAX);
    assert(pj_display_worker_model_take(
        &model, &slot, &dirty, NULL, NULL));
    pj_display_worker_model_complete(&model, slot, 1);

    uint32_t after_wrap = submit_scene(&model, &dirty, 1, &slot);
    assert(after_wrap == 1);
    assert(pj_display_worker_model_take(
        &model, &slot, &dirty, NULL, NULL));
    pj_display_worker_model_complete(&model, slot, 1);
    assert(model.committed_generation == 1);
    assert(model.ordering_errors == 0);
}

int main(void)
{
    test_uncommitted_slot_cannot_be_displayed();
    test_invalid_submission_releases_reserved_slot();
    test_latest_frame_coalesces_partial_regions();
    test_full_refresh_is_sticky_while_pending();
    test_inflight_and_pending_frames_use_distinct_slots();
    test_failed_commit_promotes_latest_pending_frame_to_full();
    test_failure_during_copy_forces_committed_frame_full();
    test_failed_frame_retries_full_when_no_newer_frame_exists();
    test_shutdown_discards_pending_and_rejects_submissions();
    test_scene_is_not_presented_until_physical_commit();
    test_new_scene_supersedes_pending_hit_map_as_full();
    test_slow_display_keeps_generations_and_final_scene_ordered();
    test_input_requires_capture_after_first_scene_commit();
    test_input_capture_remains_ordered_after_32_bit_uptime();
    test_touch_contact_before_commit_is_rejected_after_stabilizing();
    test_partial_rate_limit_is_global_and_size_independent();
    test_deferred_metrics_are_explicit();
    test_generation_order_survives_wrap();
    puts("display worker tests passed");
    return 0;
}
