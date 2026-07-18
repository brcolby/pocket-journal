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

static pj_display_worker_request_t request_for(
    const pj_ui_dirty_region_t *dirty, uint32_t layout_epoch,
    uint32_t interaction_generation)
{
    pj_display_worker_request_t request;
    pj_display_worker_request_init(&request, dirty);
    request.layout_epoch = layout_epoch;
    request.interaction_generation = interaction_generation;
    request.visual_revision = interaction_generation;
    return request;
}

static uint32_t submit_request(pj_display_worker_model_t *model,
                               const pj_display_worker_request_t *request,
                               int *slot)
{
    uint32_t generation = 0;
    assert(pj_display_worker_model_begin_submit_request(
        model, request, slot));
    assert(model->slots[*slot].state == PJ_DISPLAY_WORKER_SLOT_WRITING);
    assert(pj_display_worker_model_commit_submit_request(
        model, *slot, request, &generation));
    assert(generation != 0);
    return generation;
}

static uint32_t submit_revision(pj_display_worker_model_t *model,
                                const pj_ui_dirty_region_t *dirty,
                                uint32_t layout_epoch,
                                uint32_t interaction_generation,
                                int *slot)
{
    pj_display_worker_request_t request = request_for(
        dirty, layout_epoch, interaction_generation);
    return submit_request(model, &request, slot);
}

static uint32_t submit(pj_display_worker_model_t *model,
                       const pj_ui_dirty_region_t *dirty,
                       int *slot)
{
    return submit_revision(model, dirty, 1, 1, slot);
}

static int take_request(pj_display_worker_model_t *model, int *slot,
                        pj_display_worker_request_t *request,
                        uint32_t *generation)
{
    return pj_display_worker_model_take_request_at(
        model, slot, request, generation, 0);
}

static int take_frame(pj_display_worker_model_t *model, int *slot,
                      pj_ui_dirty_region_t *dirty, uint32_t *generation,
                      uint32_t *interaction_generation)
{
    pj_display_worker_request_t request;
    int taken = take_request(model, slot, &request, generation);
    if (!taken) {
        return 0;
    }
    if (dirty != NULL) {
        *dirty = request.dirty;
    }
    if (interaction_generation != NULL) {
        *interaction_generation = request.interaction_generation;
    }
    return 1;
}

static void test_uncommitted_slot_cannot_be_displayed(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t full = full_region();
    pj_display_worker_request_t request = request_for(&full, 1, 1);

    int slot = -1;
    assert(!take_frame(
        &model, &slot, &(pj_ui_dirty_region_t){0}, NULL, NULL));
    assert(pj_display_worker_model_begin_submit_request(
        &model, &request, &slot));
    assert(!take_frame(
        &model, &slot, &(pj_ui_dirty_region_t){0}, NULL, NULL));
}

