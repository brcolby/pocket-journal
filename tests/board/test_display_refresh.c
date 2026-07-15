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
    assert(plan.transfer_bytes == 12);
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
    pj_framebuffer_t framebuffer = {0};
    pj_framebuffer_t shadow = {0};
    int shadow_valid = 1;
    set_pixel(&framebuffer, 1, 0);
    const pj_display_refresh_plan_t plan = {
        .kind = PJ_DISPLAY_REFRESH_PARTIAL,
        .region = {.x = 0, .y = 0, .width = 8, .height = 2, .partial = 1},
        .requested_area = 16,
        .changed_pixels = 1,
        .transfer_bytes = 2,
    };
    assert(!pj_display_refresh_complete(&policy, &shadow, &shadow_valid,
                                        &framebuffer, &plan, 0, 200, 100));
    assert(policy.metrics.errors == 1);
    assert(policy.metrics.applied_partial == 0);
    assert(policy.metrics.transfer_bytes == 0);
    assert(policy.partial_since_full == 0);
    assert(!shadow_valid);
    assert(!get_pixel(&shadow, 1, 0));
}

static void test_successful_refresh_atomically_updates_shadow_and_metrics(void)
{
    pj_display_refresh_policy_t policy;
    pj_display_refresh_policy_init(&policy, 30);
    pj_framebuffer_t framebuffer = {0};
    pj_framebuffer_t shadow = {0};
    int shadow_valid = 1;
    set_pixel(&framebuffer, 1, 0);
    const pj_display_refresh_plan_t partial = {
        .kind = PJ_DISPLAY_REFRESH_PARTIAL,
        .region = {.x = 0, .y = 0, .width = 8, .height = 2, .partial = 1},
        .requested_area = 16,
        .changed_pixels = 1,
        .transfer_bytes = 2,
        .requested_partial = 1,
    };
    assert(pj_display_refresh_complete(&policy, &shadow, &shadow_valid,
                                       &framebuffer, &partial, 1, 200, 100));
    assert(shadow_valid);
    assert(get_pixel(&shadow, 1, 0));
    assert(policy.metrics.applied_partial == 1);
    assert(policy.metrics.transfer_bytes == 2);
    assert(policy.metrics.busy_time_us == 100);

    memset(&framebuffer, 0, sizeof(framebuffer));
    const pj_display_refresh_plan_t full = {
        .kind = PJ_DISPLAY_REFRESH_FULL,
        .region = {.x = 0, .y = 0, .width = PJ_DISPLAY_WIDTH,
                   .height = PJ_DISPLAY_HEIGHT, .partial = 0},
        .requested_area = PJ_DISPLAY_WIDTH * PJ_DISPLAY_HEIGHT,
        .changed_pixels = 1,
        .transfer_bytes = PJ_FRAMEBUFFER_BYTES * 2,
    };
    shadow_valid = 0;
    assert(pj_display_refresh_complete(&policy, &shadow, &shadow_valid,
                                       &framebuffer, &full, 1, 400, 300));
    assert(shadow_valid);
    assert(!get_pixel(&shadow, 1, 0));
    assert(policy.metrics.applied_full == 1);
}

static void test_partial_refresh_cannot_establish_an_invalid_shadow(void)
{
    pj_display_refresh_policy_t policy;
    pj_display_refresh_policy_init(&policy, 30);
    pj_framebuffer_t framebuffer = {0};
    pj_framebuffer_t shadow = {0};
    int shadow_valid = 0;
    set_pixel(&framebuffer, 1, 0);
    const pj_display_refresh_plan_t partial = {
        .kind = PJ_DISPLAY_REFRESH_PARTIAL,
        .region = {.x = 0, .y = 0, .width = 8, .height = 2, .partial = 1},
        .requested_area = 16,
        .changed_pixels = 1,
        .transfer_bytes = 2,
        .requested_partial = 1,
    };
    assert(!pj_display_refresh_complete(&policy, &shadow, &shadow_valid,
                                        &framebuffer, &partial, 1, 200, 100));
    assert(!shadow_valid);
    assert(!get_pixel(&shadow, 1, 0));
    assert(policy.metrics.errors == 1);
    assert(policy.metrics.applied_partial == 0);
}

