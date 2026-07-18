#include "pj_display_refresh.h"
#include "pj_display_worker.h"
#include "pj_ui.h"
#include "pj_ui_presenter.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    pj_display_worker_model_t worker;
    pj_ui_presenter_t presenter;
    pj_framebuffer_t frames[PJ_DISPLAY_WORKER_SLOT_COUNT];
    pj_framebuffer_t panel;
    pj_display_refresh_policy_t refresh;
    pj_ui_dirty_region_t last_dirty;
    pj_ui_frame_result_t last_result;
    int panel_valid;
    uint64_t committed_at_ms;
} pipeline_t;

static void pipeline_init(pipeline_t *pipeline)
{
    memset(pipeline, 0, sizeof(*pipeline));
    pj_display_worker_model_init(&pipeline->worker);
    pj_ui_presenter_init(&pipeline->presenter);
    pj_display_refresh_policy_init(
        &pipeline->refresh, PJ_DISPLAY_REFRESH_DEFAULT_PARTIAL_LIMIT);
}

static uint32_t pipeline_submit(pipeline_t *pipeline, pj_ui_context_t *ui,
                                uint32_t interaction_generation,
                                pj_framebuffer_t *rendered)
{
    pj_ui_presenter_revision_t revision = pj_ui_presenter_revision(ui);
    revision.interaction_generation = interaction_generation;
    pj_ui_presenter_frame_t frame;
    pj_ui_frame_result_t result = pj_ui_presenter_prepare(
        &pipeline->presenter, ui, &revision, &frame);
    pipeline->last_result = result;
    pipeline->last_dirty = frame.dirty;
    if (result == PJ_UI_FRAME_IDLE || result == PJ_UI_FRAME_NOOP) {
        if (result == PJ_UI_FRAME_NOOP) {
            assert(pj_ui_presenter_accept(
                &pipeline->presenter, frame.token));
        }
        if (rendered != NULL && pipeline->presenter.accepted_valid) {
            *rendered = pipeline->presenter.accepted_framebuffer;
        }
        return 0;
    }

    pj_display_worker_request_t request;
    pj_display_worker_request_init(&request, &frame.dirty);
    request.layout_epoch = revision.layout_epoch;
    request.interaction_generation = revision.interaction_generation;
    request.visual_revision = revision.visual_revision;
    request.full_refresh_revision = revision.full_refresh_revision;
    request.barrier = result == PJ_UI_FRAME_BARRIER;
    int slot = -1;
    assert(pj_display_worker_model_begin_submit_request(
        &pipeline->worker, &request, &slot));
    pipeline->frames[slot] = *frame.framebuffer;
    uint32_t generation = 0;
    assert(pj_display_worker_model_commit_submit_request(
        &pipeline->worker, slot, &request, &generation));
    assert(pj_ui_presenter_accept(&pipeline->presenter, frame.token));
    if (rendered != NULL) {
        *rendered = *frame.framebuffer;
    }
    return generation;
}

static uint32_t pipeline_commit_next(pipeline_t *pipeline,
                                     uint32_t *interaction_generation)
{
    int slot = -1;
    pj_display_worker_request_t request;
    uint32_t generation = 0;
    assert(pj_display_worker_model_take_request_at(
        &pipeline->worker, &slot, &request, &generation,
        pipeline->committed_at_ms));
    if (request.barrier) {
        pipeline->committed_at_ms += 100U;
        pj_display_worker_model_complete_at(
            &pipeline->worker, slot, 1, pipeline->committed_at_ms);
        if (interaction_generation != NULL) {
            *interaction_generation = request.interaction_generation;
        }
        return generation;
    }
    pj_display_refresh_plan_t plan = pj_display_refresh_plan(
        &pipeline->refresh, &pipeline->frames[slot], &pipeline->panel,
        pipeline->panel_valid, &request.dirty);
    assert(plan.kind != PJ_DISPLAY_REFRESH_NOOP ||
           memcmp(&pipeline->frames[slot], &pipeline->panel,
                  sizeof(pipeline->panel)) == 0);
    assert(pj_display_refresh_complete(
        &pipeline->refresh, &pipeline->panel, &pipeline->panel_valid,
        &pipeline->frames[slot], &plan, 1, 600000, 580000));
    pipeline->committed_at_ms += 100U;
    pj_display_worker_model_complete_at(
        &pipeline->worker, slot, 1, pipeline->committed_at_ms);
    assert(memcmp(&pipeline->frames[slot], &pipeline->panel,
                  sizeof(pipeline->panel)) == 0);
    if (interaction_generation != NULL) {
        *interaction_generation = request.interaction_generation;
    }
    return generation;
}

static int point_inside(const pj_ui_dirty_region_t *dirty, int x, int y)
{
    if (!dirty->partial) {
        return 1;
    }
    return x >= dirty->x && y >= dirty->y &&
        x < dirty->x + dirty->width && y < dirty->y + dirty->height;
}

static void assert_frame_delta_covered(const pj_framebuffer_t *before,
                                       const pj_framebuffer_t *after,
                                       const pj_ui_dirty_region_t *dirty)
{
    for (int y = 0; y < PJ_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
            if (pj_framebuffer_get(before, x, y) !=
                pj_framebuffer_get(after, x, y)) {
                assert(point_inside(dirty, x, y));
            }
        }
    }
}

static pj_ui_dirty_region_t exact_delta_bounds(
    const pj_framebuffer_t *before, const pj_framebuffer_t *after)
{
    int min_x = PJ_DISPLAY_WIDTH;
    int min_y = PJ_DISPLAY_HEIGHT;
    int max_x = -1;
    int max_y = -1;
    for (int y = 0; y < PJ_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
            if (pj_framebuffer_get(before, x, y) ==
                pj_framebuffer_get(after, x, y)) {
                continue;
            }
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
        }
    }
    if (max_x < min_x) {
        return (pj_ui_dirty_region_t) {0};
    }
    return (pj_ui_dirty_region_t) {
        .x = min_x,
        .y = min_y,
        .width = max_x - min_x + 1,
        .height = max_y - min_y + 1,
        .partial = 1,
    };
}

static void copy_region(pj_framebuffer_t *destination,
                        const pj_framebuffer_t *source,
                        const pj_ui_dirty_region_t *dirty)
{
    for (int y = dirty->y; y < dirty->y + dirty->height; y++) {
        for (int x = dirty->x; x < dirty->x + dirty->width; x++) {
            size_t bit = (size_t)y * PJ_DISPLAY_WIDTH + (size_t)x;
            uint8_t mask = (uint8_t)(1u << (bit & 7u));
            if (pj_framebuffer_get(source, x, y)) {
                destination->pixels[bit >> 3u] |= mask;
            } else {
                destination->pixels[bit >> 3u] &= (uint8_t)~mask;
            }
        }
    }
}

