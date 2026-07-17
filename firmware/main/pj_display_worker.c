#include "pj_display_worker.h"

#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

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

static pj_ui_dirty_region_t empty_region(void)
{
    return (pj_ui_dirty_region_t) {0};
}

static void increment_saturated(uint32_t *value)
{
    if (*value < UINT32_MAX) {
        (*value)++;
    }
}

static uint32_t next_nonzero_sequence(uint32_t sequence)
{
    sequence++;
    return sequence == 0 ? 1 : sequence;
}

static int generation_is_newer(uint32_t candidate, uint32_t reference)
{
    return candidate != reference &&
        (reference == 0 || (int32_t)(candidate - reference) > 0);
}

static int normalize_region(const pj_ui_dirty_region_t *input,
                            pj_ui_dirty_region_t *output)
{
    if (input == NULL || output == NULL ||
        input->width <= 0 || input->height <= 0) {
        return 0;
    }
    if (!input->partial) {
        *output = full_region();
        return 1;
    }

    int64_t x0 = input->x;
    int64_t y0 = input->y;
    int64_t x1 = x0 + input->width;
    int64_t y1 = y0 + input->height;
    if (x1 <= 0 || y1 <= 0 ||
        x0 >= PJ_DISPLAY_WIDTH || y0 >= PJ_DISPLAY_HEIGHT) {
        return 0;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > PJ_DISPLAY_WIDTH) x1 = PJ_DISPLAY_WIDTH;
    if (y1 > PJ_DISPLAY_HEIGHT) y1 = PJ_DISPLAY_HEIGHT;
    if (x1 <= x0 || y1 <= y0) {
        return 0;
    }
    *output = (pj_ui_dirty_region_t) {
        .x = (int)x0,
        .y = (int)y0,
        .width = (int)(x1 - x0),
        .height = (int)(y1 - y0),
        .partial = 1,
    };
    return 1;
}

static pj_ui_dirty_region_t merge_regions(const pj_ui_dirty_region_t *left,
                                           const pj_ui_dirty_region_t *right)
{
    pj_ui_dirty_region_t a;
    pj_ui_dirty_region_t b;
    if (!normalize_region(left, &a)) {
        return normalize_region(right, &b) ? b : full_region();
    }
    if (!normalize_region(right, &b)) {
        return a;
    }
    if (!a.partial || !b.partial) {
        return full_region();
    }
    int x0 = a.x < b.x ? a.x : b.x;
    int y0 = a.y < b.y ? a.y : b.y;
    int a_x1 = a.x + a.width;
    int b_x1 = b.x + b.width;
    int a_y1 = a.y + a.height;
    int b_y1 = b.y + b.height;
    int x1 = a_x1 > b_x1 ? a_x1 : b_x1;
    int y1 = a_y1 > b_y1 ? a_y1 : b_y1;
    return (pj_ui_dirty_region_t) {
        .x = x0,
        .y = y0,
        .width = x1 - x0,
        .height = y1 - y0,
        .partial = 1,
    };
}

void pj_display_worker_request_init(pj_display_worker_request_t *request,
                                    const pj_ui_dirty_region_t *dirty)
{
    if (request == NULL) {
        return;
    }
    memset(request, 0, sizeof(*request));
    if (dirty != NULL) {
        request->dirty = *dirty;
    }
}

static int normalize_request(const pj_display_worker_request_t *input,
                             pj_display_worker_request_t *output)
{
    if (input == NULL || output == NULL ||
        input->cadence_class < PJ_DISPLAY_CADENCE_NONE ||
        input->cadence_class > PJ_DISPLAY_CADENCE_SECONDS ||
        input->cleanup_state < PJ_DISPLAY_CLEANUP_NONE ||
        input->cleanup_state > PJ_DISPLAY_CLEANUP_PENDING) {
        return 0;
    }
    *output = *input;
    if (input->barrier) {
        output->dirty = empty_region();
    } else if (!normalize_region(&input->dirty, &output->dirty)) {
        return 0;
    }
    if (input->cadence_class == PJ_DISPLAY_CADENCE_SECONDS &&
        (input->cadence_sequence == 0 ||
         input->cadence_deadline_ms == 0)) {
        return 0;
    }
    return 1;
}

static void request_from_slot(const pj_display_worker_slot_t *slot,
                              pj_display_worker_request_t *request)
{
    *request = (pj_display_worker_request_t) {
        .dirty = slot->dirty,
        .layout_epoch = slot->layout_epoch,
        .interaction_generation = slot->interaction_generation,
        .visual_revision = slot->visual_revision,
        .full_refresh_revision = slot->full_refresh_revision,
        .cadence_class = slot->cadence_class,
        .cadence_sequence = slot->cadence_sequence,
        .cadence_deadline_ms = slot->cadence_deadline_ms,
        .cleanup_state = slot->cleanup_state,
        .barrier = slot->barrier,
    };
}

