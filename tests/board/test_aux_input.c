#include "pj_aux_input.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static pj_aux_gesture_t settle_level(pj_aux_input_t *input, int level, uint32_t changed_at)
{
    pj_aux_gesture_t on_change = pj_aux_input_update(input, level, changed_at);
    pj_aux_gesture_t on_stable = pj_aux_input_update(input, level, changed_at + PJ_AUX_DEBOUNCE_MS);
    assert(on_change == PJ_AUX_GESTURE_NONE || on_stable == PJ_AUX_GESTURE_NONE);
    return on_change != PJ_AUX_GESTURE_NONE ? on_change : on_stable;
}

static uint32_t short_press(pj_aux_input_t *input, uint32_t pressed_at, uint32_t released_at)
{
    assert(settle_level(input, 0, pressed_at) == PJ_AUX_GESTURE_NONE);
    assert(settle_level(input, 1, released_at) == PJ_AUX_GESTURE_NONE);
    return released_at + PJ_AUX_DEBOUNCE_MS;
}

static void test_single_click_is_deferred(void)
{
    pj_aux_input_t input;
    pj_aux_input_init(&input, 1, 0);

    uint32_t released_at = short_press(&input, 100, 180);
    assert(pj_aux_input_update(&input, 1, released_at + PJ_AUX_DOUBLE_CLICK_MS) == PJ_AUX_GESTURE_NONE);
    assert(pj_aux_input_update(&input, 1, released_at + PJ_AUX_DOUBLE_CLICK_MS + 1U) == PJ_AUX_GESTURE_SHORT);
    assert(pj_aux_input_update(&input, 1, 1000) == PJ_AUX_GESTURE_NONE);
}

static void test_double_click_replaces_pending_short(void)
{
    pj_aux_input_t input;
    pj_aux_input_init(&input, 1, 0);

    short_press(&input, 100, 180);
    assert(settle_level(&input, 0, 300) == PJ_AUX_GESTURE_NONE);
    assert(settle_level(&input, 1, 380) == PJ_AUX_GESTURE_DOUBLE);
    assert(pj_aux_input_update(&input, 1, 1000) == PJ_AUX_GESTURE_NONE);
}

static void test_late_second_click_preserves_both_singles(void)
{
    pj_aux_input_t input;
    pj_aux_input_init(&input, 1, 0);

    short_press(&input, 100, 180);
    assert(settle_level(&input, 0, 600) == PJ_AUX_GESTURE_SHORT);
    assert(settle_level(&input, 1, 680) == PJ_AUX_GESTURE_NONE);
    uint32_t released_at = 680 + PJ_AUX_DEBOUNCE_MS;
    assert(pj_aux_input_update(&input, 1, released_at + PJ_AUX_DOUBLE_CLICK_MS + 1U) == PJ_AUX_GESTURE_SHORT);
}

static void test_long_press_is_not_a_double_click(void)
{
    pj_aux_input_t input;
    pj_aux_input_init(&input, 1, 0);

    assert(settle_level(&input, 0, 100) == PJ_AUX_GESTURE_NONE);
    assert(settle_level(&input, 1, 100 + PJ_AUX_LONG_PRESS_MS) == PJ_AUX_GESTURE_LONG);

    uint32_t first_release = short_press(&input, 2000, 2080);
    assert(settle_level(&input, 0, 2200) == PJ_AUX_GESTURE_NONE);
    assert(pj_aux_input_update(&input, 0, first_release + PJ_AUX_DOUBLE_CLICK_MS + 1U) == PJ_AUX_GESTURE_SHORT);
    assert(settle_level(&input, 1, 2200 + PJ_AUX_LONG_PRESS_MS) == PJ_AUX_GESTURE_LONG);
}

static void test_contact_bounce_does_not_drop_click(void)
{
    pj_aux_input_t input;
    pj_aux_input_init(&input, 1, 0);

    assert(pj_aux_input_update(&input, 0, 100) == PJ_AUX_GESTURE_NONE);
    assert(pj_aux_input_update(&input, 1, 105) == PJ_AUX_GESTURE_NONE);
    assert(pj_aux_input_update(&input, 0, 110) == PJ_AUX_GESTURE_NONE);
    assert(pj_aux_input_update(&input, 0, 139) == PJ_AUX_GESTURE_NONE);
    assert(pj_aux_input_update(&input, 0, 140) == PJ_AUX_GESTURE_NONE);

    assert(pj_aux_input_update(&input, 1, 200) == PJ_AUX_GESTURE_NONE);
    assert(pj_aux_input_update(&input, 0, 205) == PJ_AUX_GESTURE_NONE);
    assert(pj_aux_input_update(&input, 1, 210) == PJ_AUX_GESTURE_NONE);
    assert(pj_aux_input_update(&input, 1, 240) == PJ_AUX_GESTURE_NONE);
    assert(pj_aux_input_update(&input, 1, 591) == PJ_AUX_GESTURE_SHORT);
}

static void test_wake_press_survives_release_before_poll(void)
{
    pj_aux_input_t input;
    pj_aux_input_init(&input, 1, 0);

    pj_aux_input_resume_pressed(&input, 100);
    assert(pj_aux_input_update(&input, 1, 140) == PJ_AUX_GESTURE_NONE);
    assert(pj_aux_input_update(&input, 1, 170) == PJ_AUX_GESTURE_NONE);
    assert(settle_level(&input, 0, 260) == PJ_AUX_GESTURE_NONE);
    assert(settle_level(&input, 1, 330) == PJ_AUX_GESTURE_DOUBLE);
}

static void test_millisecond_counter_wrap(void)
{
    pj_aux_input_t input;
    uint32_t start = UINT32_MAX - 200U;
    pj_aux_input_init(&input, 1, start);

    uint32_t released_at = short_press(&input, start + 50U, start + 130U);
    assert(pj_aux_input_update(&input, 1, released_at + PJ_AUX_DOUBLE_CLICK_MS + 1U) == PJ_AUX_GESTURE_SHORT);
}

int main(void)
{
    test_single_click_is_deferred();
    test_double_click_replaces_pending_short();
    test_late_second_click_preserves_both_singles();
    test_long_press_is_not_a_double_click();
    test_contact_bounce_does_not_drop_click();
    test_wake_press_survives_release_before_poll();
    test_millisecond_counter_wrap();
    puts("aux input tests passed");
    return 0;
}
