#include "pj_display_refresh.h"
#include "pj_display_worker.h"
#include "pj_ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    pj_display_worker_model_t worker;
    pj_framebuffer_t frames[PJ_DISPLAY_WORKER_SLOT_COUNT];
    pj_framebuffer_t panel;
    pj_display_refresh_policy_t refresh;
    int panel_valid;
} pipeline_t;

static void pipeline_init(pipeline_t *pipeline)
{
    memset(pipeline, 0, sizeof(*pipeline));
    pj_display_worker_model_init(&pipeline->worker);
    pj_display_refresh_policy_init(
        &pipeline->refresh, PJ_DISPLAY_REFRESH_DEFAULT_PARTIAL_LIMIT);
}

static uint32_t pipeline_submit(pipeline_t *pipeline, pj_ui_context_t *ui,
                                uint32_t scene_epoch,
                                pj_framebuffer_t *rendered)
{
    pj_framebuffer_t frame;
    pj_ui_render(ui, &frame);
    pj_ui_dirty_region_t dirty = pj_ui_dirty_region(ui);
    int slot = -1;
    assert(pj_display_worker_model_begin_submit(&pipeline->worker, &slot));
    pipeline->frames[slot] = frame;
    uint32_t generation = 0;
    assert(pj_display_worker_model_commit_submit(
        &pipeline->worker, slot, &dirty, scene_epoch, &generation));
    pj_ui_mark_displayed(ui);
    if (rendered != NULL) {
        *rendered = frame;
    }
    return generation;
}

