#include "pj_display_refresh.h"

#include <stddef.h>
#include <string.h>

static uint32_t region_area(const pj_ui_dirty_region_t *region)
{
    return (uint32_t)region->width * (uint32_t)region->height;
}

static int framebuffer_pixel(const pj_framebuffer_t *framebuffer, int x, int y)
{
    size_t bit = (size_t)y * PJ_DISPLAY_WIDTH + (size_t)x;
    return (framebuffer->pixels[bit >> 3u] >> (bit & 7u)) & 1u;
}

void pj_display_refresh_policy_init(pj_display_refresh_policy_t *policy,
                                    uint32_t partial_limit)
{
    if (policy == NULL) {
        return;
    }
    memset(policy, 0, sizeof(*policy));
    policy->partial_limit = partial_limit;
}

void pj_display_refresh_set_cleanup_deferred(
    pj_display_refresh_policy_t *policy, int deferred)
{
    if (policy != NULL) policy->cleanup_deferred = deferred != 0;
}

int pj_display_refresh_cleanup_pending(
    const pj_display_refresh_policy_t *policy)
{
    return policy != NULL && policy->cleanup_pending;
}

int pj_display_refresh_region_normalize(const pj_ui_dirty_region_t *dirty,
                                        int align_x_to_byte,
                                        pj_ui_dirty_region_t *normalized)
{
    if (normalized == NULL) {
        return 0;
    }
    if (dirty == NULL || !dirty->partial) {
        *normalized = (pj_ui_dirty_region_t) {
            .x = 0,
            .y = 0,
            .width = PJ_DISPLAY_WIDTH,
            .height = PJ_DISPLAY_HEIGHT,
            .partial = 0,
        };
        return 1;
    }
    if (dirty->width <= 0 || dirty->height <= 0) {
        return 0;
    }

    int64_t x0 = dirty->x;
    int64_t y0 = dirty->y;
    int64_t x1 = x0 + dirty->width;
    int64_t y1 = y0 + dirty->height;
    if (x1 <= 0 || y1 <= 0 || x0 >= PJ_DISPLAY_WIDTH || y0 >= PJ_DISPLAY_HEIGHT) {
        return 0;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > PJ_DISPLAY_WIDTH) x1 = PJ_DISPLAY_WIDTH;
    if (y1 > PJ_DISPLAY_HEIGHT) y1 = PJ_DISPLAY_HEIGHT;
    if (align_x_to_byte) {
        x0 &= ~INT64_C(7);
        x1 = (x1 + 7) & ~INT64_C(7);
        if (x1 > PJ_DISPLAY_WIDTH) x1 = PJ_DISPLAY_WIDTH;
    }
    if (x1 <= x0 || y1 <= y0) {
        return 0;
    }
    *normalized = (pj_ui_dirty_region_t) {
        .x = (int)x0,
        .y = (int)y0,
        .width = (int)(x1 - x0),
        .height = (int)(y1 - y0),
        .partial = 1,
    };
    return 1;
}

pj_display_refresh_plan_t pj_display_refresh_plan(
    const pj_display_refresh_policy_t *policy,
    const pj_framebuffer_t *framebuffer,
    const pj_framebuffer_t *shadow,
    int shadow_valid,
    const pj_ui_dirty_region_t *dirty)
{
    pj_display_refresh_plan_t plan = {0};
    pj_ui_dirty_region_t requested;
    if (framebuffer == NULL ||
        !pj_display_refresh_region_normalize(dirty, 0, &requested)) {
        return plan;
    }
    plan.requested_area = region_area(&requested);
    plan.requested_partial = requested.partial;

    if (!requested.partial || !shadow_valid || shadow == NULL) {
        plan.kind = PJ_DISPLAY_REFRESH_FULL;
        plan.region = (pj_ui_dirty_region_t) {
            .x = 0,
            .y = 0,
            .width = PJ_DISPLAY_WIDTH,
            .height = PJ_DISPLAY_HEIGHT,
            .partial = 0,
        };
        plan.changed_pixels = shadow_valid && shadow != NULL ? 0u :
            (uint32_t)PJ_DISPLAY_WIDTH * PJ_DISPLAY_HEIGHT;
        if (shadow_valid && shadow != NULL) {
            for (int y = 0; y < PJ_DISPLAY_HEIGHT; y++) {
                for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
                    plan.changed_pixels += framebuffer_pixel(framebuffer, x, y) !=
                        framebuffer_pixel(shadow, x, y);
                }
            }
        }
        plan.transfer_bytes = PJ_FRAMEBUFFER_BYTES * 2u;
        return plan;
    }

    int changed_x0 = PJ_DISPLAY_WIDTH;
    int changed_y0 = PJ_DISPLAY_HEIGHT;
    int changed_x1 = -1;
    int changed_y1 = -1;
    for (int y = requested.y; y < requested.y + requested.height; y++) {
        for (int x = requested.x; x < requested.x + requested.width; x++) {
            if (framebuffer_pixel(framebuffer, x, y) == framebuffer_pixel(shadow, x, y)) {
                continue;
            }
            plan.changed_pixels++;
            if (x < changed_x0) changed_x0 = x;
            if (x > changed_x1) changed_x1 = x;
            if (y < changed_y0) changed_y0 = y;
            if (y > changed_y1) changed_y1 = y;
        }
    }
    if (plan.changed_pixels == 0) {
        return plan;
    }

    pj_ui_dirty_region_t changed = {
        .x = changed_x0,
        .y = changed_y0,
        .width = changed_x1 - changed_x0 + 1,
        .height = changed_y1 - changed_y0 + 1,
        .partial = 1,
    };
    (void)pj_display_refresh_region_normalize(&changed, 1, &plan.region);
    int cleanup_due = policy != NULL && policy->partial_limit > 0 &&
        (policy->cleanup_pending ||
         policy->partial_since_full >= policy->partial_limit - 1u);
    if (cleanup_due && !policy->cleanup_deferred) {
        plan.kind = PJ_DISPLAY_REFRESH_FULL;
        plan.region = (pj_ui_dirty_region_t) {
            .x = 0,
            .y = 0,
            .width = PJ_DISPLAY_WIDTH,
            .height = PJ_DISPLAY_HEIGHT,
            .partial = 0,
        };
        plan.transfer_bytes = PJ_FRAMEBUFFER_BYTES * 2u;
        plan.promoted_to_full = 1;
    } else {
        plan.kind = PJ_DISPLAY_REFRESH_PARTIAL;
        plan.deferred_cleanup = cleanup_due;
        plan.transfer_bytes = (uint32_t)(plan.region.width / 8) *
            (uint32_t)plan.region.height;
    }
    return plan;
}