static int guarded_touch(pipeline_t *pipeline, pj_ui_context_t *ui,
                         uint32_t interaction_generation,
                         uint64_t captured_at_ms,
                         int x, int y)
{
    pj_display_worker_status_t status =
        pj_display_worker_model_status(&pipeline->worker);
    if (!pj_display_worker_status_accepts_interaction(
            &status, interaction_generation, captured_at_ms)) {
        pj_display_worker_model_note_input_deferred(&pipeline->worker);
        return 0;
    }
    return pj_ui_handle_touch(ui, x, y, PJ_TOUCH_TAP);
}

static pj_ui_time_projection_t stopwatch_projection(int seconds)
{
    return (pj_ui_time_projection_t) {
        .stopwatch_running = 1,
        .stopwatch_elapsed_ms = (uint64_t)seconds * 1000u,
    };
}

typedef enum {
    HARD_CADENCE_RECORD = 0,
    HARD_CADENCE_STOPWATCH,
    HARD_CADENCE_TIMER,
    HARD_CADENCE_INTERVAL,
} hard_cadence_screen_t;

static void hard_cadence_init_ui(hard_cadence_screen_t screen,
                                 pj_ui_context_t *ui)
{
    pj_ui_init(ui);
    switch (screen) {
    case HARD_CADENCE_RECORD:
        ui->state = PJ_UI_STATE_RECORD;
        pj_ui_set_audio_state(ui, 1, 0);
        pj_ui_set_recording_elapsed(ui, 0);
        break;
    case HARD_CADENCE_STOPWATCH: {
        ui->state = PJ_UI_STATE_STOPWATCH;
        pj_ui_time_projection_t projection = stopwatch_projection(0);
        pj_ui_set_time_projection(ui, &projection);
        break;
    }
    case HARD_CADENCE_TIMER: {
        ui->state = PJ_UI_STATE_TIMER;
        pj_ui_time_projection_t projection = {
            .timer_running = 1,
            .timer_remaining_ms = UINT64_C(300000),
        };
        pj_ui_set_time_projection(ui, &projection);
        break;
    }
    case HARD_CADENCE_INTERVAL: {
        ui->state = PJ_UI_STATE_INTERVAL;
        pj_ui_time_projection_t projection = {
            .interval_running = 1,
            .interval_remaining_ms = UINT64_C(30000),
            .interval_phase = 1,
        };
        pj_ui_set_time_projection(ui, &projection);
        break;
    }
    }
    pj_ui_request_full_presentation(ui);
}

static void hard_cadence_advance_ui(hard_cadence_screen_t screen,
                                    pj_ui_context_t *ui,
                                    uint32_t sequence)
{
    switch (screen) {
    case HARD_CADENCE_RECORD:
        pj_ui_set_recording_elapsed(
            ui, (uint64_t)sequence * UINT64_C(1000));
        assert(ui->record_state == PJ_RECORD_ACTIVE);
        assert(ui->recording_seconds == (int)sequence);
        break;
    case HARD_CADENCE_STOPWATCH: {
        pj_ui_time_projection_t projection =
            stopwatch_projection((int)sequence);
        pj_ui_set_time_projection(ui, &projection);
        assert(ui->stopwatch_running);
        assert(ui->stopwatch_seconds == (int)sequence);
        break;
    }
    case HARD_CADENCE_TIMER: {
        int remaining = sequence < 60U ?
            300 - (int)sequence : 331 - (int)sequence;
        if (sequence == 60U) {
            assert(pj_ui_handle_touch(ui, 50, 125, PJ_TOUCH_TAP));
            pj_ui_time_command_t command;
            assert(pj_ui_consume_time_command(ui, &command));
            assert(command.type == PJ_UI_TIME_COMMAND_TIMER_SET);
            assert(command.duration_ms == UINT64_C(271000));
            assert(ui->timer_seconds == remaining);
        }
        pj_ui_time_projection_t projection = {
            .timer_running = 1,
            .timer_remaining_ms =
                (uint64_t)remaining * UINT64_C(1000),
        };
        pj_ui_set_time_projection(ui, &projection);
        assert(ui->timer_running);
        assert(ui->timer_seconds == remaining);
        break;
    }
    case HARD_CADENCE_INTERVAL: {
        uint32_t within_round = sequence % 30U;
        int remaining = sequence < 60U ?
            (within_round == 0U ? 30 : 30 - (int)within_round) :
            121 - (int)sequence;
        int round = sequence < 30U ? 1 : (sequence < 60U ? 2 : 3);
        if (sequence == 60U) {
            assert(pj_ui_handle_touch(ui, 50, 125, PJ_TOUCH_TAP));
            pj_ui_time_command_t command;
            assert(pj_ui_consume_time_command(ui, &command));
            assert(command.type == PJ_UI_TIME_COMMAND_INTERVAL_SET);
            assert(command.duration_ms == UINT64_C(31000));
            assert(command.secondary_duration_ms == UINT64_C(31000));
            assert(ui->interval_seconds == 31);
        }
        pj_ui_time_projection_t projection = {
            .interval_running = 1,
            .interval_remaining_ms =
                (uint64_t)remaining * UINT64_C(1000),
            .interval_phase = (uint64_t)round,
        };
        pj_ui_set_time_projection(ui, &projection);
        assert(ui->interval_running);
        assert(ui->interval_seconds == remaining);
        assert(ui->interval_round == round);
        break;
    }
    }
}

static pj_display_worker_request_t request_from_presenter_frame(
    const pj_ui_presenter_frame_t *frame)
{
    pj_display_worker_request_t request;
    pj_display_worker_request_init(&request, &frame->dirty);
    request.layout_epoch = frame->revision.layout_epoch;
    request.interaction_generation =
        frame->revision.interaction_generation;
    request.visual_revision = frame->revision.visual_revision;
    request.full_refresh_revision =
        frame->revision.full_refresh_revision;
    request.barrier = frame->result == PJ_UI_FRAME_BARRIER;
    return request;
}