static void request_to_slot(pj_display_worker_slot_t *slot,
                            const pj_display_worker_request_t *request)
{
    slot->dirty = request->dirty;
    slot->layout_epoch = request->layout_epoch;
    slot->interaction_generation = request->interaction_generation;
    slot->visual_revision = request->visual_revision;
    slot->full_refresh_revision = request->full_refresh_revision;
    slot->cadence_class = request->cadence_class;
    slot->cadence_sequence = request->cadence_sequence;
    slot->cadence_deadline_ms = request->cadence_deadline_ms;
    slot->cleanup_state = request->cleanup_state;
    slot->barrier = request->barrier;
}

static void clear_slot(pj_display_worker_slot_t *slot)
{
    memset(slot, 0, sizeof(*slot));
}

static int latest_ready_ordinary_slot(
    const pj_display_worker_model_t *model)
{
    int latest = -1;
    for (int i = 0; i < PJ_DISPLAY_WORKER_SLOT_COUNT; i++) {
        if (model->slots[i].state != PJ_DISPLAY_WORKER_SLOT_READY ||
            model->slots[i].cadence_class != PJ_DISPLAY_CADENCE_NONE) {
            continue;
        }
        if (latest < 0 || generation_is_newer(
                model->slots[i].generation,
                model->slots[latest].generation)) {
            latest = i;
        }
    }
    return latest;
}

static int ready_cadence_slot(const pj_display_worker_model_t *model)
{
    for (int i = 0; i < PJ_DISPLAY_WORKER_SLOT_COUNT; i++) {
        if (model->slots[i].state == PJ_DISPLAY_WORKER_SLOT_READY &&
            model->slots[i].cadence_class == PJ_DISPLAY_CADENCE_SECONDS) {
            return i;
        }
    }
    return -1;
}

static int displaying_slot(const pj_display_worker_model_t *model)
{
    for (int i = 0; i < PJ_DISPLAY_WORKER_SLOT_COUNT; i++) {
        if (model->slots[i].state == PJ_DISPLAY_WORKER_SLOT_DISPLAYING) {
            return i;
        }
    }
    return -1;
}

static uint32_t latest_accepted_layout_epoch(
    const pj_display_worker_model_t *model, int excluded_slot)
{
    uint32_t generation = model->committed_generation;
    uint32_t layout_epoch = model->committed_layout_epoch;
    for (int i = 0; i < PJ_DISPLAY_WORKER_SLOT_COUNT; i++) {
        if (i == excluded_slot || model->slots[i].generation == 0 ||
            (model->slots[i].state != PJ_DISPLAY_WORKER_SLOT_READY &&
             model->slots[i].state != PJ_DISPLAY_WORKER_SLOT_DISPLAYING)) {
            continue;
        }
        if (generation == 0 || generation_is_newer(
                model->slots[i].generation, generation)) {
            generation = model->slots[i].generation;
            layout_epoch = model->slots[i].layout_epoch;
        }
    }
    return layout_epoch;
}

static void coalesce_ready_slots(pj_display_worker_model_t *model, int latest)
{
    if (latest < 0) {
        return;
    }
    for (int i = 0; i < PJ_DISPLAY_WORKER_SLOT_COUNT; i++) {
        if (i == latest ||
            model->slots[i].state != PJ_DISPLAY_WORKER_SLOT_READY ||
            model->slots[i].cadence_class != PJ_DISPLAY_CADENCE_NONE) {
            continue;
        }
        if (model->slots[latest].layout_epoch ==
            model->slots[i].layout_epoch) {
            model->slots[latest].dirty = merge_regions(
                &model->slots[latest].dirty, &model->slots[i].dirty);
        } else {
            model->slots[latest].dirty = full_region();
        }
        if (model->slots[i].barrier &&
            model->slots[latest].dirty.width <= 0) {
            model->slots[latest].barrier = 1;
        } else if (model->slots[latest].dirty.width > 0) {
            model->slots[latest].barrier = 0;
        }
        increment_saturated(&model->superseded_frames);
        clear_slot(&model->slots[i]);
    }
}

void pj_display_worker_model_init(pj_display_worker_model_t *model)
{
    if (model == NULL) {
        return;
    }
    memset(model, 0, sizeof(*model));
    model->next_generation = 1;
    model->accepting = 1;
}

int pj_display_worker_model_begin_submit_request(
    pj_display_worker_model_t *model,
    const pj_display_worker_request_t *request, int *slot_index)
{
    pj_display_worker_request_t normalized;
    if (model == NULL || slot_index == NULL || !model->accepting ||
        !normalize_request(request, &normalized)) {
        return 0;
    }

    int slot = -1;
    if (normalized.cadence_class == PJ_DISPLAY_CADENCE_SECONDS) {
        if (!model->cadence_active ||
            normalized.cadence_sequence !=
                model->cadence_expected_submit_sequence ||
            normalized.cadence_deadline_ms !=
                model->cadence_expected_deadline_ms ||
            ready_cadence_slot(model) >= 0) {
            increment_saturated(&model->cadence_misses);
            return 0;
        }
    } else {
        if (model->cadence_active) {
            return 0;
        }
        slot = latest_ready_ordinary_slot(model);
        coalesce_ready_slots(model, slot);
    }
    if (slot < 0) {
        for (int i = 0; i < PJ_DISPLAY_WORKER_SLOT_COUNT; i++) {
            if (model->slots[i].state == PJ_DISPLAY_WORKER_SLOT_FREE) {
                slot = i;
                clear_slot(&model->slots[i]);
                break;
            }
        }
    }
    if (slot < 0) {
        return 0;
    }
    model->slots[slot].state = PJ_DISPLAY_WORKER_SLOT_WRITING;
    *slot_index = slot;
    return 1;
}

