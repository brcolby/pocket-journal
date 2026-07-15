#include "pj_display_worker.h"

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

static void submit(pj_display_worker_model_t *model,
                   const pj_ui_dirty_region_t *dirty,
                   int *slot)
{
    assert(pj_display_worker_model_begin_submit(model, slot));
    assert(model->slots[*slot].state == PJ_DISPLAY_WORKER_SLOT_WRITING);
    assert(pj_display_worker_model_commit_submit(model, *slot, dirty));
}

static void test_uncommitted_slot_cannot_be_displayed(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);

    int slot = -1;
    assert(!pj_display_worker_model_take(&model, &slot, &(pj_ui_dirty_region_t){0}, NULL));
    assert(pj_display_worker_model_begin_submit(&model, &slot));
    assert(!pj_display_worker_model_take(&model, &slot, &(pj_ui_dirty_region_t){0}, NULL));
}

static void test_invalid_submission_releases_reserved_slot(void)
{
    pj_display_worker_model_t model;
    pj_display_worker_model_init(&model);

    int slot;
    assert(pj_display_worker_model_begin_submit(&model, &slot));
    assert(!pj_display_worker_model_commit_submit(
        &model, slot, &(pj_ui_dirty_region_t){0}));
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
    assert(pj_display_worker_model_take(&model, &latest_slot, &dirty, &generation));
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
    assert(pj_display_worker_model_take(&model, &slot, &dirty, NULL));
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
    assert(pj_display_worker_model_take(&model, &active, &dirty, NULL));

    int pending;
    submit(&model, &second, &pending);
    assert(pending != active);
    assert(!pj_display_worker_model_take(&model, &pending, &dirty, NULL));

    pj_display_worker_model_complete(&model, active, 1);
    assert(pj_display_worker_model_take(&model, &pending, &dirty, NULL));
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
    assert(pj_display_worker_model_take(&model, &active, &dirty, NULL));
    int pending;
    submit(&model, &latest, &pending);

    pj_display_worker_model_complete(&model, active, 0);
    assert(pj_display_worker_model_take(&model, &pending, &dirty, NULL));
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
    assert(pj_display_worker_model_take(&model, &active, &dirty, NULL));

    int writing;
    assert(pj_display_worker_model_begin_submit(&model, &writing));
    pj_display_worker_model_complete(&model, active, 0);
    assert(pj_display_worker_model_commit_submit(&model, writing, &latest));
    assert(pj_display_worker_model_take(&model, &writing, &dirty, NULL));
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
    assert(pj_display_worker_model_take(&model, &slot, &dirty, NULL));
    pj_display_worker_model_complete(&model, slot, 0);
    assert(pj_display_worker_model_take(&model, &slot, &dirty, NULL));
    assert(!dirty.partial);
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
    assert(!pj_display_worker_model_take(&model, &slot, &dirty, NULL));
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
    puts("display worker tests passed");
    return 0;
}
