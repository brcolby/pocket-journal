#pragma once

#include <stdint.h>

#include "pj_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_DISPLAY_REFRESH_DEFAULT_PARTIAL_LIMIT 30u

typedef enum {
    PJ_DISPLAY_REFRESH_NOOP = 0,
    PJ_DISPLAY_REFRESH_PARTIAL,
    PJ_DISPLAY_REFRESH_FULL,
} pj_display_refresh_kind_t;

typedef struct {
    pj_display_refresh_kind_t kind;
    pj_ui_dirty_region_t region;
    uint32_t requested_area;
    uint32_t changed_pixels;
    uint32_t transfer_bytes;
    int requested_partial;
    int promoted_to_full;
} pj_display_refresh_plan_t;

typedef struct {
    uint64_t requests;
    uint64_t requested_full;
    uint64_t requested_partial;
    uint64_t applied_full;
    uint64_t applied_partial;
    uint64_t noops;
    uint64_t errors;
    uint64_t requested_area;
    uint64_t changed_pixels;
    uint64_t transfer_bytes;
    uint64_t total_latency_us;
    uint64_t busy_time_us;
    uint32_t max_latency_us;
    uint32_t max_busy_time_us;
} pj_display_refresh_metrics_t;

typedef struct {
    uint32_t partial_limit;
    uint32_t partial_since_full;
    pj_display_refresh_metrics_t metrics;
} pj_display_refresh_policy_t;

void pj_display_refresh_policy_init(pj_display_refresh_policy_t *policy,
                                    uint32_t partial_limit);
int pj_display_refresh_region_normalize(const pj_ui_dirty_region_t *dirty,
                                        int align_x_to_byte,
                                        pj_ui_dirty_region_t *normalized);
pj_display_refresh_plan_t pj_display_refresh_plan(
    const pj_display_refresh_policy_t *policy,
    const pj_framebuffer_t *framebuffer,
    const pj_framebuffer_t *shadow,
    int shadow_valid,
    const pj_ui_dirty_region_t *dirty);
void pj_display_refresh_record(pj_display_refresh_policy_t *policy,
                               const pj_display_refresh_plan_t *plan,
                               int success,
                               uint32_t latency_us,
                               uint32_t busy_time_us);
void pj_display_refresh_apply_shadow(pj_framebuffer_t *shadow,
                                     const pj_framebuffer_t *framebuffer,
                                     const pj_display_refresh_plan_t *plan);
int pj_display_refresh_complete(pj_display_refresh_policy_t *policy,
                                pj_framebuffer_t *shadow,
                                int *shadow_valid,
                                const pj_framebuffer_t *framebuffer,
                                const pj_display_refresh_plan_t *plan,
                                int success,
                                uint32_t latency_us,
                                uint32_t busy_time_us);

#ifdef __cplusplus
}
#endif