static void presenter_accept_initial_clock(pj_ui_presenter_t *presenter,
                                           pj_ui_context_t *ui,
                                           pj_framebuffer_t *accepted)
{
    pj_ui_init(ui);
    ui->state = PJ_UI_STATE_TIME_TEMP;
    pj_ui_request_full_presentation(ui);
    pj_ui_presenter_revision_t revision = pj_ui_presenter_revision(ui);
    pj_ui_presenter_frame_t frame;
    assert(pj_ui_presenter_prepare(
        presenter, ui, &revision, &frame) == PJ_UI_FRAME_FULL);
    *accepted = *frame.framebuffer;
    assert(pj_ui_presenter_accept(presenter, frame.token));
}

static void test_presenter_reports_exact_partial_bounds(void)
{
    pj_ui_presenter_t presenter;
    pj_ui_presenter_init(&presenter);
    pj_ui_context_t ui;
    pj_framebuffer_t before;
    presenter_accept_initial_clock(&presenter, &ui, &before);

    pj_ui_set_status(&ui, 39, 23, 46);
    pj_ui_presenter_revision_t revision = pj_ui_presenter_revision(&ui);
    pj_ui_presenter_frame_t frame;
    assert(pj_ui_presenter_prepare(
        &presenter, &ui, &revision, &frame) == PJ_UI_FRAME_PARTIAL);
    pj_ui_dirty_region_t exact = exact_delta_bounds(
        &before, frame.framebuffer);
    assert(frame.dirty.partial);
    assert(frame.dirty.x == exact.x && frame.dirty.y == exact.y);
    assert(frame.dirty.width == exact.width &&
           frame.dirty.height == exact.height);

    pj_framebuffer_t patched = before;
    copy_region(&patched, frame.framebuffer, &frame.dirty);
    assert(memcmp(&patched, frame.framebuffer, sizeof(patched)) == 0);
}

static void test_presenter_rejection_retains_lossless_snapshot(void)
{
    pj_ui_presenter_t presenter;
    pj_ui_presenter_init(&presenter);
    pj_ui_context_t ui;
    pj_framebuffer_t initial;
    presenter_accept_initial_clock(&presenter, &ui, &initial);

    pj_ui_set_status(&ui, 50, 22, 45);
    pj_ui_presenter_revision_t first_revision =
        pj_ui_presenter_revision(&ui);
    pj_ui_presenter_frame_t first;
    assert(pj_ui_presenter_prepare(
        &presenter, &ui, &first_revision, &first) == PJ_UI_FRAME_PARTIAL);
    pj_framebuffer_t rejected = *first.framebuffer;
    uint32_t rejected_token = first.token;
    assert(pj_ui_presenter_reject(&presenter, rejected_token));

    pj_ui_set_status(&ui, 10, 28, 60);
    pj_ui_presenter_revision_t latest_revision =
        pj_ui_presenter_revision(&ui);
    pj_ui_presenter_frame_t retry;
    assert(pj_ui_presenter_prepare(
        &presenter, &ui, &latest_revision, &retry) ==
        PJ_UI_FRAME_PARTIAL);
    assert(retry.token == rejected_token);
    assert(retry.revision.visual_revision == first_revision.visual_revision);
    assert(memcmp(retry.framebuffer, &rejected, sizeof(rejected)) == 0);
    assert(!pj_ui_presenter_accept(&presenter, rejected_token + 1));
    assert(pj_ui_presenter_accept(&presenter, rejected_token));

    pj_ui_presenter_frame_t latest;
    assert(pj_ui_presenter_prepare(
        &presenter, &ui, &latest_revision, &latest) ==
        PJ_UI_FRAME_PARTIAL);
    assert(memcmp(latest.framebuffer, &rejected, sizeof(rejected)) != 0);
}

static void test_presenter_barrier_noop_and_full_policy(void)
{
    pj_ui_presenter_t presenter;
    pj_ui_presenter_init(&presenter);
    pj_ui_context_t ui;
    pj_framebuffer_t initial;
    presenter_accept_initial_clock(&presenter, &ui, &initial);

    pj_ui_presenter_revision_t revision = pj_ui_presenter_revision(&ui);
    revision.interaction_generation++;
    pj_ui_presenter_frame_t frame;
    assert(pj_ui_presenter_prepare(
        &presenter, &ui, &revision, &frame) == PJ_UI_FRAME_BARRIER);
    assert(frame.dirty.width == 0 && frame.dirty.height == 0);
    assert(memcmp(frame.framebuffer, &initial, sizeof(initial)) == 0);
    assert(pj_ui_presenter_accept(&presenter, frame.token));

    revision.visual_revision++;
    assert(pj_ui_presenter_prepare(
        &presenter, &ui, &revision, &frame) == PJ_UI_FRAME_NOOP);
    assert(pj_ui_presenter_has_pending(&presenter));
    assert(pj_ui_presenter_accept(&presenter, frame.token));
    assert(presenter.accepted_revision.visual_revision ==
           revision.visual_revision);

    revision.layout_epoch++;
    assert(pj_ui_presenter_prepare(
        &presenter, &ui, &revision, &frame) == PJ_UI_FRAME_FULL);
    assert(!frame.dirty.partial);
    assert(frame.dirty.width == PJ_DISPLAY_WIDTH &&
           frame.dirty.height == PJ_DISPLAY_HEIGHT);
}

static void test_hidden_new_layout_controls_are_inert_until_commit(void)
{
    pipeline_t pipeline;
    pipeline_init(&pipeline);
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    pj_ui_wake(&ui);

    (void)pipeline_submit(&pipeline, &ui, 1, NULL);
    (void)pipeline_commit_next(&pipeline, NULL);
    assert(pj_display_worker_model_status(&pipeline.worker)
               .committed_interaction_generation == 1);

    ui.state = PJ_UI_STATE_TIME;
    pj_ui_request_full_presentation(&ui);
    (void)pipeline_submit(&pipeline, &ui, 2, NULL);
    assert(pj_display_worker_model_status(&pipeline.worker)
               .committed_interaction_generation != 2);

    assert(!guarded_touch(&pipeline, &ui, 2, 101, 50, 150));
    assert(ui.state == PJ_UI_STATE_TIME);
    assert(pipeline.worker.input_deferred_events == 1);

    uint32_t committed_epoch = 0;
    (void)pipeline_commit_next(&pipeline, &committed_epoch);
    assert(committed_epoch == 2);
    assert(guarded_touch(&pipeline, &ui, 2, 201, 50, 150));
    assert(ui.state == PJ_UI_STATE_TIMER);
}