int pj_display_worker_model_commit_submit_request(
    pj_display_worker_model_t *model, int slot_index,
    const pj_display_worker_request_t *request, uint32_t *generation)
{
    pj_display_worker_request_t normalized;
    if (model == NULL || slot_index < 0 ||
        slot_index >= PJ_DISPLAY_WORKER_SLOT_COUNT ||
        model->slots[slot_index].state != PJ_DISPLAY_WORKER_SLOT_WRITING) {
        return 0;
    }
    if (!model->accepting || !normalize_request(request, &normalized)) {
        clear_slot(&model->slots[slot_index]);
        return 0;
    }

    if (normalized.cadence_class == PJ_DISPLAY_CADENCE_SECONDS &&
        (!model->cadence_active ||
         normalized.cadence_sequence !=
             model->cadence_expected_submit_sequence ||
         normalized.cadence_deadline_ms !=
             model->cadence_expected_deadline_ms)) {
        increment_saturated(&model->cadence_misses);
        clear_slot(&model->slots[slot_index]);
        return 0;
    }

    pj_display_worker_slot_t *slot = &model->slots[slot_index];
    uint32_t replaced_generation = slot->generation;
    uint32_t replaced_layout_epoch = slot->layout_epoch;
    uint32_t baseline_layout_epoch = replaced_generation != 0 ?
        replaced_layout_epoch : latest_accepted_layout_epoch(
            model, slot_index);
    pj_ui_dirty_region_t replaced_dirty = slot->dirty;
    int replaced_barrier = slot->barrier;
    if (model->force_full_on_commit) {
        normalized.dirty = full_region();
        normalized.barrier = 0;
        model->force_full_on_commit = 0;
    } else if (baseline_layout_epoch != 0 &&
               baseline_layout_epoch != normalized.layout_epoch) {
        normalized.dirty = full_region();
        normalized.barrier = 0;
    } else if (replaced_generation != 0 && !replaced_barrier &&
               replaced_dirty.width > 0 && replaced_dirty.height > 0) {
        if (normalized.barrier) {
            normalized.dirty = replaced_dirty;
        } else {
            normalized.dirty = merge_regions(
                &replaced_dirty, &normalized.dirty);
        }
        normalized.barrier = 0;
    }

    request_to_slot(slot, &normalized);
    slot->generation = model->next_generation++;
    if (model->next_generation == 0) {
        model->next_generation = 1;
    }
    slot->state = PJ_DISPLAY_WORKER_SLOT_READY;
    model->accepted_generation = slot->generation;
    if (replaced_generation != 0) {
        increment_saturated(&model->superseded_frames);
    }
    if (normalized.cadence_class == PJ_DISPLAY_CADENCE_SECONDS) {
        model->cadence_expected_submit_sequence = next_nonzero_sequence(
            model->cadence_expected_submit_sequence);
        model->cadence_expected_deadline_ms +=
            PJ_DISPLAY_WORKER_CADENCE_PERIOD_MS;
    }
    if (normalized.cleanup_state == PJ_DISPLAY_CLEANUP_PENDING) {
        model->cleanup_pending = 1;
    }
    if (generation != NULL) {
        *generation = slot->generation;
    }
    return 1;
}

int pj_display_worker_model_peek_request(
    pj_display_worker_model_t *model, pj_display_worker_request_t *request,
    uint32_t *generation)
{
    if (model == NULL || request == NULL) {
        return 0;
    }
    if (displaying_slot(model) >= 0) {
        return 0;
    }
    int slot = ready_cadence_slot(model);
    if (slot < 0) {
        slot = latest_ready_ordinary_slot(model);
    }
    if (slot < 0) {
        return 0;
    }
    coalesce_ready_slots(model, slot);
    request_from_slot(&model->slots[slot], request);
    if (generation != NULL) {
        *generation = model->slots[slot].generation;
    }
    return 1;
}