void pj_display_refresh_record(pj_display_refresh_policy_t *policy,
                               const pj_display_refresh_plan_t *plan,
                               int success,
                               uint32_t latency_us,
                               uint32_t busy_time_us)
{
    if (policy == NULL || plan == NULL) {
        return;
    }
    pj_display_refresh_metrics_t *metrics = &policy->metrics;
    metrics->requests++;
    metrics->requested_area += plan->requested_area;
    metrics->changed_pixels += plan->changed_pixels;
    if (plan->requested_partial) {
        metrics->requested_partial++;
    } else {
        metrics->requested_full++;
    }
    if (!success) {
        metrics->errors++;
        policy->partial_since_full = 0;
        return;
    }
    if (plan->kind == PJ_DISPLAY_REFRESH_NOOP) {
        metrics->noops++;
        return;
    }

    metrics->transfer_bytes += plan->transfer_bytes;
    metrics->total_latency_us += latency_us;
    metrics->busy_time_us += busy_time_us;
    if (latency_us > metrics->max_latency_us) metrics->max_latency_us = latency_us;
    if (busy_time_us > metrics->max_busy_time_us) metrics->max_busy_time_us = busy_time_us;
    if (plan->kind == PJ_DISPLAY_REFRESH_FULL) {
        metrics->applied_full++;
        policy->partial_since_full = 0;
        policy->cleanup_pending = 0;
    } else {
        metrics->applied_partial++;
        if (policy->partial_since_full < UINT32_MAX) policy->partial_since_full++;
        if (plan->deferred_cleanup) {
            policy->cleanup_pending = 1;
            metrics->cleanup_deferrals++;
        }
    }
}

void pj_display_refresh_apply_shadow(pj_framebuffer_t *shadow,
                                     const pj_framebuffer_t *framebuffer,
                                     const pj_display_refresh_plan_t *plan)
{
    if (shadow == NULL || framebuffer == NULL || plan == NULL ||
        plan->kind == PJ_DISPLAY_REFRESH_NOOP) {
        return;
    }
    if (plan->kind == PJ_DISPLAY_REFRESH_FULL) {
        *shadow = *framebuffer;
        return;
    }
    const pj_ui_dirty_region_t *region = &plan->region;
    size_t bytes_per_row = PJ_DISPLAY_WIDTH / 8u;
    size_t x_byte = (size_t)region->x / 8u;
    size_t width_bytes = (size_t)region->width / 8u;
    for (int y = region->y; y < region->y + region->height; y++) {
        size_t offset = (size_t)y * bytes_per_row + x_byte;
        memcpy(&shadow->pixels[offset], &framebuffer->pixels[offset], width_bytes);
    }
}

int pj_display_refresh_complete(pj_display_refresh_policy_t *policy,
                                pj_framebuffer_t *shadow,
                                int *shadow_valid,
                                const pj_framebuffer_t *framebuffer,
                                const pj_display_refresh_plan_t *plan,
                                int success,
                                uint32_t latency_us,
                                uint32_t busy_time_us)
{
    if (policy == NULL || shadow_valid == NULL || plan == NULL) {
        return 0;
    }

    if (success && plan->kind != PJ_DISPLAY_REFRESH_NOOP &&
        (shadow == NULL || framebuffer == NULL ||
         (plan->kind == PJ_DISPLAY_REFRESH_PARTIAL && !*shadow_valid))) {
        success = 0;
    }
    pj_display_refresh_record(policy, plan, success, latency_us, busy_time_us);
    if (!success) {
        *shadow_valid = 0;
        return 0;
    }
    if (plan->kind == PJ_DISPLAY_REFRESH_NOOP) {
        return 1;
    }
    pj_display_refresh_apply_shadow(shadow, framebuffer, plan);
    if (plan->kind == PJ_DISPLAY_REFRESH_FULL) {
        *shadow_valid = 1;
    }
    return 1;
}

int pj_display_refresh_commit_partial_planes(
    const pj_display_partial_plane_io_t *io,
    const uint8_t *current,
    size_t length)
{
    if (io == NULL || current == NULL || length == 0u ||
        io->position == NULL || io->command == NULL || io->write == NULL ||
        io->activate == NULL) {
        return -1;
    }

    int result = io->position(io->context);
    if (result != 0) return result;
    result = io->command(io->context, PJ_DISPLAY_PARTIAL_CURRENT_RAM_COMMAND);
    if (result != 0) return result;
    result = io->write(io->context, current, length);
    if (result != 0) return result;
    return io->activate(io->context);
}