static void test_rapid_digit_updates_coalesce_to_exact_latest_frame(void)
{
    pipeline_t pipeline;
    pipeline_init(&pipeline);
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_STOPWATCH;
    ui.stopwatch_running = 1;
    pj_ui_request_full_presentation(&ui);
    pj_framebuffer_t previous;
    (void)pipeline_submit(&pipeline, &ui, 3, &previous);
    (void)pipeline_commit_next(&pipeline, NULL);

    const int values[] = {9, 10, 19, 20, 40, 41, 42, 59, 60};
    pj_framebuffer_t latest = previous;
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        pj_ui_time_projection_t projection = stopwatch_projection(values[i]);
        pj_ui_set_time_projection(&ui, &projection);
        pj_framebuffer_t rendered;
        (void)pipeline_submit(&pipeline, &ui, 3, &rendered);
        assert_frame_delta_covered(
            &previous, &rendered, &pipeline.last_dirty);
        latest = rendered;
        previous = rendered;
    }

    assert(pipeline.worker.superseded_frames ==
           sizeof(values) / sizeof(values[0]) - 1u);
    (void)pipeline_commit_next(&pipeline, NULL);
    assert(memcmp(&pipeline.panel, &latest, sizeof(latest)) == 0);
    assert(pipeline.worker.ordering_errors == 0);
}

static void test_every_stopwatch_second_has_complete_dirty_coverage(void)
{
    pipeline_t pipeline;
    pipeline_init(&pipeline);
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_STOPWATCH;
    ui.stopwatch_running = 1;
    pj_ui_request_full_presentation(&ui);
    pj_framebuffer_t previous;
    (void)pipeline_submit(&pipeline, &ui, 4, &previous);
    (void)pipeline_commit_next(&pipeline, NULL);

    for (int second = 1; second <= 120; second++) {
        pj_ui_time_projection_t projection = stopwatch_projection(second);
        pj_ui_set_time_projection(&ui, &projection);
        pj_framebuffer_t rendered;
        (void)pipeline_submit(&pipeline, &ui, 4, &rendered);
        assert_frame_delta_covered(
            &previous, &rendered, &pipeline.last_dirty);
        for (int y = PJ_DISPLAY_HEIGHT / 2; y < PJ_DISPLAY_HEIGHT; y++) {
            for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
                assert(pj_framebuffer_get(&previous, x, y) ==
                       pj_framebuffer_get(&rendered, x, y));
            }
        }
        (void)pipeline_commit_next(&pipeline, NULL);
        previous = rendered;
    }
    assert(pipeline.worker.committed_generation == 121);
    assert(pipeline.worker.ordering_errors == 0);
}