int pj_display_worker_model_take_request_at(
    pj_display_worker_model_t *model, int *slot_index,
    pj_display_worker_request_t *request, uint32_t *generation,
    uint64_t started_at_ms)
{
    if (model == NULL || slot_index == NULL || request == NULL) {
        return 0;
    }
    if (displaying_slot(model) >= 0) {
        return 0;
    }

    int slot = ready_cadence_slot(model);
    if (slot < 0) {
        slot = latest_ready_ordinary_slot(model);
    }
    if (slot < 0) {
        return 0;
    }
    coalesce_ready_slots(model, slot);
    if (model->slots[slot].cadence_class == PJ_DISPLAY_CADENCE_SECONDS &&
        started_at_ms < model->slots[slot].cadence_deadline_ms) {
        return 0;
    }
    model->slots[slot].state = PJ_DISPLAY_WORKER_SLOT_DISPLAYING;
    *slot_index = slot;
    request_from_slot(&model->slots[slot], request);
    if (generation != NULL) {
        *generation = model->slots[slot].generation;
    }
    if (model->slots[slot].generation != model->started_generation &&
        !generation_is_newer(model->slots[slot].generation,
                             model->started_generation)) {
        if (model->ordering_errors < UINT32_MAX) {
            model->ordering_errors++;
        }
    }
    model->started_generation = model->slots[slot].generation;
    model->started_layout_epoch = model->slots[slot].layout_epoch;
    model->started_interaction_generation =
        model->slots[slot].interaction_generation;

    if (model->slots[slot].cadence_class == PJ_DISPLAY_CADENCE_SECONDS) {
        increment_saturated(&model->cadence_starts);
        if (model->slots[slot].cadence_sequence !=
            model->cadence_expected_commit_sequence) {
            increment_saturated(&model->cadence_misses);
            model->slots[slot].cadence_fault_recorded = 1;
        }
        uint64_t lateness = started_at_ms -
            model->slots[slot].cadence_deadline_ms;
        uint32_t bounded = lateness > UINT32_MAX ?
            UINT32_MAX : (uint32_t)lateness;
        if (bounded > model->cadence_max_start_lateness_ms) {
            model->cadence_max_start_lateness_ms = bounded;
        }
        if (lateness > PJ_DISPLAY_WORKER_CADENCE_START_TOLERANCE_MS &&
            !model->slots[slot].cadence_fault_recorded) {
            increment_saturated(&model->cadence_misses);
            model->slots[slot].cadence_fault_recorded = 1;
        }
        model->cadence_last_started_sequence =
            model->slots[slot].cadence_sequence;
    }
    return 1;
}

void pj_display_worker_model_complete_at(pj_display_worker_model_t *model,
                                         int slot_index, int success,
                                         uint64_t committed_at_ms)
{
    if (model == NULL || slot_index < 0 ||
        slot_index >= PJ_DISPLAY_WORKER_SLOT_COUNT ||
        model->slots[slot_index].state != PJ_DISPLAY_WORKER_SLOT_DISPLAYING) {
        return;
    }
    if (success) {
        if (!generation_is_newer(model->slots[slot_index].generation,
                                 model->committed_generation)) {
            if (model->ordering_errors < UINT32_MAX) {
                model->ordering_errors++;
            }
        } else {
            uint32_t previous_interaction =
                model->committed_interaction_generation;
            model->committed_generation =
                model->slots[slot_index].generation;
            model->committed_layout_epoch =
                model->slots[slot_index].layout_epoch;
            model->committed_interaction_generation =
                model->slots[slot_index].interaction_generation;
            model->committed_visual_revision =
                model->slots[slot_index].visual_revision;
            model->committed_full_refresh_revision =
                model->slots[slot_index].full_refresh_revision;
            if (model->committed_interaction_generation !=
                previous_interaction) {
                model->committed_interaction_started_ms = committed_at_ms;
            }
        }
        if (model->slots[slot_index].cadence_class ==
            PJ_DISPLAY_CADENCE_SECONDS) {
            increment_saturated(&model->cadence_commits);
            if (model->slots[slot_index].cadence_sequence ==
                model->cadence_expected_commit_sequence) {
                model->cadence_expected_commit_sequence =
                    next_nonzero_sequence(
                        model->cadence_expected_commit_sequence);
            } else if (!model->slots[slot_index].cadence_fault_recorded) {
                increment_saturated(&model->cadence_misses);
                model->slots[slot_index].cadence_fault_recorded = 1;
            }
            uint64_t next_deadline =
                model->slots[slot_index].cadence_deadline_ms +
                PJ_DISPLAY_WORKER_CADENCE_PERIOD_MS;
            if (committed_at_ms >= next_deadline) {
                increment_saturated(&model->cadence_overruns);
                if (!model->slots[slot_index].cadence_fault_recorded) {
                    increment_saturated(&model->cadence_misses);
                    model->slots[slot_index].cadence_fault_recorded = 1;
                }
            }
            model->cadence_last_committed_sequence =
                model->slots[slot_index].cadence_sequence;
        }
        if (model->slots[slot_index].cleanup_state ==
            PJ_DISPLAY_CLEANUP_PENDING) {
            model->cleanup_pending = 1;
        }
        clear_slot(&model->slots[slot_index]);
        return;
    }
    if (model->slots[slot_index].cadence_class ==
            PJ_DISPLAY_CADENCE_SECONDS &&
        !model->slots[slot_index].cadence_fault_recorded) {
        increment_saturated(&model->cadence_misses);
        model->slots[slot_index].cadence_fault_recorded = 1;
    }
    if (!model->accepting) {
        clear_slot(&model->slots[slot_index]);
        return;
    }

    int ready = latest_ready_ordinary_slot(model);
    if (ready >= 0) {
        coalesce_ready_slots(model, ready);
        model->slots[ready].dirty = full_region();
        model->slots[ready].barrier = 0;
        clear_slot(&model->slots[slot_index]);
        return;
    }
    for (int i = 0; i < PJ_DISPLAY_WORKER_SLOT_COUNT; i++) {
        if (model->slots[i].state == PJ_DISPLAY_WORKER_SLOT_WRITING) {
            if (model->slots[slot_index].cadence_class ==
                    PJ_DISPLAY_CADENCE_SECONDS &&
                model->slots[slot_index].cadence_sequence ==
                    model->cadence_expected_commit_sequence) {
                model->cadence_expected_commit_sequence =
                    next_nonzero_sequence(
                        model->cadence_expected_commit_sequence);
            }
            model->force_full_on_commit = 1;
            clear_slot(&model->slots[slot_index]);
            return;
        }
    }

    model->slots[slot_index].dirty = full_region();
    model->slots[slot_index].barrier = 0;
    model->slots[slot_index].state = PJ_DISPLAY_WORKER_SLOT_READY;
}

