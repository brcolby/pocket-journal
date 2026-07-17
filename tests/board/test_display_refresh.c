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

static void test_region_alignment_exhaustive_x_sweep(void)
{
    for (int x = -PJ_DISPLAY_WIDTH; x <= PJ_DISPLAY_WIDTH * 2; x++) {
        for (int width = 1; width <= PJ_DISPLAY_WIDTH * 2; width++) {
            const pj_ui_dirty_region_t dirty = {
                .x = x,
                .y = 37,
                .width = width,
                .height = 19,
                .partial = 1,
            };
            pj_ui_dirty_region_t normalized;
            const int64_t requested_x1 = (int64_t)x + width;
            const int intersects = requested_x1 > 0 && x < PJ_DISPLAY_WIDTH;

            assert(pj_display_refresh_region_normalize(
                       &dirty, 1, &normalized) == intersects);
            if (!intersects) {
                continue;
            }

            const int clipped_x0 = x < 0 ? 0 : x;
            const int clipped_x1 = requested_x1 > PJ_DISPLAY_WIDTH
                ? PJ_DISPLAY_WIDTH : (int)requested_x1;
            assert(normalized.x >= 0);
            assert(normalized.x % 8 == 0);
            assert(normalized.width > 0);
            assert(normalized.width % 8 == 0);
            assert(normalized.x + normalized.width <= PJ_DISPLAY_WIDTH);
            assert(normalized.x <= clipped_x0);
            assert(normalized.x + normalized.width >= clipped_x1);
            assert(normalized.y == dirty.y);
            assert(normalized.height == dirty.height);
        }
    }
}

static void test_region_y_is_pixel_granular_and_clipped(void)
{
    for (int y = -PJ_DISPLAY_HEIGHT; y <= PJ_DISPLAY_HEIGHT * 2; y++) {
        for (int height = 1; height <= PJ_DISPLAY_HEIGHT * 2; height++) {
            const pj_ui_dirty_region_t dirty = {
                .x = 17,
                .y = y,
                .width = 1,
                .height = height,
                .partial = 1,
            };
            pj_ui_dirty_region_t normalized;
            const int64_t requested_y1 = (int64_t)y + height;
            const int intersects = requested_y1 > 0 && y < PJ_DISPLAY_HEIGHT;

            assert(pj_display_refresh_region_normalize(
                       &dirty, 1, &normalized) == intersects);
            if (!intersects) {
                continue;
            }

            const int clipped_y0 = y < 0 ? 0 : y;
            const int clipped_y1 = requested_y1 > PJ_DISPLAY_HEIGHT
                ? PJ_DISPLAY_HEIGHT : (int)requested_y1;
            assert(normalized.y == clipped_y0);
            assert(normalized.height == clipped_y1 - clipped_y0);
            assert(normalized.x == 16);
            assert(normalized.width == 8);
        }
    }
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
    assert(plan.transfer_bytes == 6 * PJ_DISPLAY_PARTIAL_WIRE_WRITES);
}

static void test_single_pixel_patch_geometry_exhaustive(void)
{
    pj_framebuffer_t framebuffer = {0};
    const pj_framebuffer_t shadow = {0};
    pj_display_refresh_policy_t policy;
    pj_display_refresh_policy_init(&policy, 0);

    for (int y = 0; y < PJ_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
            const pj_ui_dirty_region_t dirty = {
                .x = x, .y = y, .width = 1, .height = 1, .partial = 1,
            };
            set_pixel(&framebuffer, x, y);
            pj_display_refresh_plan_t plan = pj_display_refresh_plan(
                &policy, &framebuffer, &shadow, 1, &dirty);
            assert(plan.kind == PJ_DISPLAY_REFRESH_PARTIAL);
            assert(plan.region.x == (x & ~7));
            assert(plan.region.y == y);
            assert(plan.region.width == 8);
            assert(plan.region.height == 1);
            assert(plan.changed_pixels == 1);
            assert(plan.transfer_bytes == PJ_DISPLAY_PARTIAL_WIRE_WRITES);
            framebuffer.pixels[((size_t)y * PJ_DISPLAY_WIDTH + (size_t)x) >> 3u] = 0;
        }
    }
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
        .transfer_bytes = 2 * PJ_DISPLAY_PARTIAL_WIRE_WRITES,
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
        .transfer_bytes = 2 * PJ_DISPLAY_PARTIAL_WIRE_WRITES,
        .requested_partial = 1,
    };
    assert(pj_display_refresh_complete(&policy, &shadow, &shadow_valid,
                                       &framebuffer, &partial, 1, 200, 100));
    assert(shadow_valid);
    assert(get_pixel(&shadow, 1, 0));
    assert(policy.metrics.applied_partial == 1);
    assert(policy.metrics.transfer_bytes == 2 * PJ_DISPLAY_PARTIAL_WIRE_WRITES);
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
        .transfer_bytes = 2 * PJ_DISPLAY_PARTIAL_WIRE_WRITES,
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