static void test_cadence_cancel_preserves_pixels_and_defers_cleanup(void)
{
    const uint64_t first_deadline_ms = 2800U;
    pj_display_worker_model_t worker;
    pj_display_worker_model_init(&worker);
    pj_ui_presenter_t presenter;
    pj_ui_presenter_init(&presenter);
    pj_display_refresh_policy_t refresh;
    pj_display_refresh_policy_init(
        &refresh, PJ_DISPLAY_REFRESH_DEFAULT_PARTIAL_LIMIT);
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_STOPWATCH;
    pj_ui_time_projection_t projection = stopwatch_projection(0);
    pj_ui_set_time_projection(&ui, &projection);
    pj_ui_request_full_presentation(&ui);

    pj_ui_presenter_revision_t revision =
        pj_ui_presenter_revision(&ui);
    pj_ui_presenter_frame_t frame;
    assert(pj_ui_presenter_prepare(
        &presenter, &ui, &revision, &frame) == PJ_UI_FRAME_FULL);
    pj_framebuffer_t panel = {0};
    int panel_valid = 0;
    pj_framebuffer_t initial = *frame.framebuffer;
    pj_display_worker_request_t request =
        request_from_presenter_frame(&frame);
    int slot;
    assert(pj_display_worker_model_begin_submit_request(
        &worker, &request, &slot));
    assert(pj_display_worker_model_commit_submit_request(
        &worker, slot, &request, NULL));
    assert(pj_ui_presenter_accept(&presenter, frame.token));
    pj_display_worker_request_t taken;
    assert(pj_display_worker_model_take_request_at(
        &worker, &slot, &taken, NULL, 0U));
    pj_display_refresh_plan_t plan = pj_display_refresh_plan(
        &refresh, &initial, &panel, panel_valid, &taken.dirty);
    assert(plan.kind == PJ_DISPLAY_REFRESH_FULL);
    assert(pj_display_refresh_complete(
        &refresh, &panel, &panel_valid, &initial, &plan, 1,
        1800000U, 1780000U));
    pj_display_worker_model_complete_at(&worker, slot, 1, 1800U);
    assert(memcmp(&panel, &initial, sizeof(panel)) == 0);
    assert(pj_display_worker_model_cadence_start(
        &worker, 1U, first_deadline_ms));

    projection = stopwatch_projection(1);
    pj_ui_set_time_projection(&ui, &projection);
    revision = pj_ui_presenter_revision(&ui);
    assert(pj_ui_presenter_prepare(
        &presenter, &ui, &revision, &frame) == PJ_UI_FRAME_PARTIAL);
    pj_framebuffer_t first = *frame.framebuffer;
    request = request_from_presenter_frame(&frame);
    request.cadence_class = PJ_DISPLAY_CADENCE_SECONDS;
    request.cadence_sequence = 1U;
    request.cadence_deadline_ms = first_deadline_ms;
    assert(pj_display_worker_model_begin_submit_request(
        &worker, &request, &slot));
    int first_slot = slot;
    assert(pj_display_worker_model_commit_submit_request(
        &worker, first_slot, &request, NULL));
    assert(pj_ui_presenter_accept(&presenter, frame.token));
    assert(pj_display_worker_model_take_request_at(
        &worker, &first_slot, &taken, NULL, first_deadline_ms));
    assert(taken.cleanup_state == PJ_DISPLAY_CLEANUP_DEFERRED);

    projection = stopwatch_projection(2);
    pj_ui_set_time_projection(&ui, &projection);
    revision = pj_ui_presenter_revision(&ui);
    assert(pj_ui_presenter_prepare(
        &presenter, &ui, &revision, &frame) == PJ_UI_FRAME_PARTIAL);
    request = request_from_presenter_frame(&frame);
    request.cadence_class = PJ_DISPLAY_CADENCE_SECONDS;
    request.cadence_sequence = 2U;
    request.cadence_deadline_ms = first_deadline_ms +
        PJ_DISPLAY_WORKER_CADENCE_PERIOD_MS;
    int canceled_slot;
    assert(pj_display_worker_model_begin_submit_request(
        &worker, &request, &canceled_slot));
    assert(canceled_slot != first_slot);
    assert(pj_display_worker_model_commit_submit_request(
        &worker, canceled_slot, &request, NULL));
    assert(pj_ui_presenter_accept(&presenter, frame.token));

    /* Model the 30-partial cleanup becoming due during the active clock. */
    refresh.cleanup_pending = 1;
    pj_display_worker_model_cleanup_due(&worker);
    pj_display_worker_model_cadence_end(&worker);
    assert(worker.slots[first_slot].state ==
           PJ_DISPLAY_WORKER_SLOT_DISPLAYING);
    assert(worker.slots[canceled_slot].state ==
           PJ_DISPLAY_WORKER_SLOT_FREE);
    assert(pj_display_worker_model_status(&worker).cadence_misses == 0U);

    projection.stopwatch_running = 0;
    projection.stopwatch_elapsed_ms = UINT64_C(2000);
    pj_ui_set_time_projection(&ui, &projection);
    revision = pj_ui_presenter_revision(&ui);
    assert(pj_ui_presenter_prepare(
        &presenter, &ui, &revision, &frame) == PJ_UI_FRAME_PARTIAL);
    pj_framebuffer_t paused = *frame.framebuffer;
    request = request_from_presenter_frame(&frame);
    int pause_slot;
    assert(pj_display_worker_model_begin_submit_request(
        &worker, &request, &pause_slot));
    assert(pause_slot == canceled_slot);
    assert(pj_display_worker_model_commit_submit_request(
        &worker, pause_slot, &request, NULL));
    assert(pj_ui_presenter_accept(&presenter, frame.token));

    pj_display_refresh_set_cleanup_deferred(&refresh, 1);
    plan = pj_display_refresh_plan(
        &refresh, &first, &panel, panel_valid, &taken.dirty);
    assert(plan.kind == PJ_DISPLAY_REFRESH_PARTIAL);
    assert(pj_display_refresh_complete(
        &refresh, &panel, &panel_valid, &first, &plan, 1,
        600000U, 580000U));
    pj_display_worker_model_complete_at(
        &worker, first_slot, 1, first_deadline_ms + 600U);
    assert(memcmp(&panel, &first, sizeof(panel)) == 0);

    assert(pj_display_worker_model_take_request_at(
        &worker, &pause_slot, &taken, NULL,
        first_deadline_ms + 600U));
    assert(taken.dirty.partial);
    assert(taken.cleanup_state == PJ_DISPLAY_CLEANUP_DEFERRED);
    pj_framebuffer_t reconstructed = panel;
    copy_region(&reconstructed, &paused, &taken.dirty);
    assert(memcmp(&reconstructed, &paused, sizeof(paused)) == 0);
    pj_display_refresh_set_cleanup_deferred(&refresh, 1);
    plan = pj_display_refresh_plan(
        &refresh, &paused, &panel, panel_valid, &taken.dirty);
    assert(plan.kind == PJ_DISPLAY_REFRESH_PARTIAL);
    assert(plan.deferred_cleanup);
    assert(pj_display_refresh_complete(
        &refresh, &panel, &panel_valid, &paused, &plan, 1,
        600000U, 580000U));
    pj_display_worker_model_complete_at(
        &worker, pause_slot, 1, first_deadline_ms + 1200U);
    assert(memcmp(&panel, &paused, sizeof(panel)) == 0);
    assert(refresh.cleanup_pending);
    assert(pj_display_worker_model_status(&worker).cleanup_pending);

    ui.state = PJ_UI_STATE_HOME;
    pj_ui_request_full_presentation(&ui);
    revision = pj_ui_presenter_revision(&ui);
    assert(pj_ui_presenter_prepare(
        &presenter, &ui, &revision, &frame) == PJ_UI_FRAME_FULL);
    pj_framebuffer_t home = *frame.framebuffer;
    request = request_from_presenter_frame(&frame);
    int home_slot;
    assert(pj_display_worker_model_begin_submit_request(
        &worker, &request, &home_slot));
    assert(pj_display_worker_model_commit_submit_request(
        &worker, home_slot, &request, NULL));
    assert(pj_ui_presenter_accept(&presenter, frame.token));
    assert(pj_display_worker_model_take_request_at(
        &worker, &home_slot, &taken, NULL,
        first_deadline_ms + 1200U));
    assert(!taken.dirty.partial);
    assert(taken.cleanup_state == PJ_DISPLAY_CLEANUP_SATISFY);
    pj_display_refresh_set_cleanup_deferred(&refresh, 0);
    plan = pj_display_refresh_plan(
        &refresh, &home, &panel, panel_valid, &taken.dirty);
    assert(plan.kind == PJ_DISPLAY_REFRESH_FULL);
    assert(pj_display_refresh_complete(
        &refresh, &panel, &panel_valid, &home, &plan, 1,
        1800000U, 1780000U));
    pj_display_worker_model_complete_at(
        &worker, home_slot, 1, first_deadline_ms + 3000U);
    assert(memcmp(&panel, &home, sizeof(panel)) == 0);
    assert(!refresh.cleanup_pending);
    assert(!pj_display_worker_model_status(&worker).cleanup_pending);
}

