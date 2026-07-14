#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pj_display_refresh.h"

static void set_pixel(pj_framebuffer_t *framebuffer, int x, int y)
{
    size_t bit = (size_t)y * PJ_DISPLAY_WIDTH + (size_t)x;
    framebuffer->pixels[bit >> 3u] |= (uint8_t)(1u << (bit & 7u));
}

static int get_pixel(const pj_framebuffer_t *framebuffer, int x, int y)
{
    size_t bit = (size_t)y * PJ_DISPLAY_WIDTH + (size_t)x;
    return (framebuffer->pixels[bit >> 3u] >> (bit & 7u)) & 1u;
}

static void test_region_clips_and_aligns(void)
{
    pj_ui_dirty_region_t normalized;
    const pj_ui_dirty_region_t dirty = {
        .x = -3, .y = -2, .width = 13, .height = 8, .partial = 1,
    };
    assert(pj_display_refresh_region_normalize(&dirty, 1, &normalized));
    assert(normalized.x == 0 && normalized.y == 0);
    assert(normalized.width == 16 && normalized.height == 6);

    const pj_ui_dirty_region_t right = {
        .x = 197, .y = 198, .width = 10, .height = 10, .partial = 1,
    };
    assert(pj_display_refresh_region_normalize(&right, 1, &normalized));
    assert(normalized.x == 192 && normalized.y == 198);
    assert(normalized.width == 8 && normalized.height == 2);

    const pj_ui_dirty_region_t outside = {
        .x = 210, .y = 20, .width = 5, .height = 5, .partial = 1,
    };
    assert(!pj_display_refresh_region_normalize(&outside, 1, &normalized));
}

static void test_identical_partial_is_noop(void)
{
    pj_framebuffer_t framebuffer = {0};
    pj_framebuffer_t shadow = {0};
    pj_display_refresh_policy_t policy;
    pj_display_refresh_policy_init(&policy, 30);
    const pj_ui_dirty_region_t dirty = {
        .x = 3, .y = 4, .width = 10, .height = 12, .partial = 1,
    };
    pj_display_refresh_plan_t plan = pj_display_refresh_plan(
        &policy, &framebuffer, &shadow, 1, &dirty);
    assert(plan.kind == PJ_DISPLAY_REFRESH_NOOP);
    assert(plan.requested_area == 120);
    pj_display_refresh_record(&policy, &plan, 1, 0, 0);
    assert(policy.metrics.noops == 1);
    assert(policy.partial_since_full == 0);
}

static void test_changed_bounds_are_tight_and_byte_aligned(void)
{
    pj_framebuffer_t framebuffer = {0};
    pj_framebuffer_t shadow = {0};
    pj_display_refresh_policy_t policy;
    pj_display_refresh_policy_init(&policy, 30);
    set_pixel(&framebuffer, 9, 10);
    set_pixel(&framebuffer, 17, 12);
    const pj_ui_dirty_region_t dirty = {
        .x = 3, .y = 5, .width = 30, .height = 20, .partial = 1,
    };
    pj_display_refresh_plan_t plan = pj_display_refresh_plan(
        &policy, &framebuffer, &shadow, 1, &dirty);
    assert(plan.kind == PJ_DISPLAY_REFRESH_PARTIAL);
    assert(plan.region.x == 8 && plan.region.y == 10);
    assert(plan.region.width == 16 && plan.region.height == 3);
    assert(plan.changed_pixels == 2);
    assert(plan.transfer_bytes == 6);
}

static void test_invalid_shadow_and_cadence_promote_to_full(void)
{
    pj_framebuffer_t framebuffer = {0};
    pj_framebuffer_t shadow = {0};
    pj_display_refresh_policy_t policy;
    pj_display_refresh_policy_init(&policy, 3);
    set_pixel(&framebuffer, 10, 10);
    const pj_ui_dirty_region_t dirty = {
        .x = 8, .y = 8, .width = 8, .height = 8, .partial = 1,
    };
    pj_display_refresh_plan_t plan = pj_display_refresh_plan(
        &policy, &framebuffer, &shadow, 0, &dirty);
    assert(plan.kind == PJ_DISPLAY_REFRESH_FULL);
    assert(!plan.promoted_to_full);

    policy.partial_since_full = 2;
    plan = pj_display_refresh_plan(&policy, &framebuffer, &shadow, 1, &dirty);
    assert(plan.kind == PJ_DISPLAY_REFRESH_FULL);
    assert(plan.promoted_to_full);
    pj_display_refresh_record(&policy, &plan, 1, 900, 700);
    assert(policy.partial_since_full == 0);
    assert(policy.metrics.applied_full == 1);
}

static void test_partial_shadow_copy_does_not_hide_out_of_region_change(void)
{
    pj_framebuffer_t framebuffer = {0};
    pj_framebuffer_t shadow = {0};
    pj_display_refresh_policy_t policy;
    pj_display_refresh_policy_init(&policy, 30);
    set_pixel(&framebuffer, 9, 10);
    set_pixel(&framebuffer, 150, 150);
    const pj_ui_dirty_region_t dirty = {
        .x = 8, .y = 8, .width = 8, .height = 8, .partial = 1,
    };
    pj_display_refresh_plan_t plan = pj_display_refresh_plan(
        &policy, &framebuffer, &shadow, 1, &dirty);
    assert(plan.kind == PJ_DISPLAY_REFRESH_PARTIAL);
    pj_display_refresh_apply_shadow(&shadow, &framebuffer, &plan);
    assert(get_pixel(&shadow, 9, 10));
    assert(!get_pixel(&shadow, 150, 150));
}

static void test_failed_refresh_records_error_without_advancing_cadence(void)
{
    pj_display_refresh_policy_t policy;
    pj_display_refresh_policy_init(&policy, 30);
    policy.partial_since_full = 4;
    const pj_display_refresh_plan_t plan = {
        .kind = PJ_DISPLAY_REFRESH_PARTIAL,
        .region = {.x = 0, .y = 0, .width = 8, .height = 2, .partial = 1},
        .requested_area = 16,
        .changed_pixels = 1,
        .transfer_bytes = 2,
    };
    pj_display_refresh_record(&policy, &plan, 0, 200, 100);
    assert(policy.metrics.errors == 1);
    assert(policy.metrics.applied_partial == 0);
    assert(policy.metrics.transfer_bytes == 0);
    assert(policy.partial_since_full == 0);
}

int main(void)
{
    test_region_clips_and_aligns();
    test_identical_partial_is_noop();
    test_changed_bounds_are_tight_and_byte_aligned();
    test_invalid_shadow_and_cadence_promote_to_full();
    test_partial_shadow_copy_does_not_hide_out_of_region_change();
    test_failed_refresh_records_error_without_advancing_cadence();
    puts("display refresh tests passed");
    return 0;
}