static void test_invalid_submission_releases_reserved_slot(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t full = full_region();
    pj_display_worker_request_t valid = request_for(&full, 1, 1);
    pj_display_worker_request_t invalid;
    pj_display_worker_request_init(
        &invalid, &(pj_ui_dirty_region_t){0});

    int slot;
    assert(pj_display_worker_model_begin_submit_request(
        &model, &valid, &slot));
    assert(!pj_display_worker_model_commit_submit_request(
        &model, slot, &invalid, NULL));
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
    assert(take_frame(
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
    assert(take_frame(
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
    assert(take_frame(
        &model, &active, &dirty, NULL, NULL));

    int pending;
    submit(&model, &second, &pending);
    assert(pending != active);
    assert(!take_frame(
        &model, &pending, &dirty, NULL, NULL));

    pj_display_worker_model_complete(&model, active, 1);
    assert(take_frame(
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
    assert(take_frame(
        &model, &active, &dirty, NULL, NULL));
    int pending;
    submit(&model, &latest, &pending);

    pj_display_worker_model_complete(&model, active, 0);
    assert(take_frame(
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
    assert(take_frame(
        &model, &active, &dirty, NULL, NULL));

    int writing;
    pj_display_worker_request_t request = request_for(&latest, 1, 1);
    assert(pj_display_worker_model_begin_submit_request(
        &model, &request, &writing));
    pj_display_worker_model_complete(&model, active, 0);
    assert(pj_display_worker_model_commit_submit_request(
        &model, writing, &request, NULL));
    assert(take_frame(
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
    assert(take_frame(
        &model, &slot, &dirty, NULL, NULL));
    pj_display_worker_model_complete(&model, slot, 0);
    assert(take_frame(
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
    pj_display_worker_request_t request = request_for(&dirty, 1, 1);
    assert(!pj_display_worker_model_begin_submit_request(
        &model, &request, &slot));
    assert(!take_frame(
        &model, &slot, &dirty, NULL, NULL));
}

static void test_interaction_is_not_presented_until_physical_commit(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t full = full_region();
    int slot;

    uint32_t generation = submit_revision(
        &model, &full, 41, 41, &slot);
    assert(generation == 1);
    assert(pj_display_worker_model_status(&model)
               .committed_interaction_generation != 41);

    pj_ui_dirty_region_t dirty;
    uint32_t taken_generation = 0;
    uint32_t interaction_generation = 0;
    assert(take_frame(
        &model, &slot, &dirty, &taken_generation,
        &interaction_generation));
    assert(taken_generation == generation &&
           interaction_generation == 41);
    assert(pj_display_worker_model_status(&model)
               .committed_interaction_generation != 41);

    pj_display_worker_model_complete(&model, slot, 1);
    pj_display_worker_status_t status =
        pj_display_worker_model_status(&model);
    assert(status.accepted_generation == 1);
    assert(status.started_generation == 1);
    assert(status.committed_generation == 1);
    assert(status.committed_layout_epoch == 41);
    assert(status.committed_interaction_generation == 41);
    assert(status.ordering_errors == 0);
}

static void test_new_layout_supersedes_pending_hit_map_as_full(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t first = partial_region(0, 0, 40, 40);
    pj_ui_dirty_region_t second = partial_region(160, 160, 40, 40);
    int slot;

    (void)submit_revision(&model, &first, 7, 7, &slot);
    (void)submit_revision(&model, &second, 8, 8, &slot);
    pj_ui_dirty_region_t dirty;
    uint32_t interaction_generation = 0;
    assert(take_frame(
        &model, &slot, &dirty, NULL, &interaction_generation));
    assert(interaction_generation == 8);
    assert(!dirty.partial);
    assert(dirty.width == PJ_DISPLAY_WIDTH &&
           dirty.height == PJ_DISPLAY_HEIGHT);
    assert(pj_display_worker_model_status(&model).superseded_frames == 1);
}

static void test_slow_display_keeps_generations_and_final_layout_ordered(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t full = full_region();
    int active;
    uint32_t first = submit_revision(
        &model, &full, 10, 10, &active);
    pj_ui_dirty_region_t dirty;
    uint32_t generation = 0;
    uint32_t interaction_generation = 0;
    assert(take_frame(
        &model, &active, &dirty, &generation,
        &interaction_generation));
    assert(generation == first && interaction_generation == 10);

    int pending;
    uint32_t second = submit_revision(
        &model, &full, 11, 11, &pending);
    uint32_t third = submit_revision(
        &model, &full, 12, 12, &pending);
    assert(first < second && second < third);
    assert(pj_display_worker_model_status(&model)
               .committed_interaction_generation != 12);

    pj_display_worker_model_complete(&model, active, 1);
    assert(take_frame(
        &model, &pending, &dirty, &generation,
        &interaction_generation));
    assert(generation == third && interaction_generation == 12);
    pj_display_worker_model_complete(&model, pending, 1);

    pj_display_worker_status_t status =
        pj_display_worker_model_status(&model);
    assert(status.committed_generation == third);
    assert(status.committed_layout_epoch == 12);
    assert(status.committed_interaction_generation == 12);
    assert(status.superseded_frames == 1);
    assert(status.ordering_errors == 0);
}

static void commit_interaction_at(pj_display_worker_model_t *model,
                                  uint32_t interaction_generation,
                                  uint64_t committed_at_ms)
{
    pj_ui_dirty_region_t dirty = full_region();
    int slot;
    (void)submit_revision(
        model, &dirty, interaction_generation, interaction_generation,
        &slot);
    assert(take_frame(
        model, &slot, &dirty, NULL, NULL));
    pj_display_worker_model_complete_at(
        model, slot, 1, committed_at_ms);
}

static void test_input_requires_capture_after_interaction_commit(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_display_worker_status_t status =
        pj_display_worker_model_status(&model);
    assert(!pj_display_worker_status_accepts_interaction(
        &status, 1U, 1U));

    commit_interaction_at(&model, 41U, 100U);
    status = pj_display_worker_model_status(&model);
    assert(status.committed_interaction_started_ms == 100U);
    assert(!pj_display_worker_status_accepts_interaction(
        &status, 40U, 101U));
    assert(!pj_display_worker_status_accepts_interaction(
        &status, 41U, 99U));
    /* Millisecond equality cannot prove the contact followed the commit. */
    assert(!pj_display_worker_status_accepts_interaction(
        &status, 41U, 100U));
    assert(pj_display_worker_status_accepts_interaction(
        &status, 41U, 101U));

    commit_interaction_at(&model, 41U, 500U);
    status = pj_display_worker_model_status(&model);
    assert(status.committed_interaction_started_ms == 100U);
    assert(pj_display_worker_status_accepts_interaction(
        &status, 41U, 200U));

    commit_interaction_at(&model, 42U, 600U);
    status = pj_display_worker_model_status(&model);
    assert(status.committed_interaction_started_ms == 600U);
    assert(!pj_display_worker_status_accepts_interaction(
        &status, 42U, 599U));
    assert(pj_display_worker_status_accepts_interaction(
        &status, 42U, 601U));

    pj_ui_dirty_region_t dirty = full_region();
    int slot;
    (void)submit_revision(&model, &dirty, 43U, 43U, &slot);
    assert(take_frame(
        &model, &slot, &dirty, NULL, NULL));
    pj_display_worker_model_complete_at(&model, slot, 0, 700U);
    status = pj_display_worker_model_status(&model);
    assert(status.committed_interaction_generation == 42U);
    assert(status.committed_interaction_started_ms == 600U);

    assert(take_frame(
        &model, &slot, &dirty, NULL, NULL));
    pj_display_worker_model_complete_at(&model, slot, 1, 800U);
    status = pj_display_worker_model_status(&model);
    assert(status.committed_interaction_generation == 43U);
    assert(status.committed_interaction_started_ms == 800U);
}

static void test_input_capture_remains_ordered_after_32_bit_uptime(void)
{
    pj_display_worker_status_t status = {
        .committed_interaction_generation = 7U,
        .committed_interaction_started_ms = 100U,
    };
    assert(pj_display_worker_status_accepts_interaction(
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
    commit_interaction_at(&model, 8U, 101U);

    assert(pj_touch_candidate_update(
        &candidate, 51U, 50U, 102U, 8U, 2U, &captured_at_ms));
    assert(captured_at_ms == 100U);
    pj_display_worker_status_t status =
        pj_display_worker_model_status(&model);
    assert(!pj_display_worker_status_accepts_interaction(
        &status, 8U, captured_at_ms));

    pj_touch_candidate_reset(&candidate);
    assert(!pj_touch_candidate_update(
        &candidate, 50U, 50U, 103U, 8U, 2U, &captured_at_ms));
    assert(pj_touch_candidate_update(
        &candidate, 51U, 50U, 104U, 8U, 2U, &captured_at_ms));
    assert(pj_display_worker_status_accepts_interaction(
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
    pj_display_worker_model_note_cadence_fault(&model, 1U);
    pj_display_worker_status_t status =
        pj_display_worker_model_status(&model);
    assert(status.rate_deferred_frames == 1);
    assert(status.input_deferred_events == 1);
    assert(status.cadence_misses == 1);
}

static void test_semantic_cadence_fault_is_latched_per_sequence(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    assert(pj_display_worker_model_cadence_start(&model, 1U, 1000U));

    for (int retry = 0; retry < 20; retry++) {
        pj_display_worker_model_note_cadence_fault(&model, 1U);
    }
    pj_display_worker_model_note_cadence_fault(&model, 0U);
    assert(pj_display_worker_model_status(&model).cadence_misses == 1U);

    for (int retry = 0; retry < 20; retry++) {
        pj_display_worker_model_note_cadence_fault(&model, 2U);
    }
    assert(pj_display_worker_model_status(&model).cadence_misses == 2U);

    pj_display_worker_model_cadence_end(&model);
    assert(pj_display_worker_model_cadence_start(&model, 1U, 3000U));
    pj_display_worker_model_note_cadence_fault(&model, 1U);
    assert(pj_display_worker_model_status(&model).cadence_misses == 3U);
}

static void test_generation_order_survives_wrap(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    model.next_generation = UINT32_MAX;
    pj_ui_dirty_region_t dirty = partial_region(0, 0, 8, 8);
    int slot;

    uint32_t before_wrap = submit_revision(
        &model, &dirty, 1, 1, &slot);
    assert(before_wrap == UINT32_MAX);
    assert(take_frame(
        &model, &slot, &dirty, NULL, NULL));
    pj_display_worker_model_complete(&model, slot, 1);

    uint32_t after_wrap = submit_revision(
        &model, &dirty, 1, 1, &slot);
    assert(after_wrap == 1);
    assert(take_frame(
        &model, &slot, &dirty, NULL, NULL));
    pj_display_worker_model_complete(&model, slot, 1);
    assert(model.committed_generation == 1);
    assert(model.ordering_errors == 0);
}

static void test_same_layout_semantics_remain_partial(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t first_dirty = partial_region(10, 10, 10, 10);
    pj_ui_dirty_region_t second_dirty = partial_region(100, 100, 10, 10);
    pj_display_worker_request_t first = request_for(
        &first_dirty, 7, 20);
    pj_display_worker_request_t second = request_for(
        &second_dirty, 7, 21);
    int slot;

    (void)submit_request(&model, &first, &slot);
    (void)submit_request(&model, &second, &slot);

    pj_display_worker_request_t taken;
    assert(pj_display_worker_model_take_request_at(
        &model, &slot, &taken, NULL, 100));
    assert(taken.layout_epoch == 7);
    assert(taken.interaction_generation == 21);
    assert(taken.dirty.partial);
    assert(taken.dirty.x == 10 && taken.dirty.y == 10);
    assert(taken.dirty.width == 100 && taken.dirty.height == 100);
    pj_display_worker_model_complete_at(&model, slot, 1, 200);
    assert(pj_display_worker_model_status(&model)
               .committed_interaction_generation == 21);
}

static void test_cross_layout_coalescing_promotes_full(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t dirty = partial_region(10, 10, 10, 10);
    pj_display_worker_request_t first = request_for(&dirty, 7, 20);
    pj_display_worker_request_t second = request_for(&dirty, 8, 21);
    int slot;

    (void)submit_request(&model, &first, &slot);
    (void)submit_request(&model, &second, &slot);
    pj_display_worker_request_t taken;
    assert(pj_display_worker_model_take_request_at(
        &model, &slot, &taken, NULL, 100));
    assert(taken.layout_epoch == 8);
    assert(!taken.dirty.partial);
    assert(taken.dirty.width == PJ_DISPLAY_WIDTH);
    assert(taken.dirty.height == PJ_DISPLAY_HEIGHT);
}

static void test_cross_layout_after_commit_promotes_full(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t dirty = partial_region(10, 10, 10, 10);
    pj_display_worker_request_t first = request_for(&dirty, 7, 20);
    pj_display_worker_request_t second = request_for(&dirty, 8, 21);
    int slot;
    (void)submit_request(&model, &first, &slot);
    pj_display_worker_request_t taken;
    assert(pj_display_worker_model_take_request_at(
        &model, &slot, &taken, NULL, 100));
    pj_display_worker_model_complete_at(&model, slot, 1, 200);

    (void)submit_request(&model, &second, &slot);
    assert(pj_display_worker_model_take_request_at(
        &model, &slot, &taken, NULL, 300));
    assert(!taken.dirty.partial);
    assert(taken.dirty.width == PJ_DISPLAY_WIDTH);
    assert(taken.dirty.height == PJ_DISPLAY_HEIGHT);
}

static void test_barrier_commits_interaction_without_pixels(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t full = full_region();
    pj_display_worker_request_t initial = request_for(&full, 1, 10);
    int slot;
    (void)submit_request(&model, &initial, &slot);
    pj_display_worker_request_t taken;
    assert(pj_display_worker_model_take_request_at(
        &model, &slot, &taken, NULL, 100));
    pj_display_worker_model_complete_at(&model, slot, 1, 200);

    pj_display_worker_request_t barrier = request_for(NULL, 1, 11);
    barrier.barrier = 1;
    barrier.visual_revision = 11;
    (void)submit_request(&model, &barrier, &slot);
    assert(pj_display_worker_model_take_request_at(
        &model, &slot, &taken, NULL, 300));
    assert(taken.barrier);
    assert(taken.dirty.width == 0 && taken.dirty.height == 0);
    pj_display_worker_model_complete_at(&model, slot, 1, 300);

    pj_display_worker_status_t status =
        pj_display_worker_model_status(&model);
    assert(status.committed_layout_epoch == 1);
    assert(status.committed_interaction_generation == 11);
    assert(status.committed_interaction_started_ms == 300);
    assert(!pj_display_worker_status_accepts_interaction(
        &status, 11, 300));
    assert(pj_display_worker_status_accepts_interaction(
        &status, 11, 301));
}

static void test_pending_pixels_survive_newer_barrier(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t dirty = partial_region(20, 30, 40, 50);
    pj_display_worker_request_t pixels = request_for(&dirty, 4, 8);
    pj_display_worker_request_t barrier = request_for(NULL, 4, 9);
    barrier.barrier = 1;
    int slot;

    (void)submit_request(&model, &pixels, &slot);
    (void)submit_request(&model, &barrier, &slot);
    pj_display_worker_request_t taken;
    assert(pj_display_worker_model_take_request_at(
        &model, &slot, &taken, NULL, 100));
    assert(!taken.barrier);
    assert(taken.dirty.partial && taken.dirty.x == 20);
    assert(taken.interaction_generation == 9);
}

static pj_display_worker_request_t cadence_request_for(
    const pj_ui_dirty_region_t *dirty, uint32_t sequence,
    uint64_t deadline_ms)
{
    pj_display_worker_request_t request = request_for(dirty, 9, 30);
    request.cadence_class = PJ_DISPLAY_CADENCE_SECONDS;
    request.cadence_sequence = sequence;
    request.cadence_deadline_ms = deadline_ms;
    return request;
}

static void test_cadence_backpressure_does_not_inflate_misses(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t dirty = partial_region(10, 10, 20, 20);
    assert(pj_display_worker_model_cadence_start(&model, 1, 1000));
    pj_display_worker_request_t first = cadence_request_for(
        &dirty, 1, 1000);
    int first_slot;
    (void)submit_request(&model, &first, &first_slot);

    int rejected_slot = -1;
    for (int retry = 0; retry < 10; retry++) {
        assert(!pj_display_worker_model_begin_submit_request(
            &model, &first, &rejected_slot));
    }
    pj_display_worker_request_t second = cadence_request_for(
        &dirty, 2, 2000);
    for (int retry = 0; retry < 10; retry++) {
        assert(!pj_display_worker_model_begin_submit_request(
            &model, &second, &rejected_slot));
    }
    assert(pj_display_worker_model_status(&model).cadence_misses == 0);

    pj_display_worker_request_t taken;
    assert(pj_display_worker_model_take_request_at(
        &model, &first_slot, &taken, NULL, 1000));
    int second_slot;
    (void)submit_request(&model, &second, &second_slot);
    for (int retry = 0; retry < 10; retry++) {
        assert(!pj_display_worker_model_begin_submit_request(
            &model, &second, &rejected_slot));
    }
    assert(pj_display_worker_model_status(&model).cadence_misses == 0);

    pj_display_worker_model_complete_at(&model, first_slot, 1, 1600);
    assert(pj_display_worker_model_take_request_at(
        &model, &second_slot, &taken, NULL, 2000));
    pj_display_worker_model_complete_at(&model, second_slot, 1, 2600);
    assert(pj_display_worker_model_status(&model).cadence_misses == 0);
}

static void test_cadence_cancel_handoffs_ready_pixels_without_a_miss(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t cadence_dirty = partial_region(10, 20, 30, 40);
    pj_ui_dirty_region_t pause_dirty = partial_region(100, 120, 20, 30);
    assert(pj_display_worker_model_cadence_start(&model, 1, 1000));

    pj_display_worker_request_t cadence = cadence_request_for(
        &cadence_dirty, 1, 1000);
    int cadence_slot;
    (void)submit_request(&model, &cadence, &cadence_slot);
    assert(model.slots[cadence_slot].state ==
           PJ_DISPLAY_WORKER_SLOT_READY);

    pj_display_worker_model_cadence_end(&model);
    pj_display_worker_status_t status =
        pj_display_worker_model_status(&model);
    assert(!status.cadence_active);
    assert(status.cadence_misses == 0);
    assert(model.cadence_handoff_pending);
    assert(!pj_display_worker_model_cadence_start(&model, 1, 2000));

    pj_display_worker_request_t pause = request_for(
        &pause_dirty, 9, 31);
    int pause_slot;
    (void)submit_request(&model, &pause, &pause_slot);
    assert(!model.cadence_handoff_pending);
    pj_display_worker_request_t taken;
    assert(pj_display_worker_model_take_request_at(
        &model, &pause_slot, &taken, NULL, 1000));
    assert(taken.cadence_class == PJ_DISPLAY_CADENCE_NONE);
    assert(taken.dirty.partial);
    assert(taken.dirty.x == 10 && taken.dirty.y == 20);
    assert(taken.dirty.width == 110 && taken.dirty.height == 130);
    pj_display_worker_model_complete_at(&model, pause_slot, 1, 1600);
    assert(pj_display_worker_model_cadence_start(&model, 1, 2000));
}

static void test_cadence_cancel_queues_ordinary_behind_physical_frame(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t first_dirty = partial_region(10, 10, 20, 20);
    pj_ui_dirty_region_t canceled_dirty = partial_region(40, 40, 20, 20);
    pj_ui_dirty_region_t navigation_dirty = partial_region(80, 80, 20, 20);
    assert(pj_display_worker_model_cadence_start(&model, 1, 1000));

    pj_display_worker_request_t first = cadence_request_for(
        &first_dirty, 1, 1000);
    int active_slot;
    (void)submit_request(&model, &first, &active_slot);
    pj_display_worker_request_t taken;
    assert(pj_display_worker_model_take_request_at(
        &model, &active_slot, &taken, NULL, 1000));

    pj_display_worker_request_t canceled = cadence_request_for(
        &canceled_dirty, 2, 2000);
    int canceled_slot;
    (void)submit_request(&model, &canceled, &canceled_slot);
    assert(canceled_slot != active_slot);

    pj_display_worker_model_cadence_end(&model);
    assert(model.slots[active_slot].state ==
           PJ_DISPLAY_WORKER_SLOT_DISPLAYING);
    assert(model.slots[canceled_slot].state ==
           PJ_DISPLAY_WORKER_SLOT_FREE);
    assert(pj_display_worker_model_status(&model).cadence_misses == 0);

    pj_display_worker_request_t navigation = request_for(
        &navigation_dirty, 9, 31);
    int navigation_slot;
    (void)submit_request(&model, &navigation, &navigation_slot);
    assert(navigation_slot == canceled_slot);
    assert(!pj_display_worker_model_take_request_at(
        &model, &navigation_slot, &taken, NULL, 1100));

    pj_display_worker_model_complete_at(&model, active_slot, 1, 1600);
    assert(pj_display_worker_model_take_request_at(
        &model, &navigation_slot, &taken, NULL, 1600));
    assert(taken.cadence_class == PJ_DISPLAY_CADENCE_NONE);
    assert(taken.dirty.partial);
    assert(taken.dirty.x == 40 && taken.dirty.y == 40);
    assert(taken.dirty.width == 60 && taken.dirty.height == 60);
    pj_display_worker_model_complete_at(
        &model, navigation_slot, 1, 2200);

    pj_display_worker_status_t status =
        pj_display_worker_model_status(&model);
    assert(status.cadence_starts == 1);
    assert(status.cadence_commits == 1);
    assert(status.cadence_last_committed_sequence == 1);
    assert(status.cadence_misses == 0);
    assert(status.ordering_errors == 0);
    assert(status.committed_interaction_generation == 31);
}

static void test_cadence_cancel_during_copy_is_intentional(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t dirty = partial_region(10, 10, 20, 20);
    assert(pj_display_worker_model_cadence_start(&model, 1, 1000));
    pj_display_worker_request_t cadence = cadence_request_for(
        &dirty, 1, 1000);

    int slot;
    assert(pj_display_worker_model_begin_submit_request(
        &model, &cadence, &slot));
    assert(model.slots[slot].state == PJ_DISPLAY_WORKER_SLOT_WRITING);
    pj_display_worker_model_cadence_end(&model);
    assert(model.slots[slot].state == PJ_DISPLAY_WORKER_SLOT_WRITING);
    assert(model.slots[slot].cancel_on_commit);
    assert(!pj_display_worker_model_commit_submit_request(
        &model, slot, &cadence, NULL));
    assert(pj_display_worker_model_is_idle(&model));
    assert(pj_display_worker_model_status(&model).cadence_misses == 0);
}

static void test_cleanup_waits_for_successful_full_handoff(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t partial = partial_region(40, 40, 80, 80);
    pj_ui_dirty_region_t full = full_region();
    assert(pj_display_worker_model_cadence_start(&model, 1, 1000));
    pj_display_worker_model_cleanup_due(&model);
    pj_display_worker_model_cadence_end(&model);
    assert(pj_display_worker_model_status(&model).cleanup_pending);

    pj_display_worker_request_t pause = request_for(&partial, 9, 31);
    int slot;
    (void)submit_request(&model, &pause, &slot);
    pj_display_worker_request_t taken;
    assert(pj_display_worker_model_take_request_at(
        &model, &slot, &taken, NULL, 1000));
    assert(taken.dirty.partial);
    assert(taken.cleanup_state == PJ_DISPLAY_CLEANUP_DEFERRED);
    pj_display_worker_model_complete_at(&model, slot, 1, 1600);
    assert(pj_display_worker_model_status(&model).cleanup_pending);

    pj_display_worker_request_t navigation = request_for(&full, 10, 32);
    (void)submit_request(&model, &navigation, &slot);
    assert(pj_display_worker_model_take_request_at(
        &model, &slot, &taken, NULL, 1700));
    assert(!taken.dirty.partial);
    assert(taken.cleanup_state == PJ_DISPLAY_CLEANUP_SATISFY);
    pj_display_worker_model_complete_at(&model, slot, 0, 1800);
    assert(pj_display_worker_model_status(&model).cleanup_pending);

    assert(pj_display_worker_model_take_request_at(
        &model, &slot, &taken, NULL, 2000));
    assert(!taken.dirty.partial);
    assert(taken.cleanup_state == PJ_DISPLAY_CLEANUP_SATISFY);
    pj_display_worker_model_complete_at(&model, slot, 1, 3800);
    assert(!pj_display_worker_model_status(&model).cleanup_pending);
}

static void test_hard_cadence_commits_120_consecutive_seconds(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t dirty = partial_region(40, 40, 80, 80);
    const uint64_t first_deadline = 1000;
    assert(pj_display_worker_model_cadence_start(
        &model, 1, first_deadline));

    for (uint32_t sequence = 1; sequence <= 120; sequence++) {
        uint64_t deadline = first_deadline +
            (uint64_t)(sequence - 1) *
                PJ_DISPLAY_WORKER_CADENCE_PERIOD_MS;
        pj_display_worker_request_t request = request_for(
            &dirty, 9, 30);
        request.visual_revision = sequence;
        request.cadence_class = PJ_DISPLAY_CADENCE_SECONDS;
        request.cadence_sequence = sequence;
        request.cadence_deadline_ms = deadline;
        request.barrier = sequence % 17 == 0;
        if (sequence == 30) {
            pj_display_worker_model_cleanup_due(&model);
            request.cleanup_state = PJ_DISPLAY_CLEANUP_DEFERRED;
        }

        int slot;
        (void)submit_request(&model, &request, &slot);
        pj_display_worker_request_t taken;
        assert(!pj_display_worker_model_take_request_at(
            &model, &slot, &taken, NULL, deadline - 1));
        uint64_t start = deadline + sequence % 30;
        assert(pj_display_worker_model_take_request_at(
            &model, &slot, &taken, NULL, start));
        assert(taken.cadence_sequence == sequence);
        assert(taken.barrier == (sequence % 17 == 0));
        pj_display_worker_model_complete_at(
            &model, slot, 1, deadline + 600);
    }

    pj_display_worker_status_t status =
        pj_display_worker_model_status(&model);
    assert(status.cadence_starts == 120);
    assert(status.cadence_commits == 120);
    assert(status.cadence_last_started_sequence == 120);
    assert(status.cadence_last_committed_sequence == 120);
    assert(status.cadence_max_start_lateness_ms == 29);
    assert(status.cadence_misses == 0);
    assert(status.cadence_overruns == 0);
    assert(status.superseded_frames == 0);
    assert(status.cleanup_pending);
    assert(status.cleanup_deferred_frames == 1);
    assert(!pj_display_worker_model_take_cleanup_pending(&model));

    pj_display_worker_model_cadence_end(&model);
    assert(pj_display_worker_model_take_cleanup_pending(&model));
    assert(!pj_display_worker_model_take_cleanup_pending(&model));
}

static void test_deferred_cleanup_metadata_is_informational(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t dirty = partial_region(40, 40, 80, 80);
    assert(pj_display_worker_model_cadence_start(&model, 1, 1000));
    pj_display_worker_request_t request = request_for(&dirty, 2, 3);
    request.cadence_class = PJ_DISPLAY_CADENCE_SECONDS;
    request.cadence_sequence = 1;
    request.cadence_deadline_ms = 1000;
    request.cleanup_state = PJ_DISPLAY_CLEANUP_DEFERRED;
    int slot;
    (void)submit_request(&model, &request, &slot);
    assert(!pj_display_worker_model_status(&model).cleanup_pending);
    pj_display_worker_request_t taken;
    assert(pj_display_worker_model_take_request_at(
        &model, &slot, &taken, NULL, 1000));
    pj_display_worker_model_complete_at(&model, slot, 1, 1600);
    assert(!pj_display_worker_model_status(&model).cleanup_pending);

    pj_display_worker_model_cleanup_due(&model);
    pj_display_worker_status_t status =
        pj_display_worker_model_status(&model);
    assert(status.cleanup_pending);
    assert(status.cleanup_deferred_frames == 1);
}

static void test_cadence_lateness_and_overrun_are_faults(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t dirty = partial_region(40, 40, 80, 80);
    assert(pj_display_worker_model_cadence_start(&model, 7, 1000));
    pj_display_worker_request_t request = request_for(&dirty, 2, 3);
    request.cadence_class = PJ_DISPLAY_CADENCE_SECONDS;
    request.cadence_sequence = 7;
    request.cadence_deadline_ms = 1000;
    int slot;
    (void)submit_request(&model, &request, &slot);
    pj_display_worker_request_t taken;
    assert(pj_display_worker_model_take_request_at(
        &model, &slot, &taken, NULL, 1076));
    pj_display_worker_model_complete_at(&model, slot, 1, 2000);

    pj_display_worker_status_t status =
        pj_display_worker_model_status(&model);
    assert(status.cadence_starts == 1);
    assert(status.cadence_commits == 1);
    assert(status.cadence_max_start_lateness_ms == 76);
    assert(status.cadence_overruns == 1);
    assert(status.cadence_misses == 1);
}

static void test_failed_cadence_frame_recovers_sequence_tracking(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);
    pj_ui_dirty_region_t dirty = partial_region(40, 40, 80, 80);
    assert(pj_display_worker_model_cadence_start(&model, 1, 1000));
    pj_display_worker_request_t first = request_for(&dirty, 2, 3);
    first.cadence_class = PJ_DISPLAY_CADENCE_SECONDS;
    first.cadence_sequence = 1;
    first.cadence_deadline_ms = 1000;
    int first_slot;
    (void)submit_request(&model, &first, &first_slot);
    pj_display_worker_request_t taken;
    assert(pj_display_worker_model_take_request_at(
        &model, &first_slot, &taken, NULL, 1000));

    pj_display_worker_request_t second = first;
    second.cadence_sequence = 2;
    second.cadence_deadline_ms = 2000;
    int second_slot;
    assert(pj_display_worker_model_begin_submit_request(
        &model, &second, &second_slot));
    assert(second_slot != first_slot);
    pj_display_worker_model_complete_at(&model, first_slot, 0, 1600);
    assert(pj_display_worker_model_commit_submit_request(
        &model, second_slot, &second, NULL));
    assert(pj_display_worker_model_take_request_at(
        &model, &second_slot, &taken, NULL, 2000));
    assert(!taken.dirty.partial);
    pj_display_worker_model_complete_at(&model, second_slot, 1, 2600);

    pj_display_worker_status_t status =
        pj_display_worker_model_status(&model);
    assert(status.cadence_commits == 1);
    assert(status.cadence_last_committed_sequence == 2);
    assert(status.cadence_misses == 1);
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
    test_interaction_is_not_presented_until_physical_commit();
    test_new_layout_supersedes_pending_hit_map_as_full();
    test_slow_display_keeps_generations_and_final_layout_ordered();
    test_input_requires_capture_after_interaction_commit();
    test_input_capture_remains_ordered_after_32_bit_uptime();
    test_touch_contact_before_commit_is_rejected_after_stabilizing();
    test_partial_rate_limit_is_global_and_size_independent();
    test_deferred_metrics_are_explicit();
    test_semantic_cadence_fault_is_latched_per_sequence();
    test_generation_order_survives_wrap();
    test_same_layout_semantics_remain_partial();
    test_cross_layout_coalescing_promotes_full();
    test_cross_layout_after_commit_promotes_full();
    test_barrier_commits_interaction_without_pixels();
    test_pending_pixels_survive_newer_barrier();
    test_cadence_backpressure_does_not_inflate_misses();
    test_cadence_cancel_handoffs_ready_pixels_without_a_miss();
    test_cadence_cancel_queues_ordinary_behind_physical_frame();
    test_cadence_cancel_during_copy_is_intentional();
    test_cleanup_waits_for_successful_full_handoff();
    test_hard_cadence_commits_120_consecutive_seconds();
    test_deferred_cleanup_metadata_is_informational();
    test_cadence_lateness_and_overrun_are_faults();
    test_failed_cadence_frame_recovers_sequence_tracking();
    puts("display worker tests passed");
    return 0;
}