static void test_record_exit_cancels_ready_cadence_and_commits_home(void)
{
    pipeline_t pipeline;
    pipeline_init(&pipeline);
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_RECORD;
    pj_ui_set_audio_state(&ui, 1, 0);
    pj_ui_set_recording_elapsed(&ui, 0U);
    pj_ui_request_full_presentation(&ui);
    (void)pipeline_submit(
        &pipeline, &ui, pj_ui_interaction_generation(&ui), NULL);
    (void)pipeline_commit_next(&pipeline, NULL);
    assert(ui.record_state == PJ_RECORD_ACTIVE);
    assert(pj_display_worker_model_cadence_start(
        &pipeline.worker, 1U, 1000U));

    pj_ui_set_recording_elapsed(&ui, 1000U);
    pj_ui_presenter_revision_t revision =
        pj_ui_presenter_revision(&ui);
    pj_ui_presenter_frame_t frame;
    assert(pj_ui_presenter_prepare(
        &pipeline.presenter, &ui, &revision, &frame) ==
           PJ_UI_FRAME_PARTIAL);
    pj_display_worker_request_t request =
        request_from_presenter_frame(&frame);
    request.cadence_class = PJ_DISPLAY_CADENCE_SECONDS;
    request.cadence_sequence = 1U;
    request.cadence_deadline_ms = 1000U;
    int cadence_slot;
    assert(pj_display_worker_model_begin_submit_request(
        &pipeline.worker, &request, &cadence_slot));
    pipeline.frames[cadence_slot] = *frame.framebuffer;
    assert(pj_display_worker_model_commit_submit_request(
        &pipeline.worker, cadence_slot, &request, NULL));
    assert(pj_ui_presenter_accept(&pipeline.presenter, frame.token));
    assert(pipeline.worker.slots[cadence_slot].state ==
           PJ_DISPLAY_WORKER_SLOT_READY);

    assert(pj_ui_handle_aux_short(&ui));
    assert(ui.state == PJ_UI_STATE_HOME);
    assert(ui.record_state == PJ_RECORD_STOPPING);
    pj_display_worker_model_cadence_end(&pipeline.worker);
    assert(pipeline.worker.slots[cadence_slot].state ==
           PJ_DISPLAY_WORKER_SLOT_FREE);
    assert(pipeline.worker.cadence_handoff_pending);
    assert(pj_display_worker_model_status(&pipeline.worker)
               .cadence_misses == 0U);

    revision = pj_ui_presenter_revision(&ui);
    assert(pj_ui_presenter_prepare(
        &pipeline.presenter, &ui, &revision, &frame) ==
           PJ_UI_FRAME_FULL);
    pj_framebuffer_t home = *frame.framebuffer;
    request = request_from_presenter_frame(&frame);
    int home_slot;
    assert(pj_display_worker_model_begin_submit_request(
        &pipeline.worker, &request, &home_slot));
    pipeline.frames[home_slot] = home;
    assert(pj_display_worker_model_commit_submit_request(
        &pipeline.worker, home_slot, &request, NULL));
    assert(pj_ui_presenter_accept(&pipeline.presenter, frame.token));
    assert(!pipeline.worker.cadence_handoff_pending);
    assert(!pipeline.worker.slots[home_slot].dirty.partial);
    (void)pipeline_commit_next(&pipeline, NULL);
    assert(memcmp(&pipeline.panel, &home, sizeof(home)) == 0);
    pj_display_worker_status_t status =
        pj_display_worker_model_status(&pipeline.worker);
    assert(status.cadence_misses == 0U);
    assert(status.committed_interaction_generation ==
           revision.interaction_generation);
    assert(status.ordering_errors == 0U);
}

static void test_async_record_end_releases_cadence_before_home_submit(void)
{
    pipeline_t pipeline;
    pipeline_init(&pipeline);
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_RECORD;
    pj_ui_set_audio_state(&ui, 1, 0);
    pj_ui_request_full_presentation(&ui);
    (void)pipeline_submit(
        &pipeline, &ui, pj_ui_interaction_generation(&ui), NULL);
    (void)pipeline_commit_next(&pipeline, NULL);
    assert(pj_display_worker_model_cadence_start(
        &pipeline.worker, 1U, 1000U));

    /* Mirrors service_seconds_cadence's board projection before compose. */
    pj_ui_set_audio_state(&ui, 0, 0);
    assert(ui.state == PJ_UI_STATE_HOME);
    assert(ui.record_state == PJ_RECORD_IDLE);
    pj_display_worker_model_cadence_end(&pipeline.worker);

    pj_ui_presenter_revision_t revision =
        pj_ui_presenter_revision(&ui);
    pj_ui_presenter_frame_t frame;
    assert(pj_ui_presenter_prepare(
        &pipeline.presenter, &ui, &revision, &frame) == PJ_UI_FRAME_FULL);
    pj_framebuffer_t home = *frame.framebuffer;
    pj_display_worker_request_t request =
        request_from_presenter_frame(&frame);
    assert(request.cadence_class == PJ_DISPLAY_CADENCE_NONE);
    int home_slot;
    assert(pj_display_worker_model_begin_submit_request(
        &pipeline.worker, &request, &home_slot));
    pipeline.frames[home_slot] = home;
    assert(pj_display_worker_model_commit_submit_request(
        &pipeline.worker, home_slot, &request, NULL));
    assert(pj_ui_presenter_accept(&pipeline.presenter, frame.token));
    (void)pipeline_commit_next(&pipeline, NULL);
    assert(memcmp(&pipeline.panel, &home, sizeof(home)) == 0);
    pj_display_worker_status_t status =
        pj_display_worker_model_status(&pipeline.worker);
    assert(!status.cadence_active);
    assert(status.cadence_starts == 0U);
    assert(status.cadence_commits == 0U);
    assert(status.cadence_misses == 0U);
}

