#include "pj_aux_input.h"

#include <string.h>

void pj_aux_input_init(pj_aux_input_t *input, int initial_level, uint32_t now_ms)
{
    if (input != NULL) {
        memset(input, 0, sizeof(*input));
        input->raw_level = initial_level;
        input->stable_level = initial_level;
        input->raw_changed_ms = now_ms;
        if (initial_level == 0) {
            input->pressed = 1;
            input->press_started_ms = now_ms;
        }
    }
}

void pj_aux_input_resume_pressed(pj_aux_input_t *input, uint32_t now_ms)
{
    pj_aux_input_init(input, 0, now_ms);
    if (input != NULL) {
        input->pressed = 1;
        input->press_started_ms = now_ms;
    }
}

static pj_aux_gesture_t handle_press(pj_aux_input_t *input, uint32_t now_ms)
{
    if (input->pressed) {
        return PJ_AUX_GESTURE_NONE;
    }

    pj_aux_gesture_t expired = PJ_AUX_GESTURE_NONE;
    if (input->pending_short) {
        if ((uint32_t)(now_ms - input->first_release_ms) <= PJ_AUX_DOUBLE_CLICK_MS) {
            input->second_press = 1;
        } else {
            input->pending_short = 0;
            expired = PJ_AUX_GESTURE_SHORT;
        }
    }

    input->pressed = 1;
    input->long_emitted = 0;
    input->press_started_ms = input->raw_changed_ms;
    return expired;
}

static pj_aux_gesture_t handle_release(pj_aux_input_t *input, uint32_t now_ms)
{
    if (!input->pressed) {
        return PJ_AUX_GESTURE_NONE;
    }

    input->pressed = 0;
    if (input->long_emitted) {
        input->long_emitted = 0;
        return PJ_AUX_GESTURE_NONE;
    }
    if (input->second_press) {
        input->pending_short = 0;
        input->second_press = 0;
        return PJ_AUX_GESTURE_DOUBLE;
    }

    input->pending_short = 1;
    input->first_release_ms = now_ms;
    return PJ_AUX_GESTURE_NONE;
}

static pj_aux_gesture_t poll_long_press(pj_aux_input_t *input, uint32_t now_ms)
{
    if (!input->pressed || input->long_emitted ||
        (uint32_t)(now_ms - input->press_started_ms) < PJ_AUX_LONG_PRESS_MS) {
        return PJ_AUX_GESTURE_NONE;
    }

    input->long_emitted = 1;
    input->pending_short = 0;
    input->second_press = 0;
    return PJ_AUX_GESTURE_LONG;
}

static pj_aux_gesture_t poll_pending_short(pj_aux_input_t *input, uint32_t now_ms)
{
    if (!input->pending_short) {
        return PJ_AUX_GESTURE_NONE;
    }
    if ((uint32_t)(now_ms - input->first_release_ms) <= PJ_AUX_DOUBLE_CLICK_MS) {
        return PJ_AUX_GESTURE_NONE;
    }

    input->pending_short = 0;
    input->second_press = 0;
    return PJ_AUX_GESTURE_SHORT;
}

pj_aux_gesture_t pj_aux_input_update(pj_aux_input_t *input, int raw_level, uint32_t now_ms)
{
    if (input == NULL) {
        return PJ_AUX_GESTURE_NONE;
    }

    if (raw_level != input->raw_level) {
        input->raw_level = raw_level;
        input->raw_changed_ms = now_ms;
    }

    pj_aux_gesture_t gesture = PJ_AUX_GESTURE_NONE;
    if (input->raw_level != input->stable_level &&
        (uint32_t)(now_ms - input->raw_changed_ms) >= PJ_AUX_DEBOUNCE_MS) {
        input->stable_level = input->raw_level;
        gesture = input->stable_level == 0 ? handle_press(input, now_ms) : handle_release(input, now_ms);
    }

    if (gesture != PJ_AUX_GESTURE_NONE) {
        return gesture;
    }
    gesture = poll_long_press(input, now_ms);
    return gesture != PJ_AUX_GESTURE_NONE ? gesture : poll_pending_short(input, now_ms);
}

int pj_aux_input_is_released(const pj_aux_input_t *input)
{
    return input != NULL && input->raw_level != 0 &&
        input->stable_level != 0 && !input->pressed;
}
