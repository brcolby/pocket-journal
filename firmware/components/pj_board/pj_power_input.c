#include "pj_power_input.h"

#include <string.h>

static uint8_t normalized_level(int level)
{
    return level != 0 ? 1U : 0U;
}

void pj_power_input_init(pj_power_input_t *input, int level, uint32_t now_ms)
{
    if (input == NULL) {
        return;
    }
    memset(input, 0, sizeof(*input));
    input->raw_level = normalized_level(level);
    input->stable_level = input->raw_level;
    input->raw_changed_ms = now_ms;
    input->armed = input->stable_level;
    input->initialized = 1U;
}

int pj_power_input_update(pj_power_input_t *input, int level, uint32_t now_ms)
{
    if (input == NULL) {
        return 0;
    }
    if (!input->initialized) {
        pj_power_input_init(input, level, now_ms);
        return 0;
    }

    uint8_t raw_level = normalized_level(level);
    if (raw_level != input->raw_level) {
        input->raw_level = raw_level;
        input->raw_changed_ms = now_ms;
    }
    if (input->stable_level == input->raw_level ||
        (uint32_t)(now_ms - input->raw_changed_ms) < PJ_POWER_INPUT_DEBOUNCE_MS) {
        return 0;
    }

    input->stable_level = input->raw_level;
    if (input->stable_level != 0U) {
        input->armed = 1U;
        return 0;
    }
    if (!input->armed) {
        return 0;
    }
    input->armed = 0U;
    return 1;
}

int pj_power_input_is_released(const pj_power_input_t *input)
{
    return input != NULL && input->initialized && input->stable_level != 0U;
}