static void run_hard_cadence_screen(hard_cadence_screen_t screen)
{
    enum {
        INITIAL_FULL_LATENCY_US = 1800000,
        PARTIAL_LATENCY_US = 600000,
    };
    const uint64_t initial_completion_ms = 1800U;
    const uint64_t first_deadline_ms = initial_completion_ms +
        PJ_DISPLAY_WORKER_CADENCE_PERIOD_MS;

    pj_display_worker_model_t worker;
    pj_display_worker_model_init(&worker);
    pj_ui_presenter_t presenter;
    pj_ui_presenter_init(&presenter);
    pj_display_refresh_policy_t refresh;
    pj_display_refresh_policy_init(
        &refresh, PJ_DISPLAY_REFRESH_DEFAULT_PARTIAL_LIMIT);
    pj_ui_context_t ui;
    hard_cadence_init_ui(screen, &ui);

    pj_ui_presenter_revision_t revision =
        pj_ui_presenter_revision(&ui);
    pj_ui_presenter_frame_t frame;
    assert(pj_ui_presenter_prepare(
        &presenter, &ui, &revision, &frame) == PJ_UI_FRAME_FULL);
    pj_framebuffer_t candidate = *frame.framebuffer;
    pj_framebuffer_t fresh;
    pj_ui_compose_frame(&ui, &fresh);
    assert(memcmp(&candidate, &fresh, sizeof(candidate)) == 0);

    pj_display_worker_request_t request =
        request_from_presenter_frame(&frame);
    int slot = -1;
    uint32_t generation = 0;
    assert(pj_display_worker_model_begin_submit_request(
        &worker, &request, &slot));
    assert(pj_display_worker_model_commit_submit_request(
        &worker, slot, &request, &generation));
    assert(generation == 1U);
    assert(pj_ui_presenter_accept(&presenter, frame.token));

    pj_display_worker_request_t taken;
    uint32_t taken_generation = 0;
    assert(pj_display_worker_model_take_request_at(
        &worker, &slot, &taken, &taken_generation, 0U));
    assert(taken_generation == generation);
    assert(!taken.dirty.partial);
    pj_framebuffer_t panel = {0};
    int panel_valid = 0;
    pj_display_refresh_plan_t plan = pj_display_refresh_plan(
        &refresh, &candidate, &panel, panel_valid, &taken.dirty);
    assert(plan.kind == PJ_DISPLAY_REFRESH_FULL);
    assert(pj_display_refresh_complete(
        &refresh, &panel, &panel_valid, &candidate, &plan, 1,
        INITIAL_FULL_LATENCY_US, INITIAL_FULL_LATENCY_US - 20000));
    pj_display_worker_model_complete_at(
        &worker, slot, 1, initial_completion_ms);
    assert(panel_valid);
    assert(memcmp(&panel, &candidate, sizeof(panel)) == 0);
    assert(refresh.metrics.applied_full == 1U);

    pj_display_refresh_set_cleanup_deferred(&refresh, 1);
    assert(pj_display_worker_model_cadence_start(
        &worker, 1U, first_deadline_ms));

    for (uint32_t sequence = 1U; sequence <= 120U; sequence++) {
        pj_framebuffer_t before = panel;
        hard_cadence_advance_ui(screen, &ui, sequence);
        revision = pj_ui_presenter_revision(&ui);
        assert(pj_ui_presenter_prepare(
            &presenter, &ui, &revision, &frame) ==
            PJ_UI_FRAME_PARTIAL);
        candidate = *frame.framebuffer;
        pj_ui_compose_frame(&ui, &fresh);
        assert(memcmp(&candidate, &fresh, sizeof(candidate)) == 0);
        assert(memcmp(&before, &candidate, sizeof(candidate)) != 0);

        pj_ui_dirty_region_t exact = exact_delta_bounds(
            &before, &candidate);
        assert(frame.dirty.partial);
        assert(frame.dirty.x == exact.x && frame.dirty.y == exact.y);
        assert(frame.dirty.width == exact.width &&
               frame.dirty.height == exact.height);
        pj_framebuffer_t reconstructed = before;
        copy_region(&reconstructed, &candidate, &frame.dirty);
        assert(memcmp(&reconstructed, &candidate,
                      sizeof(reconstructed)) == 0);

        request = request_from_presenter_frame(&frame);
        uint64_t deadline_ms = first_deadline_ms +
            (uint64_t)(sequence - 1U) *
                PJ_DISPLAY_WORKER_CADENCE_PERIOD_MS;
        request.cadence_class = PJ_DISPLAY_CADENCE_SECONDS;
        request.cadence_sequence = sequence;
        request.cadence_deadline_ms = deadline_ms;
        request.cleanup_state = PJ_DISPLAY_CLEANUP_DEFERRED;
        slot = -1;
        generation = 0;
        assert(pj_display_worker_model_begin_submit_request(
            &worker, &request, &slot));
        assert(pj_display_worker_model_commit_submit_request(
            &worker, slot, &request, &generation));
        assert(generation == sequence + 1U);
        assert(pj_ui_presenter_accept(&presenter, frame.token));
        assert(worker.superseded_frames == 0U);

        int active_slot = -1;
        assert(!pj_display_worker_model_take_request_at(
            &worker, &active_slot, &taken, &taken_generation,
            deadline_ms - 1U));
        assert(pj_display_worker_model_take_request_at(
            &worker, &active_slot, &taken, &taken_generation,
            deadline_ms));
        assert(active_slot == slot);
        assert(taken_generation == generation);
        assert(taken.cadence_sequence == sequence);
        assert(taken.visual_revision == revision.visual_revision);
        assert(taken.interaction_generation ==
               revision.interaction_generation);

        plan = pj_display_refresh_plan(
            &refresh, &candidate, &panel, panel_valid, &taken.dirty);
        assert(plan.kind == PJ_DISPLAY_REFRESH_PARTIAL);
        assert(!plan.promoted_to_full);
        assert(plan.deferred_cleanup == (sequence >= 30U));
        assert(pj_display_refresh_complete(
            &refresh, &panel, &panel_valid, &candidate, &plan, 1,
            PARTIAL_LATENCY_US, PARTIAL_LATENCY_US - 20000));
        if (pj_display_refresh_cleanup_pending(&refresh)) {
            pj_display_worker_model_cleanup_due(&worker);
        }
        pj_display_worker_model_complete_at(
            &worker, active_slot, 1,
            deadline_ms + PARTIAL_LATENCY_US / 1000U);
        assert(memcmp(&panel, &candidate, sizeof(panel)) == 0);
        assert(refresh.metrics.applied_full == 1U);
        assert(refresh.metrics.applied_partial == sequence);
    }

    pj_display_worker_status_t status =
        pj_display_worker_model_status(&worker);
    assert(status.cadence_active);
    assert(status.cadence_starts == 120U);
    assert(status.cadence_commits == 120U);
    assert(status.cadence_last_started_sequence == 120U);
    assert(status.cadence_last_committed_sequence == 120U);
    assert(status.cadence_max_start_lateness_ms == 0U);
    assert(status.cadence_misses == 0U);
    assert(status.cadence_overruns == 0U);
    assert(status.superseded_frames == 0U);
    assert(status.ordering_errors == 0U);
    assert(status.cleanup_pending);
    assert(status.cleanup_deferred_frames == 1U);
    assert(refresh.metrics.cleanup_deferrals == 91U);
    assert(refresh.metrics.applied_full == 1U);
    assert(refresh.metrics.applied_partial == 120U);
    assert(refresh.metrics.max_latency_us == INITIAL_FULL_LATENCY_US);
    assert(!pj_display_worker_model_take_cleanup_pending(&worker));

    pj_display_worker_model_cadence_end(&worker);
    assert(pj_display_worker_model_take_cleanup_pending(&worker));
    assert(!pj_display_worker_model_take_cleanup_pending(&worker));

    pj_display_refresh_set_cleanup_deferred(&refresh, 0);
    hard_cadence_advance_ui(screen, &ui, 121U);
    revision = pj_ui_presenter_revision(&ui);
    assert(pj_ui_presenter_prepare(
        &presenter, &ui, &revision, &frame) == PJ_UI_FRAME_PARTIAL);
    candidate = *frame.framebuffer;
    plan = pj_display_refresh_plan(
        &refresh, &candidate, &panel, panel_valid, &frame.dirty);
    assert(plan.kind == PJ_DISPLAY_REFRESH_FULL);
    assert(plan.promoted_to_full);
    assert(pj_display_refresh_complete(
        &refresh, &panel, &panel_valid, &candidate, &plan, 1,
        INITIAL_FULL_LATENCY_US, INITIAL_FULL_LATENCY_US - 20000));
    assert(!pj_display_refresh_cleanup_pending(&refresh));
    assert(refresh.metrics.applied_full == 2U);
}