typedef enum {
    TEST_EVENT_POSITION = 1,
    TEST_EVENT_CURRENT_COMMAND,
    TEST_EVENT_CURRENT_WRITE,
    TEST_EVENT_ACTIVATE,
    TEST_EVENT_PREVIOUS_COMMAND,
    TEST_EVENT_PREVIOUS_WRITE,
} test_event_t;

typedef struct {
    test_event_t events[32];
    size_t event_count;
    uint8_t selected_command;
    uint8_t current_plane[4];
    uint8_t previous_plane[4];
    uint8_t activated_current[4];
    uint8_t activated_previous[4];
    size_t activation_count;
    size_t operation_count;
    size_t fail_operation;
} fake_partial_controller_t;

static int fake_operation(fake_partial_controller_t *controller)
{
    controller->operation_count++;
    return controller->fail_operation == controller->operation_count ? 73 : 0;
}

static int fake_position(void *opaque)
{
    fake_partial_controller_t *controller = opaque;
    controller->events[controller->event_count++] = TEST_EVENT_POSITION;
    return fake_operation(controller);
}

static int fake_command(void *opaque, uint8_t command)
{
    fake_partial_controller_t *controller = opaque;
    controller->selected_command = command;
    controller->events[controller->event_count++] =
        command == PJ_DISPLAY_PARTIAL_CURRENT_RAM_COMMAND ?
            TEST_EVENT_CURRENT_COMMAND : TEST_EVENT_PREVIOUS_COMMAND;
    return fake_operation(controller);
}

static int fake_write(void *opaque, const uint8_t *data, size_t length)
{
    fake_partial_controller_t *controller = opaque;
    assert(length == sizeof(controller->current_plane));
    if (controller->selected_command == PJ_DISPLAY_PARTIAL_CURRENT_RAM_COMMAND) {
        controller->events[controller->event_count++] = TEST_EVENT_CURRENT_WRITE;
        memcpy(controller->current_plane, data, length);
    } else {
        assert(controller->selected_command == PJ_DISPLAY_PARTIAL_PREVIOUS_RAM_COMMAND);
        controller->events[controller->event_count++] = TEST_EVENT_PREVIOUS_WRITE;
        memcpy(controller->previous_plane, data, length);
    }
    return fake_operation(controller);
}

static int fake_activate(void *opaque)
{
    fake_partial_controller_t *controller = opaque;
    controller->events[controller->event_count++] = TEST_EVENT_ACTIVATE;
    memcpy(controller->activated_current, controller->current_plane,
           sizeof(controller->current_plane));
    memcpy(controller->activated_previous, controller->previous_plane,
           sizeof(controller->previous_plane));
    controller->activation_count++;
    return fake_operation(controller);
}

static pj_display_partial_plane_io_t fake_io(fake_partial_controller_t *controller)
{
    return (pj_display_partial_plane_io_t) {
        .context = controller,
        .position = fake_position,
        .command = fake_command,
        .write = fake_write,
        .activate = fake_activate,
    };
}

static void assert_partial_event_sequence(const fake_partial_controller_t *controller,
                                          size_t offset)
{
    const test_event_t expected[] = {
        TEST_EVENT_POSITION,
        TEST_EVENT_CURRENT_COMMAND,
        TEST_EVENT_CURRENT_WRITE,
        TEST_EVENT_ACTIVATE,
        TEST_EVENT_POSITION,
        TEST_EVENT_PREVIOUS_COMMAND,
        TEST_EVENT_PREVIOUS_WRITE,
    };
    assert(controller->event_count >= offset + sizeof(expected) / sizeof(expected[0]));
    assert(memcmp(&controller->events[offset], expected, sizeof(expected)) == 0);
}

