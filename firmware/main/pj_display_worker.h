#pragma once

#include <stdint.h>

#include "pj_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_DISPLAY_WORKER_SLOT_COUNT 2
#define PJ_DISPLAY_WORKER_PARTIAL_INTERVAL_MS 1000U
#define PJ_DISPLAY_WORKER_CADENCE_PERIOD_MS 1000U
#define PJ_DISPLAY_WORKER_CADENCE_START_TOLERANCE_MS 75U

typedef enum {
    PJ_DISPLAY_WORKER_SLOT_FREE = 0,
    PJ_DISPLAY_WORKER_SLOT_WRITING,
    PJ_DISPLAY_WORKER_SLOT_READY,
    PJ_DISPLAY_WORKER_SLOT_DISPLAYING,
} pj_display_worker_slot_state_t;

typedef enum {
    PJ_DISPLAY_CADENCE_NONE = 0,
    PJ_DISPLAY_CADENCE_SECONDS,
} pj_display_cadence_class_t;

typedef enum {
    PJ_DISPLAY_CLEANUP_NONE = 0,
    PJ_DISPLAY_CLEANUP_DEFERRED,
    PJ_DISPLAY_CLEANUP_PENDING,
} pj_display_cleanup_state_t;

typedef struct {
    pj_ui_dirty_region_t dirty;
    uint32_t layout_epoch;
    uint32_t interaction_generation;
    uint32_t visual_revision;
    uint32_t full_refresh_revision;
    pj_display_cadence_class_t cadence_class;
    uint32_t cadence_sequence;
    uint64_t cadence_deadline_ms;
    pj_display_cleanup_state_t cleanup_state;
    int barrier;
} pj_display_worker_request_t;

typedef struct {
    pj_display_worker_slot_state_t state;
    pj_ui_dirty_region_t dirty;
    uint32_t generation;
    uint32_t layout_epoch;
    uint32_t interaction_generation;
    uint32_t visual_revision;
    uint32_t full_refresh_revision;
    pj_display_cadence_class_t cadence_class;
    uint32_t cadence_sequence;
    uint64_t cadence_deadline_ms;
    pj_display_cleanup_state_t cleanup_state;
    int barrier;
    int cadence_fault_recorded;
} pj_display_worker_slot_t;

typedef struct {
    pj_display_worker_slot_t slots[PJ_DISPLAY_WORKER_SLOT_COUNT];
    uint32_t next_generation;
    uint32_t accepted_generation;
    uint32_t started_generation;
    uint32_t started_layout_epoch;
    uint32_t started_interaction_generation;
    uint32_t committed_generation;
    uint32_t committed_layout_epoch;
    uint32_t committed_interaction_generation;
    uint32_t committed_visual_revision;
    uint32_t committed_full_refresh_revision;
    uint64_t committed_interaction_started_ms;
    uint32_t superseded_frames;
    uint32_t rate_deferred_frames;
    uint32_t input_deferred_events;
    uint32_t ordering_errors;
    uint32_t cadence_starts;
    uint32_t cadence_commits;
    uint32_t cadence_max_start_lateness_ms;
    uint32_t cadence_overruns;
    uint32_t cadence_misses;
    uint32_t cadence_expected_submit_sequence;
    uint32_t cadence_expected_commit_sequence;
    uint32_t cadence_last_started_sequence;
    uint32_t cadence_last_committed_sequence;
    uint64_t cadence_expected_deadline_ms;
    uint32_t cleanup_deferred_frames;
    int cadence_active;
    int cleanup_pending;
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
    uint32_t started_layout_epoch;
    uint32_t started_interaction_generation;
    uint32_t committed_generation;
    uint32_t committed_layout_epoch;
    uint32_t committed_interaction_generation;
    uint32_t committed_visual_revision;
    uint32_t committed_full_refresh_revision;
    uint64_t committed_interaction_started_ms;
    uint32_t superseded_frames;
    uint32_t rate_deferred_frames;
    uint32_t input_deferred_events;
    uint32_t ordering_errors;
    uint32_t cadence_starts;
    uint32_t cadence_commits;
    uint32_t cadence_max_start_lateness_ms;
    uint32_t cadence_overruns;
    uint32_t cadence_misses;
    uint32_t cadence_last_started_sequence;
    uint32_t cadence_last_committed_sequence;
    uint32_t cleanup_deferred_frames;
    int cadence_active;
    int cleanup_pending;
} pj_display_worker_status_t;

void pj_display_worker_request_init(pj_display_worker_request_t *request,
                                    const pj_ui_dirty_region_t *dirty);
void pj_display_worker_model_init(pj_display_worker_model_t *model);
int pj_display_worker_model_begin_submit_request(
    pj_display_worker_model_t *model,
    const pj_display_worker_request_t *request, int *slot_index);
int pj_display_worker_model_commit_submit_request(
    pj_display_worker_model_t *model, int slot_index,
    const pj_display_worker_request_t *request, uint32_t *generation);
int pj_display_worker_model_peek_request(
    pj_display_worker_model_t *model, pj_display_worker_request_t *request,
    uint32_t *generation);
int pj_display_worker_model_take_request_at(
    pj_display_worker_model_t *model, int *slot_index,
    pj_display_worker_request_t *request, uint32_t *generation,
    uint64_t started_at_ms);
void pj_display_worker_model_complete(pj_display_worker_model_t *model,
                                      int slot_index,
                                      int success);
void pj_display_worker_model_complete_at(pj_display_worker_model_t *model,
                                         int slot_index, int success,
                                         uint64_t committed_at_ms);
void pj_display_worker_model_shutdown(pj_display_worker_model_t *model);
int pj_display_worker_model_is_idle(const pj_display_worker_model_t *model);
pj_display_worker_status_t pj_display_worker_model_status(
    const pj_display_worker_model_t *model);
void pj_display_worker_model_note_rate_deferred(
    pj_display_worker_model_t *model);
void pj_display_worker_model_note_input_deferred(
    pj_display_worker_model_t *model);
void pj_display_worker_model_note_cadence_fault(
    pj_display_worker_model_t *model);
int pj_display_worker_model_cadence_start(
    pj_display_worker_model_t *model, uint32_t first_sequence,
    uint64_t first_deadline_ms);
void pj_display_worker_model_cadence_end(pj_display_worker_model_t *model);
void pj_display_worker_model_cleanup_due(pj_display_worker_model_t *model);
int pj_display_worker_model_take_cleanup_pending(
    pj_display_worker_model_t *model);

/* Equal millisecond timestamps are conservatively treated as pre-commit. */
int pj_display_worker_status_accepts_interaction(
    const pj_display_worker_status_t *status,
    uint32_t interaction_generation, uint64_t captured_at_ms);

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
int pj_display_worker_submit_request(
    const pj_framebuffer_t *framebuffer,
    const pj_display_worker_request_t *request, uint32_t *generation);
int pj_display_worker_cadence_start(uint32_t first_sequence,
                                    uint64_t first_deadline_ms);
void pj_display_worker_cadence_end(void);
void pj_display_worker_cleanup_due(void);
int pj_display_worker_take_cleanup_pending(void);
int pj_display_worker_is_idle(void);
uint32_t pj_display_worker_committed_frames(void);
pj_display_worker_status_t pj_display_worker_status(void);
void pj_display_worker_note_input_deferred(void);
void pj_display_worker_note_cadence_fault(void);

#ifdef __cplusplus
}
#endif