void pj_display_worker_model_complete(pj_display_worker_model_t *model,
                                      int slot_index,
                                      int success)
{
    pj_display_worker_model_complete_at(model, slot_index, success, 0U);
}

void pj_display_worker_model_shutdown(pj_display_worker_model_t *model)
{
    if (model == NULL) {
        return;
    }
    model->accepting = 0;
    model->force_full_on_commit = 0;
    for (int i = 0; i < PJ_DISPLAY_WORKER_SLOT_COUNT; i++) {
        if (model->slots[i].state != PJ_DISPLAY_WORKER_SLOT_DISPLAYING) {
            clear_slot(&model->slots[i]);
        }
    }
}

int pj_display_worker_model_is_idle(const pj_display_worker_model_t *model)
{
    if (model == NULL) {
        return 1;
    }
    for (int i = 0; i < PJ_DISPLAY_WORKER_SLOT_COUNT; i++) {
        if (model->slots[i].state != PJ_DISPLAY_WORKER_SLOT_FREE) {
            return 0;
        }
    }
    return 1;
}

pj_display_worker_status_t pj_display_worker_model_status(
    const pj_display_worker_model_t *model)
{
    if (model == NULL) {
        return (pj_display_worker_status_t) {0};
    }
    return (pj_display_worker_status_t) {
        .accepted_generation = model->accepted_generation,
        .started_generation = model->started_generation,
        .started_layout_epoch = model->started_layout_epoch,
        .started_interaction_generation =
            model->started_interaction_generation,
        .committed_generation = model->committed_generation,
        .committed_layout_epoch = model->committed_layout_epoch,
        .committed_interaction_generation =
            model->committed_interaction_generation,
        .committed_visual_revision = model->committed_visual_revision,
        .committed_full_refresh_revision =
            model->committed_full_refresh_revision,
        .committed_interaction_started_ms =
            model->committed_interaction_started_ms,
        .superseded_frames = model->superseded_frames,
        .rate_deferred_frames = model->rate_deferred_frames,
        .input_deferred_events = model->input_deferred_events,
        .ordering_errors = model->ordering_errors,
        .cadence_starts = model->cadence_starts,
        .cadence_commits = model->cadence_commits,
        .cadence_max_start_lateness_ms =
            model->cadence_max_start_lateness_ms,
        .cadence_overruns = model->cadence_overruns,
        .cadence_misses = model->cadence_misses,
        .cadence_last_started_sequence =
            model->cadence_last_started_sequence,
        .cadence_last_committed_sequence =
            model->cadence_last_committed_sequence,
        .cleanup_deferred_frames = model->cleanup_deferred_frames,
        .cadence_active = model->cadence_active,
        .cleanup_pending = model->cleanup_pending,
    };
}

int pj_display_worker_status_accepts_interaction(
    const pj_display_worker_status_t *status,
    uint32_t interaction_generation, uint64_t captured_at_ms)
{
    return status != NULL && interaction_generation != 0U &&
        status->committed_interaction_generation ==
            interaction_generation &&
        captured_at_ms > status->committed_interaction_started_ms;
}

int pj_display_worker_model_cadence_start(
    pj_display_worker_model_t *model, uint32_t first_sequence,
    uint64_t first_deadline_ms)
{
    if (model == NULL || !model->accepting || model->cadence_active ||
        first_sequence == 0 || first_deadline_ms == 0 ||
        !pj_display_worker_model_is_idle(model)) {
        return 0;
    }
    model->cadence_active = 1;
    model->cadence_expected_submit_sequence = first_sequence;
    model->cadence_expected_commit_sequence = first_sequence;
    model->cadence_expected_deadline_ms = first_deadline_ms;
    model->cadence_last_started_sequence = 0;
    model->cadence_last_committed_sequence = 0;
    return 1;
}

void pj_display_worker_model_cadence_end(pj_display_worker_model_t *model)
{
    if (model == NULL || !model->cadence_active) {
        return;
    }
    model->cadence_active = 0;
    for (int i = 0; i < PJ_DISPLAY_WORKER_SLOT_COUNT; i++) {
        if (model->slots[i].state == PJ_DISPLAY_WORKER_SLOT_READY &&
            model->slots[i].cadence_class == PJ_DISPLAY_CADENCE_SECONDS) {
            if (!model->slots[i].cadence_fault_recorded) {
                increment_saturated(&model->cadence_misses);
            }
            clear_slot(&model->slots[i]);
        }
    }
}