static void test_partial_plane_commit_advances_previous_after_activation(void)
{
    fake_partial_controller_t controller;
    memset(&controller, 0, sizeof(controller));
    memset(controller.current_plane, 0xff, sizeof(controller.current_plane));
    memset(controller.previous_plane, 0xff, sizeof(controller.previous_plane));
    pj_display_partial_plane_io_t io = fake_io(&controller);

    const uint8_t first[] = {0xf0, 0x0f, 0xaa, 0x55};
    assert(pj_display_refresh_commit_partial_planes(
               &io, first, sizeof(first)) == 0);
    assert_partial_event_sequence(&controller, 0);
    assert(controller.activation_count == 1);
    assert(memcmp(controller.activated_previous,
                  (uint8_t[]) {0xff, 0xff, 0xff, 0xff}, sizeof(first)) == 0);
    assert(memcmp(controller.activated_current, first, sizeof(first)) == 0);
    assert(memcmp(controller.previous_plane, first, sizeof(first)) == 0);

    const uint8_t second[] = {0x0f, 0xf0, 0x55, 0xaa};
    assert(pj_display_refresh_commit_partial_planes(
               &io, second, sizeof(second)) == 0);
    assert_partial_event_sequence(&controller, 7);
    assert(controller.activation_count == 2);
    assert(memcmp(controller.activated_previous, first, sizeof(first)) == 0);
    assert(memcmp(controller.activated_current, second, sizeof(second)) == 0);
    assert(memcmp(controller.previous_plane, second, sizeof(second)) == 0);
}

static void test_partial_plane_commit_fails_before_advancing_previous(void)
{
    fake_partial_controller_t controller;
    memset(&controller, 0, sizeof(controller));
    memset(controller.previous_plane, 0xa5, sizeof(controller.previous_plane));
    controller.fail_operation = 4;
    pj_display_partial_plane_io_t io = fake_io(&controller);
    const uint8_t current[] = {1, 2, 3, 4};

    assert(pj_display_refresh_commit_partial_planes(
               &io, current, sizeof(current)) == 73);
    assert(controller.event_count == 4);
    assert(controller.events[3] == TEST_EVENT_ACTIVATE);
    assert(memcmp(controller.previous_plane,
                  (uint8_t[]) {0xa5, 0xa5, 0xa5, 0xa5}, sizeof(current)) == 0);

    memset(&controller, 0, sizeof(controller));
    memset(controller.previous_plane, 0x5a, sizeof(controller.previous_plane));
    controller.fail_operation = 6;
    io = fake_io(&controller);
    assert(pj_display_refresh_commit_partial_planes(
               &io, current, sizeof(current)) == 73);
    assert(controller.event_count == 6);
    assert(controller.events[4] == TEST_EVENT_POSITION);
    assert(controller.events[5] == TEST_EVENT_PREVIOUS_COMMAND);
    assert(memcmp(controller.previous_plane,
                  (uint8_t[]) {0x5a, 0x5a, 0x5a, 0x5a}, sizeof(current)) == 0);
}

int main(void)
{
    test_region_clips_and_aligns();
    test_identical_partial_is_noop();
    test_changed_bounds_are_tight_and_byte_aligned();
    test_invalid_shadow_and_cadence_promote_to_full();
    test_partial_shadow_copy_does_not_hide_out_of_region_change();
    test_failed_refresh_records_error_without_advancing_cadence();
    test_successful_refresh_atomically_updates_shadow_and_metrics();
    test_partial_refresh_cannot_establish_an_invalid_shadow();
    test_partial_plane_commit_advances_previous_after_activation();
    test_partial_plane_commit_fails_before_advancing_previous();
    puts("display refresh tests passed");
    return 0;
}
