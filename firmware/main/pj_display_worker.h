#pragma once

#include <stdint.h>

#include "pj_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_DISPLAY_WORKER_SLOT_COUNT 2

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
} pj_display_worker_slot_t;

typedef struct {
    pj_display_worker_slot_t slots[PJ_DISPLAY_WORKER_SLOT_COUNT];
    uint32_t next_generation;
    int accepting;
    int force_full_on_commit;
} pj_display_worker_model_t;

void pj_display_worker_model_init(pj_display_worker_model_t *model);
int pj_display_worker_model_begin_submit(pj_display_worker_model_t *model,
                                         int *slot_index);
int pj_display_worker_model_commit_submit(pj_display_worker_model_t *model,
                                          int slot_index,
                                          const pj_ui_dirty_region_t *dirty);
int pj_display_worker_model_take(pj_display_worker_model_t *model,
                                 int *slot_index,
                                 pj_ui_dirty_region_t *dirty,
                                 uint32_t *generation);
void pj_display_worker_model_complete(pj_display_worker_model_t *model,
                                      int slot_index,
                                      int success);
void pj_display_worker_model_shutdown(pj_display_worker_model_t *model);
int pj_display_worker_model_is_idle(const pj_display_worker_model_t *model);

int pj_display_worker_start(void);
void pj_display_worker_stop(void);
int pj_display_worker_submit(const pj_framebuffer_t *framebuffer,
                             const pj_ui_dirty_region_t *dirty);
int pj_display_worker_is_idle(void);
uint32_t pj_display_worker_committed_frames(void);

#ifdef __cplusplus
}
#endif
