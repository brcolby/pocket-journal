#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_AUX_DEBOUNCE_MS 30U
#define PJ_AUX_LONG_PRESS_MS 500U
#define PJ_AUX_DOUBLE_CLICK_MS 350U

typedef enum {
    PJ_AUX_GESTURE_NONE = 0,
    PJ_AUX_GESTURE_SHORT,
    PJ_AUX_GESTURE_LONG,
    PJ_AUX_GESTURE_DOUBLE,
} pj_aux_gesture_t;

typedef struct {
    uint32_t raw_changed_ms;
    uint32_t press_started_ms;
    uint32_t first_release_ms;
    int raw_level;
    int stable_level;
    int pressed;
    int pending_short;
    int second_press;
} pj_aux_input_t;

void pj_aux_input_init(pj_aux_input_t *input, int initial_level, uint32_t now_ms);
void pj_aux_input_resume_pressed(pj_aux_input_t *input, uint32_t now_ms);
pj_aux_gesture_t pj_aux_input_update(pj_aux_input_t *input, int raw_level, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