#define TEST_PARTIAL_BYTES 8u
#define TEST_ACTIVATION_CAPACITY 2u
#define TEST_PARTIAL_OPERATION_COUNT 10u

typedef struct {
    test_event_t events[32];
    size_t event_count;
    uint8_t selected_command;
    uint8_t current_plane[TEST_PARTIAL_BYTES];
    uint8_t previous_plane[TEST_PARTIAL_BYTES];
    uint8_t activated_current[TEST_ACTIVATION_CAPACITY][TEST_PARTIAL_BYTES];
    uint8_t activated_previous[TEST_ACTIVATION_CAPACITY][TEST_PARTIAL_BYTES];
    size_t activation_count;
    size_t activation_calls;
    size_t command_count;
    size_t write_count;
    size_t operation_count;
    size_t fail_operation;
    uint8_t unexpected_command;
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
    controller->events[controller->event_count++] =
        command == PJ_DISPLAY_PARTIAL_CURRENT_RAM_COMMAND ?
            TEST_EVENT_CURRENT_COMMAND : TEST_EVENT_PREVIOUS_COMMAND;
    controller->command_count++;
    if (command != PJ_DISPLAY_PARTIAL_CURRENT_RAM_COMMAND &&
        command != PJ_DISPLAY_PARTIAL_PREVIOUS_RAM_COMMAND) {
        controller->unexpected_command = command;
    }
    int result = fake_operation(controller);
    if (result == 0) {
        controller->selected_command = command;
    }
    return result;
}

static int fake_write(void *opaque, const uint8_t *data, size_t length)
{
    fake_partial_controller_t *controller = opaque;
    assert(length == sizeof(controller->current_plane));
    assert(controller->selected_command == PJ_DISPLAY_PARTIAL_CURRENT_RAM_COMMAND ||
           controller->selected_command == PJ_DISPLAY_PARTIAL_PREVIOUS_RAM_COMMAND);
    controller->events[controller->event_count++] =
        controller->selected_command == PJ_DISPLAY_PARTIAL_CURRENT_RAM_COMMAND ?
            TEST_EVENT_CURRENT_WRITE : TEST_EVENT_PREVIOUS_WRITE;
    controller->write_count++;
    int result = fake_operation(controller);
    if (result == 0) {
        memcpy(controller->selected_command == PJ_DISPLAY_PARTIAL_CURRENT_RAM_COMMAND ?
                   controller->current_plane : controller->previous_plane,
               data, length);
    }
    return result;
}