void pj_display_worker_model_cleanup_due(pj_display_worker_model_t *model)
{
    if (model == NULL) {
        return;
    }
    if (!model->cleanup_pending && model->cadence_active) {
        increment_saturated(&model->cleanup_deferred_frames);
    }
    model->cleanup_pending = 1;
}

int pj_display_worker_model_take_cleanup_pending(
    pj_display_worker_model_t *model)
{
    if (model == NULL || model->cadence_active ||
        !model->cleanup_pending) {
        return 0;
    }
    model->cleanup_pending = 0;
    return 1;
}

void pj_display_worker_model_note_rate_deferred(
    pj_display_worker_model_t *model)
{
    if (model != NULL && model->rate_deferred_frames < UINT32_MAX) {
        model->rate_deferred_frames++;
    }
}

void pj_display_worker_model_note_input_deferred(
    pj_display_worker_model_t *model)
{
    if (model != NULL && model->input_deferred_events < UINT32_MAX) {
        model->input_deferred_events++;
    }
}

void pj_display_worker_model_note_cadence_fault(
    pj_display_worker_model_t *model)
{
    if (model != NULL) {
        increment_saturated(&model->cadence_misses);
    }
}

void pj_display_worker_rate_init(pj_display_worker_rate_limiter_t *limiter,
                                 uint32_t minimum_interval_ms)
{
    if (limiter == NULL) {
        return;
    }
    memset(limiter, 0, sizeof(*limiter));
    limiter->minimum_interval_ms = minimum_interval_ms;
}

uint64_t pj_display_worker_rate_earliest_start(
    const pj_display_worker_rate_limiter_t *limiter,
    const pj_ui_dirty_region_t *dirty, uint64_t now_ms)
{
    if (limiter == NULL || dirty == NULL || !dirty->partial ||
        limiter->minimum_interval_ms == 0) {
        return now_ms;
    }
    pj_ui_dirty_region_t normalized;
    if (!normalize_region(dirty, &normalized) ||
        !limiter->partial_started) {
        return now_ms;
    }
    uint64_t elapsed = now_ms >= limiter->last_partial_started_ms ?
        now_ms - limiter->last_partial_started_ms : 0;
    if (elapsed >= limiter->minimum_interval_ms) {
        return now_ms;
    }
    return now_ms + (limiter->minimum_interval_ms - elapsed);
}

int pj_display_worker_rate_record_start(
    pj_display_worker_rate_limiter_t *limiter,
    const pj_ui_dirty_region_t *dirty, uint64_t now_ms)
{
    if (limiter == NULL || dirty == NULL) {
        return 0;
    }
    if (!dirty->partial) {
        return 1;
    }
    if (pj_display_worker_rate_earliest_start(limiter, dirty, now_ms) >
        now_ms) {
        return 0;
    }
    pj_ui_dirty_region_t normalized;
    if (!normalize_region(dirty, &normalized)) {
        return 0;
    }
    limiter->last_partial_started_ms = now_ms;
    limiter->partial_started = 1;
    return 1;
}

#ifdef ESP_PLATFORM

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pj_board.h"

#define PJ_DISPLAY_WORKER_STACK_WORDS 4096
#define PJ_DISPLAY_WORKER_PRIORITY 3
#define PJ_DISPLAY_WORKER_RETRY_MIN_MS 250U
#define PJ_DISPLAY_WORKER_RETRY_MAX_MS 2000U

static const char *TAG = "pj-display-worker";
static pj_display_worker_model_t g_model;
static pj_framebuffer_t g_slots[PJ_DISPLAY_WORKER_SLOT_COUNT];
static StaticTask_t g_task_storage;
static StackType_t g_task_stack[PJ_DISPLAY_WORKER_STACK_WORDS];
static TaskHandle_t g_task;
static portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
static int g_stop_requested;
static uint32_t g_committed_frames;
static pj_display_worker_rate_limiter_t g_rate_limiter;

static uint64_t monotonic_ms(void)
{
    int64_t now_us = esp_timer_get_time();
    return now_us <= 0 ? 0 : (uint64_t)now_us / 1000u;
}

static TickType_t delay_ticks(uint64_t delay_ms)
{
    if (delay_ms == 0) {
        return 0;
    }
    if (delay_ms > UINT32_MAX) {
        delay_ms = UINT32_MAX;
    }
    TickType_t ticks = pdMS_TO_TICKS((uint32_t)delay_ms);
    return ticks == 0 ? 1 : ticks;
}

static uint32_t retry_delay_ms(uint32_t failures)
{
    uint32_t delay = PJ_DISPLAY_WORKER_RETRY_MIN_MS;
    while (failures > 1 && delay < PJ_DISPLAY_WORKER_RETRY_MAX_MS) {
        delay *= 2U;
        failures--;
    }
    return delay > PJ_DISPLAY_WORKER_RETRY_MAX_MS ?
        PJ_DISPLAY_WORKER_RETRY_MAX_MS : delay;
}

