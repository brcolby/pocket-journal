#include "pj_ui_presenter.h"

#include <stddef.h>
#include <string.h>

static pj_ui_dirty_region_t empty_region(void)
{
    return (pj_ui_dirty_region_t) {0};
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

static int revisions_equal(const pj_ui_presenter_revision_t *left,
                           const pj_ui_presenter_revision_t *right)
{
    return left->layout_epoch == right->layout_epoch &&
        left->interaction_generation == right->interaction_generation &&
        left->visual_revision == right->visual_revision &&
        left->full_refresh_revision == right->full_refresh_revision;
}

static int changed_pixel_bounds(const pj_framebuffer_t *before,
                                const pj_framebuffer_t *after,
                                pj_ui_dirty_region_t *dirty)
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

    if (max_x < min_x || max_y < min_y) {
        *dirty = empty_region();
        return 0;
    }
    *dirty = (pj_ui_dirty_region_t) {
        .x = min_x,
        .y = min_y,
        .width = max_x - min_x + 1,
        .height = max_y - min_y + 1,
        .partial = 1,
    };
    return 1;
}

static uint32_t next_token(pj_ui_presenter_t *presenter)
{
    uint32_t token = presenter->next_token++;
    if (token == 0) {
        token = presenter->next_token++;
    }
    if (presenter->next_token == 0) {
        presenter->next_token = 1;
    }
    return token;
}

static void export_pending(const pj_ui_presenter_t *presenter,
                           pj_ui_presenter_frame_t *frame)
{
    *frame = (pj_ui_presenter_frame_t) {
        .result = presenter->pending_result,
        .framebuffer = &presenter->pending_framebuffer,
        .dirty = presenter->pending_dirty,
        .revision = presenter->pending_revision,
        .token = presenter->pending_token,
    };
}

void pj_ui_presenter_init(pj_ui_presenter_t *presenter)
{
    if (presenter == NULL) {
        return;
    }
    memset(presenter, 0, sizeof(*presenter));
    presenter->next_token = 1;
}

void pj_ui_presenter_reset(pj_ui_presenter_t *presenter)
{
    pj_ui_presenter_init(presenter);
}

pj_ui_presenter_revision_t pj_ui_presenter_revision(
    const pj_ui_context_t *context)
{
    if (context == NULL) {
        return (pj_ui_presenter_revision_t) {0};
    }
    return (pj_ui_presenter_revision_t) {
        .layout_epoch = pj_ui_layout_epoch(context),
        .interaction_generation = pj_ui_interaction_generation(context),
        .visual_revision = pj_ui_visual_revision(context),
        .full_refresh_revision = pj_ui_full_refresh_revision(context),
    };
}

pj_ui_frame_result_t pj_ui_presenter_prepare(
    pj_ui_presenter_t *presenter, const pj_ui_context_t *context,
    const pj_ui_presenter_revision_t *revision,
    pj_ui_presenter_frame_t *frame)
{
    if (presenter == NULL || context == NULL || revision == NULL ||
        frame == NULL) {
        return PJ_UI_FRAME_IDLE;
    }

    if (presenter->pending_valid) {
        export_pending(presenter, frame);
        return presenter->pending_result;
    }

    if (presenter->accepted_valid &&
        revisions_equal(revision, &presenter->accepted_revision)) {
        *frame = (pj_ui_presenter_frame_t) {
            .result = PJ_UI_FRAME_IDLE,
            .revision = *revision,
        };
        return PJ_UI_FRAME_IDLE;
    }

    pj_ui_compose_frame(context, &presenter->pending_framebuffer);
    presenter->pending_revision = *revision;
    presenter->pending_dirty = empty_region();

    if (!presenter->accepted_valid ||
        revision->layout_epoch != presenter->accepted_revision.layout_epoch ||
        revision->full_refresh_revision !=
            presenter->accepted_revision.full_refresh_revision) {
        presenter->pending_result = PJ_UI_FRAME_FULL;
        presenter->pending_dirty = full_region();
    } else if (changed_pixel_bounds(&presenter->accepted_framebuffer,
                                    &presenter->pending_framebuffer,
                                    &presenter->pending_dirty)) {
        presenter->pending_result = PJ_UI_FRAME_PARTIAL;
    } else if (revision->interaction_generation !=
               presenter->accepted_revision.interaction_generation) {
        presenter->pending_result = PJ_UI_FRAME_BARRIER;
    } else {
        presenter->pending_result = PJ_UI_FRAME_NOOP;
    }

    presenter->pending_token = next_token(presenter);
    presenter->pending_valid = 1;
    export_pending(presenter, frame);
    return presenter->pending_result;
}

int pj_ui_presenter_accept(pj_ui_presenter_t *presenter, uint32_t token)
{
    if (presenter == NULL || !presenter->pending_valid || token == 0 ||
        token != presenter->pending_token) {
        return 0;
    }
    presenter->accepted_framebuffer = presenter->pending_framebuffer;
    presenter->accepted_revision = presenter->pending_revision;
    presenter->accepted_valid = 1;
    presenter->pending_valid = 0;
    presenter->pending_token = 0;
    presenter->pending_result = PJ_UI_FRAME_IDLE;
    presenter->pending_dirty = empty_region();
    return 1;
}

int pj_ui_presenter_reject(pj_ui_presenter_t *presenter, uint32_t token)
{
    return presenter != NULL && presenter->pending_valid && token != 0 &&
        presenter->pending_token == token;
}

int pj_ui_presenter_has_pending(const pj_ui_presenter_t *presenter)
{
    return presenter != NULL && presenter->pending_valid;
}