static uint32_t pipeline_commit_next(pipeline_t *pipeline,
                                     uint32_t *scene_epoch)
{
    int slot = -1;
    pj_ui_dirty_region_t dirty;
    uint32_t generation = 0;
    uint32_t epoch = 0;
    assert(pj_display_worker_model_take(
        &pipeline->worker, &slot, &dirty, &generation, &epoch));
    pj_display_refresh_plan_t plan = pj_display_refresh_plan(
        &pipeline->refresh, &pipeline->frames[slot], &pipeline->panel,
        pipeline->panel_valid, &dirty);
    assert(plan.kind != PJ_DISPLAY_REFRESH_NOOP ||
           memcmp(&pipeline->frames[slot], &pipeline->panel,
                  sizeof(pipeline->panel)) == 0);
    assert(pj_display_refresh_complete(
        &pipeline->refresh, &pipeline->panel, &pipeline->panel_valid,
        &pipeline->frames[slot], &plan, 1, 600000, 580000));
    pj_display_worker_model_complete(&pipeline->worker, slot, 1);
    assert(memcmp(&pipeline->frames[slot], &pipeline->panel,
                  sizeof(pipeline->panel)) == 0);
    if (scene_epoch != NULL) {
        *scene_epoch = epoch;
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

static int guarded_touch(pipeline_t *pipeline, pj_ui_context_t *ui,
                         uint32_t scene_epoch, int x, int y)
{
    if (!pj_display_worker_model_scene_presented(
            &pipeline->worker, scene_epoch)) {
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

static void test_hidden_new_scene_controls_are_inert_until_commit(void)
{
    pipeline_t pipeline;
    pipeline_init(&pipeline);
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    pj_ui_wake(&ui);

    (void)pipeline_submit(&pipeline, &ui, 1, NULL);
    (void)pipeline_commit_next(&pipeline, NULL);
    assert(pj_display_worker_model_scene_presented(&pipeline.worker, 1));

    ui.state = PJ_UI_STATE_TIME;
    pj_ui_request_full_refresh(&ui);
    (void)pipeline_submit(&pipeline, &ui, 2, NULL);
    assert(!pj_display_worker_model_scene_presented(&pipeline.worker, 2));

    assert(!guarded_touch(&pipeline, &ui, 2, 50, 150));
    assert(ui.state == PJ_UI_STATE_TIME);
    assert(pipeline.worker.input_deferred_events == 1);

    uint32_t committed_epoch = 0;
    (void)pipeline_commit_next(&pipeline, &committed_epoch);
    assert(committed_epoch == 2);
    assert(guarded_touch(&pipeline, &ui, 2, 50, 150));
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
    pj_ui_request_full_refresh(&ui);
    pj_framebuffer_t previous;
    (void)pipeline_submit(&pipeline, &ui, 3, &previous);
    (void)pipeline_commit_next(&pipeline, NULL);

    const int values[] = {9, 10, 19, 20, 40, 41, 42, 59, 60};
    pj_framebuffer_t latest = previous;
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        pj_ui_time_projection_t projection = stopwatch_projection(values[i]);
        pj_ui_set_time_projection(&ui, &projection);
        pj_ui_dirty_region_t dirty = pj_ui_dirty_region(&ui);
        pj_framebuffer_t rendered;
        pj_ui_render(&ui, &rendered);
        assert_frame_delta_covered(&previous, &rendered, &dirty);
        (void)pipeline_submit(&pipeline, &ui, 3, &latest);
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
    pj_ui_request_full_refresh(&ui);
    pj_framebuffer_t previous;
    (void)pipeline_submit(&pipeline, &ui, 4, &previous);
    (void)pipeline_commit_next(&pipeline, NULL);

    for (int second = 1; second <= 120; second++) {
        pj_ui_time_projection_t projection = stopwatch_projection(second);
        pj_ui_set_time_projection(&ui, &projection);
        pj_ui_dirty_region_t dirty = pj_ui_dirty_region(&ui);
        pj_framebuffer_t rendered;
        pj_ui_render(&ui, &rendered);
        assert_frame_delta_covered(&previous, &rendered, &dirty);
        for (int y = PJ_DISPLAY_HEIGHT / 2; y < PJ_DISPLAY_HEIGHT; y++) {
            for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
                assert(pj_framebuffer_get(&previous, x, y) ==
                       pj_framebuffer_get(&rendered, x, y));
            }
        }
        (void)pipeline_submit(&pipeline, &ui, 4, NULL);
        (void)pipeline_commit_next(&pipeline, NULL);
        previous = rendered;
    }
    assert(pipeline.worker.committed_generation == 121);
    assert(pipeline.worker.ordering_errors == 0);
}

static void test_scene_churn_commits_latest_full_frame_in_order(void)
{
    pipeline_t pipeline;
    pipeline_init(&pipeline);
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    pj_ui_wake(&ui);
    (void)pipeline_submit(&pipeline, &ui, 10, NULL);
    (void)pipeline_commit_next(&pipeline, NULL);

    ui.state = PJ_UI_STATE_TIMER;
    pj_ui_request_full_refresh(&ui);
    (void)pipeline_submit(&pipeline, &ui, 11, NULL);
    ui.state = PJ_UI_STATE_INTERVAL;
    pj_ui_request_full_refresh(&ui);
    pj_framebuffer_t expected;
    uint32_t latest = pipeline_submit(&pipeline, &ui, 12, &expected);

    uint32_t epoch = 0;
    uint32_t committed = pipeline_commit_next(&pipeline, &epoch);
    assert(committed == latest && epoch == 12);
    assert(memcmp(&pipeline.panel, &expected, sizeof(expected)) == 0);
    assert(pipeline.worker.committed_scene_epoch == 12);
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

    assert(!guarded_touch(&pipeline, &ui, second_page, 100, 20));
    assert(ui.state == PJ_UI_STATE_LISTEN && ui.selected_note == 0);
    (void)pipeline_commit_next(&pipeline, NULL);
    assert(guarded_touch(&pipeline, &ui, second_page, 100, 20));
    assert(ui.state == PJ_UI_STATE_NOTE_DETAIL && ui.selected_note == 3);
}

int main(void)
{
    test_hidden_new_scene_controls_are_inert_until_commit();
    test_rapid_digit_updates_coalesce_to_exact_latest_frame();
    test_every_stopwatch_second_has_complete_dirty_coverage();
    test_scene_churn_commits_latest_full_frame_in_order();
    test_note_page_identity_waits_for_page_commit();
    puts("display pipeline tests passed");
    return 0;
}