static void test_hard_cadence_all_seconds_screens(void)
{
    run_hard_cadence_screen(HARD_CADENCE_RECORD);
    run_hard_cadence_screen(HARD_CADENCE_STOPWATCH);
    run_hard_cadence_screen(HARD_CADENCE_TIMER);
    run_hard_cadence_screen(HARD_CADENCE_INTERVAL);
}

static void test_layout_churn_commits_latest_full_frame_in_order(void)
{
    pipeline_t pipeline;
    pipeline_init(&pipeline);
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    pj_ui_wake(&ui);
    (void)pipeline_submit(&pipeline, &ui, 10, NULL);
    (void)pipeline_commit_next(&pipeline, NULL);

    ui.state = PJ_UI_STATE_TIMER;
    pj_ui_request_full_presentation(&ui);
    (void)pipeline_submit(&pipeline, &ui, 11, NULL);
    ui.state = PJ_UI_STATE_INTERVAL;
    pj_ui_request_full_presentation(&ui);
    pj_framebuffer_t expected;
    uint32_t latest = pipeline_submit(&pipeline, &ui, 12, &expected);

    uint32_t epoch = 0;
    uint32_t committed = pipeline_commit_next(&pipeline, &epoch);
    assert(committed == latest && epoch == 12);
    assert(memcmp(&pipeline.panel, &expected, sizeof(expected)) == 0);
    assert(pipeline.worker.committed_interaction_generation == 12);
    assert(pipeline.worker.ordering_errors == 0);
}

static void test_note_page_identity_waits_for_page_commit(void)
{
    pipeline_t pipeline;
    pipeline_init(&pipeline);
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_LISTEN;
    const char labels[6][PJ_UI_NOTE_LABEL_LEN] = {
        "JUL 15 08:30", "JUL 15 08:31", "JUL 15 08:32",
        "JUL 15 08:33", "JUL 15 08:34", "JUL 15 08:35",
    };
    pj_ui_set_notes(&ui, 6, labels);
    uint32_t first_page = pj_ui_interaction_generation(&ui);
    (void)pipeline_submit(&pipeline, &ui, first_page, NULL);
    (void)pipeline_commit_next(&pipeline, NULL);

    assert(pj_ui_handle_touch(&ui, 150, 160,
                              PJ_TOUCH_TAP));
    uint32_t second_page = pj_ui_interaction_generation(&ui);
    assert(second_page != first_page);
    (void)pipeline_submit(&pipeline, &ui, second_page, NULL);

    assert(!guarded_touch(&pipeline, &ui, second_page, 101, 100, 20));
    assert(ui.state == PJ_UI_STATE_LISTEN && ui.selected_note == 0);
    (void)pipeline_commit_next(&pipeline, NULL);
    assert(guarded_touch(&pipeline, &ui, second_page, 201, 100, 20));
    assert(ui.state == PJ_UI_STATE_NOTE_DETAIL && ui.selected_note == 3);
}

static void test_same_layout_semantics_wait_for_commit(void)
{
    pipeline_t pipeline;
    pipeline_init(&pipeline);
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_TIMER;
    pj_ui_request_full_presentation(&ui);

    uint32_t original_epoch = pj_ui_interaction_generation(&ui);
    (void)pipeline_submit(&pipeline, &ui, original_epoch, NULL);
    (void)pipeline_commit_next(&pipeline, NULL);

    assert(guarded_touch(&pipeline, &ui, original_epoch, 101, 50, 120));
    pj_ui_time_command_t command;
    assert(pj_ui_consume_time_command(&ui, &command));
    assert(command.type == PJ_UI_TIME_COMMAND_TIMER_SET);
    assert(command.duration_ms == 90000U);
    uint32_t adjusted_epoch = pj_ui_interaction_generation(&ui);
    assert(adjusted_epoch != original_epoch);
    (void)pipeline_submit(&pipeline, &ui, adjusted_epoch, NULL);

    assert(!guarded_touch(&pipeline, &ui, adjusted_epoch, 102, 150, 120));
    assert(ui.time_command.type == PJ_UI_TIME_COMMAND_NONE);
    (void)pipeline_commit_next(&pipeline, NULL);
    assert(guarded_touch(&pipeline, &ui, adjusted_epoch, 201, 150, 120));
    assert(pj_ui_consume_time_command(&ui, &command));
    assert(command.type == PJ_UI_TIME_COMMAND_TIMER_START);
    assert(command.duration_ms == 90000U);
}

static void test_passive_ticks_keep_controls_live(void)
{
    pipeline_t pipeline;
    pipeline_init(&pipeline);
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_TIMER;
    pj_ui_time_projection_t projection = {
        .timer_running = 1,
        .timer_remaining_ms = 300000U,
    };
    pj_ui_set_time_projection(&ui, &projection);
    uint32_t running_epoch = pj_ui_interaction_generation(&ui);
    (void)pipeline_submit(&pipeline, &ui, running_epoch, NULL);
    (void)pipeline_commit_next(&pipeline, NULL);

    projection.timer_remaining_ms = 299000U;
    pj_ui_set_time_projection(&ui, &projection);
    assert(pj_ui_interaction_generation(&ui) == running_epoch);
    (void)pipeline_submit(&pipeline, &ui, running_epoch, NULL);

    assert(guarded_touch(&pipeline, &ui, running_epoch, 101, 150, 120));
    pj_ui_time_command_t command;
    assert(pj_ui_consume_time_command(&ui, &command));
    assert(command.type == PJ_UI_TIME_COMMAND_TIMER_PAUSE);
}

int main(void)
{
    test_presenter_reports_exact_partial_bounds();
    test_presenter_rejection_retains_lossless_snapshot();
    test_presenter_barrier_noop_and_full_policy();
    test_hidden_new_layout_controls_are_inert_until_commit();
    test_rapid_digit_updates_coalesce_to_exact_latest_frame();
    test_every_stopwatch_second_has_complete_dirty_coverage();
    test_cadence_cancel_preserves_pixels_and_defers_cleanup();
    test_record_exit_cancels_ready_cadence_and_commits_home();
    test_async_record_end_releases_cadence_before_home_submit();
    test_hard_cadence_all_seconds_screens();
    test_layout_churn_commits_latest_full_frame_in_order();
    test_note_page_identity_waits_for_page_commit();
    test_same_layout_semantics_wait_for_commit();
    test_passive_ticks_keep_controls_live();
    puts("display pipeline tests passed");
    return 0;
}
