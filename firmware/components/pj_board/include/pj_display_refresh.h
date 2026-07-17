#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pj_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_DISPLAY_REFRESH_DEFAULT_PARTIAL_LIMIT 30u
#define PJ_DISPLAY_PARTIAL_CURRENT_RAM_COMMAND UINT8_C(0x24)

typedef int (*pj_display_partial_position_fn)(void *context);
typedef int (*pj_display_partial_command_fn)(void *context, uint8_t command);
typedef int (*pj_display_partial_write_fn)(void *context,
                                           const uint8_t *data,
                                           size_t length);
typedef int (*pj_display_partial_activate_fn)(void *context);

typedef struct {
    void *context;
    pj_display_partial_position_fn position;
    pj_display_partial_command_fn command;
    pj_display_partial_write_fn write;
    pj_display_partial_activate_fn activate;
} pj_display_partial_plane_io_t;

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
    int deferred_cleanup;
} pj_display_refresh_plan_t;

typedef struct {
    uint64_t requests;
    uint64_t requested_full;
    uint64_t requested_partial;
    uint64_t applied_full;
    uint64_t applied_partial;
    uint64_t noops;
    uint64_t errors;
    uint64_t cleanup_deferrals;
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
    int cleanup_deferred;
    int cleanup_pending;
    pj_display_refresh_metrics_t metrics;
} pj_display_refresh_policy_t;

void pj_display_refresh_policy_init(pj_display_refresh_policy_t *policy,
                                    uint32_t partial_limit);
void pj_display_refresh_set_cleanup_deferred(
    pj_display_refresh_policy_t *policy, int deferred);
int pj_display_refresh_cleanup_pending(
    const pj_display_refresh_policy_t *policy);
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
/* Waveshare BW partial update: write RAM 0x24 once, then activate and wait BUSY. */
int pj_display_refresh_commit_partial_planes(
    const pj_display_partial_plane_io_t *io,
    const uint8_t *current,
    size_t length);

#ifdef __cplusplus
}
#endif
