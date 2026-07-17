#pragma once

#include <stdint.h>

#include "pj_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PJ_UI_FRAME_IDLE = 0,
    PJ_UI_FRAME_NOOP,
    PJ_UI_FRAME_PARTIAL,
    PJ_UI_FRAME_FULL,
    PJ_UI_FRAME_BARRIER,
} pj_ui_frame_result_t;

typedef struct {
    uint32_t layout_epoch;
    uint32_t interaction_generation;
    uint32_t visual_revision;
    uint32_t full_refresh_revision;
} pj_ui_presenter_revision_t;

typedef struct {
    pj_ui_frame_result_t result;
    const pj_framebuffer_t *framebuffer;
    pj_ui_dirty_region_t dirty;
    pj_ui_presenter_revision_t revision;
    uint32_t token;
} pj_ui_presenter_frame_t;

typedef struct {
    pj_framebuffer_t accepted_framebuffer;
    pj_framebuffer_t pending_framebuffer;
    pj_ui_presenter_revision_t accepted_revision;
    pj_ui_presenter_revision_t pending_revision;
    pj_ui_dirty_region_t pending_dirty;
    pj_ui_frame_result_t pending_result;
    uint32_t next_token;
    uint32_t pending_token;
    int accepted_valid;
    int pending_valid;
} pj_ui_presenter_t;

void pj_ui_presenter_init(pj_ui_presenter_t *presenter);
pj_ui_presenter_revision_t pj_ui_presenter_revision(
    const pj_ui_context_t *context);

/*
 * A rejected worker submission deliberately leaves the pending snapshot in
 * place. Repeated preparation therefore returns the exact same token and
 * framebuffer until pj_ui_presenter_accept() acknowledges that the worker
 * copied the complete snapshot.
 */
pj_ui_frame_result_t pj_ui_presenter_prepare(
    pj_ui_presenter_t *presenter, const pj_ui_context_t *context,
    const pj_ui_presenter_revision_t *revision,
    pj_ui_presenter_frame_t *frame);
int pj_ui_presenter_accept(pj_ui_presenter_t *presenter, uint32_t token);
int pj_ui_presenter_reject(pj_ui_presenter_t *presenter, uint32_t token);
int pj_ui_presenter_has_pending(const pj_ui_presenter_t *presenter);
void pj_ui_presenter_reset(pj_ui_presenter_t *presenter);

#ifdef __cplusplus
}
#endif