static int fake_activate(void *opaque)
{
    fake_partial_controller_t *controller = opaque;
    controller->events[controller->event_count++] = TEST_EVENT_ACTIVATE;
    controller->activation_calls++;
    int result = fake_operation(controller);
    if (result == 0) {
        assert(controller->activation_count < TEST_ACTIVATION_CAPACITY);
        memcpy(controller->activated_current[controller->activation_count],
               controller->current_plane, sizeof(controller->current_plane));
        memcpy(controller->activated_previous[controller->activation_count],
               controller->previous_plane, sizeof(controller->previous_plane));
        controller->activation_count++;
    }
    return result;
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

static const test_event_t expected_partial_events[TEST_PARTIAL_OPERATION_COUNT] = {
    TEST_EVENT_POSITION,
    TEST_EVENT_CURRENT_COMMAND,
    TEST_EVENT_CURRENT_WRITE,
    TEST_EVENT_ACTIVATE,
    TEST_EVENT_POSITION,
    TEST_EVENT_PREVIOUS_COMMAND,
    TEST_EVENT_PREVIOUS_WRITE,
    TEST_EVENT_POSITION,
    TEST_EVENT_CURRENT_COMMAND,
    TEST_EVENT_CURRENT_WRITE,
};

static void assert_partial_event_sequence(const fake_partial_controller_t *controller,
                                          size_t offset)
{
    assert(controller->event_count >= offset + TEST_PARTIAL_OPERATION_COUNT);
    assert(memcmp(&controller->events[offset], expected_partial_events,
                  sizeof(expected_partial_events)) == 0);
}

static void test_partial_commit_presents_then_advances_both_ram_baselines(void)
{
    fake_partial_controller_t controller;
    memset(&controller, 0, sizeof(controller));
    memset(controller.current_plane, 0xff, sizeof(controller.current_plane));
    memset(controller.previous_plane, 0xff, sizeof(controller.previous_plane));
    pj_display_partial_plane_io_t io = fake_io(&controller);

    const uint8_t first[TEST_PARTIAL_BYTES] = {
        0xf0, 0x0f, 0xaa, 0x55, 0x81, 0x42, 0x24, 0x18,
    };
    assert(pj_display_refresh_commit_partial_planes(
               &io, first, sizeof(first)) == 0);
    assert_partial_event_sequence(&controller, 0);
    assert(controller.activation_count == 1);
    assert(controller.activation_calls == 1);
    assert(controller.command_count == 3);
    assert(controller.write_count == 3);
    assert(controller.unexpected_command == 0);
    assert(controller.selected_command == PJ_DISPLAY_PARTIAL_CURRENT_RAM_COMMAND);
    assert(memcmp(controller.activated_current[0], first, sizeof(first)) == 0);
    assert(memcmp(controller.activated_previous[0],
                  (const uint8_t[TEST_PARTIAL_BYTES]) {
                      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                  }, sizeof(first)) == 0);
    assert(memcmp(controller.current_plane, first, sizeof(first)) == 0);
    assert(memcmp(controller.previous_plane, first, sizeof(first)) == 0);

    const uint8_t second[TEST_PARTIAL_BYTES] = {
        0x0f, 0xf0, 0x55, 0xaa, 0x18, 0x24, 0x42, 0x81,
    };
    assert(pj_display_refresh_commit_partial_planes(
               &io, second, sizeof(second)) == 0);
    assert_partial_event_sequence(&controller, 10);
    assert(controller.activation_count == 2);
    assert(controller.activation_calls == 2);
    assert(controller.command_count == 6);
    assert(controller.write_count == 6);
    assert(controller.unexpected_command == 0);
    assert(controller.selected_command == PJ_DISPLAY_PARTIAL_CURRENT_RAM_COMMAND);
    assert(memcmp(controller.activated_current[0], first, sizeof(first)) == 0);
    assert(memcmp(controller.activated_current[1], second, sizeof(second)) == 0);
    assert(memcmp(controller.activated_previous[1], first, sizeof(first)) == 0);
    assert(memcmp(controller.current_plane, second, sizeof(second)) == 0);
    assert(memcmp(controller.previous_plane, second, sizeof(second)) == 0);
}

static void test_partial_commit_propagates_each_failure(void)
{
    const uint8_t baseline[TEST_PARTIAL_BYTES] = {
        0xa5, 0x5a, 0xc3, 0x3c, 0x96, 0x69, 0xf0, 0x0f,
    };
    const uint8_t current[TEST_PARTIAL_BYTES] = {1, 2, 3, 4, 5, 6, 7, 8};
    for (size_t failed_operation = 1;
         failed_operation <= TEST_PARTIAL_OPERATION_COUNT;
         failed_operation++) {
        fake_partial_controller_t controller;
        memset(&controller, 0, sizeof(controller));
        memcpy(controller.current_plane, baseline, sizeof(baseline));
        memcpy(controller.previous_plane, baseline, sizeof(baseline));
        controller.fail_operation = failed_operation;
        pj_display_partial_plane_io_t io = fake_io(&controller);

        assert(pj_display_refresh_commit_partial_planes(
                   &io, current, sizeof(current)) == 73);
        assert(controller.operation_count == failed_operation);
        assert(controller.event_count == failed_operation);
        assert(memcmp(controller.events, expected_partial_events,
                      failed_operation * sizeof(controller.events[0])) == 0);
        assert(controller.command_count ==
               (failed_operation >= 2u) + (failed_operation >= 6u) +
                   (failed_operation >= 9u));
        assert(controller.write_count ==
               (failed_operation >= 3u) + (failed_operation >= 7u) +
                   (failed_operation >= 10u));
        assert(controller.activation_calls == (failed_operation >= 4u ? 1u : 0u));
        assert(controller.activation_count == (failed_operation > 4 ? 1u : 0u));
        assert(controller.unexpected_command == 0);
        assert(memcmp(controller.current_plane,
                      failed_operation >= 4u ? current : baseline,
                      sizeof(current)) == 0);
        assert(memcmp(controller.previous_plane,
                      failed_operation >= 8u ? current : baseline,
                      sizeof(current)) == 0);
        if (controller.activation_count == 1u) {
            assert(memcmp(controller.activated_current[0], current,
                          sizeof(current)) == 0);
            assert(memcmp(controller.activated_previous[0], baseline,
                          sizeof(baseline)) == 0);
        }
    }
}

static void test_partial_repeated_digit_transitions(void)
{
    static const uint8_t digits[10][TEST_PARTIAL_BYTES] = {
        {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e, 0x00},
        {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e, 0x00},
        {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f, 0x00},
        {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e, 0x00},
        {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02, 0x00},
        {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e, 0x00},
        {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e, 0x00},
        {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08, 0x00},
        {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e, 0x00},
        {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e, 0x00},
    };

    for (size_t first = 0; first < 10; first++) {
        for (size_t second = 0; second < 10; second++) {
            fake_partial_controller_t controller;
            memset(&controller, 0, sizeof(controller));
            pj_display_partial_plane_io_t io = fake_io(&controller);

            assert(pj_display_refresh_commit_partial_planes(
                       &io, digits[first], sizeof(digits[first])) == 0);
            assert(pj_display_refresh_commit_partial_planes(
                       &io, digits[second], sizeof(digits[second])) == 0);
            assert_partial_event_sequence(&controller, 0);
            assert_partial_event_sequence(&controller, 10);
            assert(controller.event_count == 20);
            assert(controller.command_count == 6);
            assert(controller.write_count == 6);
            assert(controller.activation_count == 2);
            assert(controller.activation_calls == 2);
            assert(controller.unexpected_command == 0);
            assert(memcmp(controller.activated_current[0], digits[first],
                          TEST_PARTIAL_BYTES) == 0);
            assert(memcmp(controller.activated_current[1], digits[second],
                          TEST_PARTIAL_BYTES) == 0);
            assert(memcmp(controller.activated_previous[1], digits[first],
                          TEST_PARTIAL_BYTES) == 0);
            assert(memcmp(controller.current_plane, digits[second],
                          TEST_PARTIAL_BYTES) == 0);
            assert(memcmp(controller.previous_plane, digits[second],
                          TEST_PARTIAL_BYTES) == 0);
            assert(controller.selected_command ==
                   PJ_DISPLAY_PARTIAL_CURRENT_RAM_COMMAND);
        }
    }
}

static void test_cleanup_is_deferred_only_during_seconds_cadence(void)
{
    pj_display_refresh_policy_t policy;
    pj_display_refresh_policy_init(&policy, 3);
    policy.partial_since_full = 2;
    pj_framebuffer_t shadow = {0};
    pj_framebuffer_t frame = {0};
    set_pixel(&frame, 17, 23);
    const pj_ui_dirty_region_t dirty = {
        .x = 17, .y = 23, .width = 1, .height = 1, .partial = 1,
    };

    pj_display_refresh_set_cleanup_deferred(&policy, 1);
    pj_display_refresh_plan_t plan = pj_display_refresh_plan(
        &policy, &frame, &shadow, 1, &dirty);
    assert(plan.kind == PJ_DISPLAY_REFRESH_PARTIAL);
    assert(plan.deferred_cleanup);
    assert(!plan.promoted_to_full);
    assert(pj_display_refresh_complete(&policy, &shadow, &(int){1},
                                       &frame, &plan, 1, 600000, 590000));
    assert(pj_display_refresh_cleanup_pending(&policy));
    assert(policy.metrics.cleanup_deferrals == 1);

    memset(&shadow, 0, sizeof(shadow));
    pj_display_refresh_set_cleanup_deferred(&policy, 0);
    plan = pj_display_refresh_plan(&policy, &frame, &shadow, 1, &dirty);
    assert(plan.kind == PJ_DISPLAY_REFRESH_FULL);
    assert(plan.promoted_to_full);
    int shadow_valid = 1;
    assert(pj_display_refresh_complete(&policy, &shadow, &shadow_valid,
                                       &frame, &plan, 1, 1800000, 1790000));
    assert(!pj_display_refresh_cleanup_pending(&policy));
}

int main(void)
{
    test_region_clips_and_aligns();
    test_region_alignment_exhaustive_x_sweep();
    test_region_y_is_pixel_granular_and_clipped();
    test_identical_partial_is_noop();
    test_changed_bounds_are_tight_and_byte_aligned();
    test_single_pixel_patch_geometry_exhaustive();
    test_invalid_shadow_and_cadence_promote_to_full();
    test_partial_shadow_copy_does_not_hide_out_of_region_change();
    test_failed_refresh_records_error_without_advancing_cadence();
    test_successful_refresh_atomically_updates_shadow_and_metrics();
    test_partial_refresh_cannot_establish_an_invalid_shadow();
    test_partial_commit_presents_then_advances_both_ram_baselines();
    test_partial_commit_propagates_each_failure();
    test_partial_repeated_digit_transitions();
    test_cleanup_is_deferred_only_during_seconds_cadence();
    puts("display refresh tests passed");
    return 0;
}