static void display_worker_task(void *argument)
{
    (void)argument;
    TickType_t wait_ticks = portMAX_DELAY;
    uint32_t consecutive_failures = 0;

    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, wait_ticks);

        int slot = -1;
        int stopping;
        pj_display_worker_request_t request;
        uint32_t generation = 0;
        uint64_t now_ms = monotonic_ms();
        uint64_t earliest_ms = now_ms;
        portENTER_CRITICAL(&g_lock);
        stopping = g_stop_requested;
        int ready = !stopping && pj_display_worker_model_peek_request(
            &g_model, &request, &generation);
        if (ready) {
            if (request.cadence_class == PJ_DISPLAY_CADENCE_SECONDS) {
                earliest_ms = request.cadence_deadline_ms;
            } else if (!request.barrier) {
                earliest_ms = pj_display_worker_rate_earliest_start(
                    &g_rate_limiter, &request.dirty, now_ms);
            }
            if (earliest_ms <= now_ms) {
                ready = pj_display_worker_model_take_request_at(
                    &g_model, &slot, &request, &generation, now_ms);
            } else if (request.cadence_class == PJ_DISPLAY_CADENCE_NONE) {
                pj_display_worker_model_note_rate_deferred(&g_model);
            }
        }
        portEXIT_CRITICAL(&g_lock);
        if (stopping) {
            break;
        }
        if (!ready) {
            wait_ticks = portMAX_DELAY;
            continue;
        }

        if (earliest_ms > now_ms) {
            wait_ticks = delay_ticks(earliest_ms - now_ms);
            continue;
        }
        if (!ready) {
            wait_ticks = 0;
            continue;
        }
        portENTER_CRITICAL(&g_lock);
        configASSERT(g_model.ordering_errors == 0);
        portEXIT_CRITICAL(&g_lock);
        int rate_allowed = request.barrier ||
            request.cadence_class == PJ_DISPLAY_CADENCE_SECONDS ||
            pj_display_worker_rate_record_start(
                &g_rate_limiter, &request.dirty, monotonic_ms());
        configASSERT(rate_allowed);

        int success = request.barrier || pj_board_display_framebuffer_ex(
            &g_slots[slot], &request.dirty,
            request.cadence_class == PJ_DISPLAY_CADENCE_SECONDS);
        int cleanup_pending = !request.barrier &&
            pj_board_display_cleanup_pending();
        uint64_t committed_at_ms = monotonic_ms();
        portENTER_CRITICAL(&g_lock);
        if (cleanup_pending) {
            pj_display_worker_model_cleanup_due(&g_model);
        }
        pj_display_worker_model_complete_at(
            &g_model, slot, success, committed_at_ms);
        if (success && !request.barrier &&
            g_committed_frames < UINT32_MAX) {
            g_committed_frames++;
        }
        configASSERT(g_model.ordering_errors == 0);
        stopping = g_stop_requested;
        portEXIT_CRITICAL(&g_lock);

        if (stopping) {
            break;
        }
        if (success) {
            consecutive_failures = 0;
            wait_ticks = 0;
        } else {
            if (consecutive_failures < UINT32_MAX) {
                consecutive_failures++;
            }
            uint32_t delay_ms = retry_delay_ms(consecutive_failures);
            ESP_LOGW(TAG,
                     "Display generation %" PRIu32
                     " failed; latest frame will retry full in %" PRIu32 " ms",
                     generation, delay_ms);
            wait_ticks = pdMS_TO_TICKS(delay_ms);
        }
    }

    portENTER_CRITICAL(&g_lock);
    pj_display_worker_model_shutdown(&g_model);
    g_task = NULL;
    portEXIT_CRITICAL(&g_lock);
    vTaskDelete(NULL);
}

int pj_display_worker_start(void)
{
    portENTER_CRITICAL(&g_lock);
    if (g_task != NULL) {
        int running = !g_stop_requested;
        portEXIT_CRITICAL(&g_lock);
        return running;
    }
    memset(g_slots, 0, sizeof(g_slots));
    pj_display_worker_model_init(&g_model);
    pj_display_worker_rate_init(&g_rate_limiter,
                                PJ_DISPLAY_WORKER_PARTIAL_INTERVAL_MS);
    g_stop_requested = 0;
    g_committed_frames = 0;
    portEXIT_CRITICAL(&g_lock);

    TaskHandle_t task = xTaskCreateStatic(
        display_worker_task, "pj-display", PJ_DISPLAY_WORKER_STACK_WORDS,
        NULL, PJ_DISPLAY_WORKER_PRIORITY, g_task_stack, &g_task_storage);
    if (task == NULL) {
        portENTER_CRITICAL(&g_lock);
        pj_display_worker_model_shutdown(&g_model);
        portEXIT_CRITICAL(&g_lock);
        ESP_LOGE(TAG, "Failed to create display worker task");
        return 0;
    }
    portENTER_CRITICAL(&g_lock);
    g_task = task;
    portEXIT_CRITICAL(&g_lock);
    return 1;
}

