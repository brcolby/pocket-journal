#pragma once

#include <stdint.h>

#include "pj_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_DISPLAY_WORKER_SLOT_COUNT 2
#define PJ_DISPLAY_WORKER_PARTIAL_INTERVAL_MS 1000U

typedef enum {
    PJ_DISPLAY_WORKER_SLOT_FREE = 0,
    PJ_DISPLAY_WORKER_SLOT_WRITING,
    PJ_DISPLAY_WORKER_SLOT_READY,
    PJ_DISPLAY_WORKER_SLOT_DISPLAYING,
} pj_display_worker_slot_state_t;

typedef struct {
    pj_display_worker_slot_state_t state;
    pj_ui_dirty_region_t dirty;
    uint32_t generation;
    uint32_t scene_epoch;
} pj_display_worker_slot_t;

typedef struct {
    pj_display_worker_slot_t slots[PJ_DISPLAY_WORKER_SLOT_COUNT];
    uint32_t next_generation;
    uint32_t accepted_generation;
    uint32_t started_generation;
    uint32_t started_scene_epoch;
    uint32_t committed_generation;
    uint32_t committed_scene_epoch;
    uint64_t committed_scene_started_ms;
    uint32_t superseded_frames;
    uint32_t rate_deferred_frames;
    uint32_t input_deferred_events;
    uint32_t ordering_errors;
    int accepting;
    int force_full_on_commit;
} pj_display_worker_model_t;

typedef struct {
    uint64_t last_partial_started_ms;
    uint32_t minimum_interval_ms;
    int partial_started;
} pj_display_worker_rate_limiter_t;

typedef struct {
    uint32_t accepted_generation;
    uint32_t started_generation;
    uint32_t started_scene_epoch;
    uint32_t committed_generation;
    uint32_t committed_scene_epoch;
    uint64_t committed_scene_started_ms;
    uint32_t superseded_frames;
    uint32_t rate_deferred_frames;
    uint32_t input_deferred_events;
    uint32_t ordering_errors;
} pj_display_worker_status_t;

void pj_display_worker_model_init(pj_display_worker_model_t *model);
int pj_display_worker_model_begin_submit(pj_display_worker_model_t *model,
                                         int *slot_index);
int pj_display_worker_model_commit_submit(pj_display_worker_model_t *model,
                                          int slot_index,
                                          const pj_ui_dirty_region_t *dirty,
                                          uint32_t scene_epoch,
                                          uint32_t *generation);
int pj_display_worker_model_peek(pj_display_worker_model_t *model,
                                 pj_ui_dirty_region_t *dirty,
                                 uint32_t *generation,
                                 uint32_t *scene_epoch);
int pj_display_worker_model_take(pj_display_worker_model_t *model,
                                 int *slot_index,
                                 pj_ui_dirty_region_t *dirty,
                                 uint32_t *generation,
                                 uint32_t *scene_epoch);
void pj_display_worker_model_complete(pj_display_worker_model_t *model,
                                      int slot_index,
                                      int success);
void pj_display_worker_model_complete_at(pj_display_worker_model_t *model,
                                         int slot_index, int success,
                                         uint64_t committed_at_ms);
void pj_display_worker_model_shutdown(pj_display_worker_model_t *model);
int pj_display_worker_model_is_idle(const pj_display_worker_model_t *model);
int pj_display_worker_model_scene_presented(
    const pj_display_worker_model_t *model, uint32_t scene_epoch);
pj_display_worker_status_t pj_display_worker_model_status(
    const pj_display_worker_model_t *model);
void pj_display_worker_model_note_rate_deferred(
    pj_display_worker_model_t *model);
void pj_display_worker_model_note_input_deferred(
    pj_display_worker_model_t *model);
/* Equal millisecond timestamps are conservatively treated as pre-commit. */
int pj_display_worker_status_accepts_input(
    const pj_display_worker_status_t *status, uint32_t scene_epoch,
    uint64_t captured_at_ms);

void pj_display_worker_rate_init(pj_display_worker_rate_limiter_t *limiter,
                                 uint32_t minimum_interval_ms);
uint64_t pj_display_worker_rate_earliest_start(
    const pj_display_worker_rate_limiter_t *limiter,
    const pj_ui_dirty_region_t *dirty, uint64_t now_ms);
int pj_display_worker_rate_record_start(
    pj_display_worker_rate_limiter_t *limiter,
    const pj_ui_dirty_region_t *dirty, uint64_t now_ms);

int pj_display_worker_start(void);
void pj_display_worker_stop(void);
int pj_display_worker_submit(const pj_framebuffer_t *framebuffer,
                             const pj_ui_dirty_region_t *dirty,
                             uint32_t scene_epoch,
                             uint32_t *generation);
int pj_display_worker_is_idle(void);
uint32_t pj_display_worker_committed_frames(void);
int pj_display_worker_scene_presented(uint32_t scene_epoch);
pj_display_worker_status_t pj_display_worker_status(void);
void pj_display_worker_note_input_deferred(void);

#ifdef __cplusplus
}
#endif
