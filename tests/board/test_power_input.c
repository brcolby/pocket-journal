#include "pj_power_input.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_press_toggles_after_debounce(void)
{
    pj_power_input_t input;
    pj_power_input_init(&input, 1, 100U);
    assert(pj_power_input_is_released(&input));
    assert(!pj_power_input_update(&input, 0, 110U));
    assert(!pj_power_input_update(&input, 0, 139U));
    assert(pj_power_input_update(&input, 0, 140U));
    assert(!pj_power_input_is_released(&input));
    assert(!pj_power_input_update(&input, 0, 500U));
}

static void test_bounce_and_release_rearm(void)
{
    pj_power_input_t input;
    pj_power_input_init(&input, 1, 0U);
    assert(!pj_power_input_update(&input, 0, 10U));
    assert(!pj_power_input_update(&input, 1, 20U));
    assert(!pj_power_input_update(&input, 0, 30U));
    assert(pj_power_input_update(&input, 0, 60U));
    assert(!pj_power_input_update(&input, 1, 70U));
    assert(!pj_power_input_update(&input, 1, 100U));
    assert(pj_power_input_is_released(&input));
    assert(!pj_power_input_update(&input, 0, 101U));
    assert(pj_power_input_update(&input, 0, 131U));
}

static void test_held_at_boot_requires_release(void)
{
    pj_power_input_t input;
    pj_power_input_init(&input, 0, 0U);
    assert(!pj_power_input_update(&input, 0, 1000U));
    assert(!pj_power_input_update(&input, 1, 1010U));
    assert(!pj_power_input_update(&input, 1, 1040U));
    assert(!pj_power_input_update(&input, 0, 1050U));
    assert(pj_power_input_update(&input, 0, 1080U));
}

static void test_timestamp_wrap(void)
{
    pj_power_input_t input;
    pj_power_input_init(&input, 1, UINT32_MAX - 20U);
    assert(!pj_power_input_update(&input, 0, UINT32_MAX - 10U));
    assert(pj_power_input_update(&input, 0, 20U));
}

int main(void)
{
    test_press_toggles_after_debounce();
    test_bounce_and_release_rearm();
    test_held_at_boot_requires_release();
    test_timestamp_wrap();
    puts("power input tests passed");
    return 0;
}