void pj_display_worker_stop(void)
{
    TaskHandle_t task;
    portENTER_CRITICAL(&g_lock);
    g_stop_requested = 1;
    pj_display_worker_model_shutdown(&g_model);
    task = g_task;
    portEXIT_CRITICAL(&g_lock);
    if (task != NULL) {
        xTaskNotifyGive(task);
    }
}

int pj_display_worker_submit_request(
    const pj_framebuffer_t *framebuffer,
    const pj_display_worker_request_t *request, uint32_t *generation)
{
    if (framebuffer == NULL || request == NULL) {
        return 0;
    }

    int slot = -1;
    portENTER_CRITICAL(&g_lock);
    int accepted = g_task != NULL && !g_stop_requested &&
        pj_display_worker_model_begin_submit_request(
            &g_model, request, &slot);
    portEXIT_CRITICAL(&g_lock);
    if (!accepted) {
        return 0;
    }

    memcpy(&g_slots[slot], framebuffer, sizeof(*framebuffer));

    TaskHandle_t task;
    portENTER_CRITICAL(&g_lock);
    accepted = pj_display_worker_model_commit_submit_request(
        &g_model, slot, request, generation);
    task = g_task;
    portEXIT_CRITICAL(&g_lock);
    if (!accepted || task == NULL) {
        return 0;
    }
    xTaskNotifyGive(task);
    return 1;
}

int pj_display_worker_cadence_start(uint32_t first_sequence,
                                    uint64_t first_deadline_ms)
{
    portENTER_CRITICAL(&g_lock);
    int started = g_task != NULL && !g_stop_requested &&
        pj_display_worker_model_cadence_start(
            &g_model, first_sequence, first_deadline_ms);
    portEXIT_CRITICAL(&g_lock);
    return started;
}

void pj_display_worker_cadence_end(void)
{
    portENTER_CRITICAL(&g_lock);
    pj_display_worker_model_cadence_end(&g_model);
    TaskHandle_t task = g_task;
    portEXIT_CRITICAL(&g_lock);
    if (task != NULL) {
        xTaskNotifyGive(task);
    }
}

void pj_display_worker_cleanup_due(void)
{
    portENTER_CRITICAL(&g_lock);
    pj_display_worker_model_cleanup_due(&g_model);
    portEXIT_CRITICAL(&g_lock);
}

int pj_display_worker_take_cleanup_pending(void)
{
    portENTER_CRITICAL(&g_lock);
    int pending = pj_display_worker_model_take_cleanup_pending(&g_model);
    portEXIT_CRITICAL(&g_lock);
    return pending;
}

int pj_display_worker_is_idle(void)
{
    portENTER_CRITICAL(&g_lock);
    int idle = g_task != NULL && !g_stop_requested &&
        pj_display_worker_model_is_idle(&g_model);
    portEXIT_CRITICAL(&g_lock);
    return idle;
}

uint32_t pj_display_worker_committed_frames(void)
{
    portENTER_CRITICAL(&g_lock);
    uint32_t committed = g_committed_frames;
    portEXIT_CRITICAL(&g_lock);
    return committed;
}

pj_display_worker_status_t pj_display_worker_status(void)
{
    portENTER_CRITICAL(&g_lock);
    pj_display_worker_status_t status =
        pj_display_worker_model_status(&g_model);
    portEXIT_CRITICAL(&g_lock);
    return status;
}

void pj_display_worker_note_input_deferred(void)
{
    portENTER_CRITICAL(&g_lock);
    pj_display_worker_model_note_input_deferred(&g_model);
    portEXIT_CRITICAL(&g_lock);
}

void pj_display_worker_note_cadence_fault(void)
{
    portENTER_CRITICAL(&g_lock);
    pj_display_worker_model_note_cadence_fault(&g_model);
    portEXIT_CRITICAL(&g_lock);
}

#else

int pj_display_worker_start(void)
{
    return 0;
}

void pj_display_worker_stop(void)
{
}

int pj_display_worker_submit_request(
    const pj_framebuffer_t *framebuffer,
    const pj_display_worker_request_t *request, uint32_t *generation)
{
    (void)framebuffer;
    (void)request;
    if (generation != NULL) {
        *generation = 0;
    }
    return 0;
}

int pj_display_worker_cadence_start(uint32_t first_sequence,
                                    uint64_t first_deadline_ms)
{
    (void)first_sequence;
    (void)first_deadline_ms;
    return 0;
}

void pj_display_worker_cadence_end(void)
{
}

void pj_display_worker_cleanup_due(void)
{
}

int pj_display_worker_take_cleanup_pending(void)
{
    return 0;
}

int pj_display_worker_is_idle(void)
{
    return 1;
}

uint32_t pj_display_worker_committed_frames(void)
{
    return 0;
}

pj_display_worker_status_t pj_display_worker_status(void)
{
    return (pj_display_worker_status_t) {0};
}

void pj_display_worker_note_input_deferred(void)
{
}

void pj_display_worker_note_cadence_fault(void)
{
}

#endif
