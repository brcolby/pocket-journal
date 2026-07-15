#ifndef PJ_POWER_INPUT_H
#define PJ_POWER_INPUT_H

#include <stdint.h>

#define PJ_POWER_INPUT_DEBOUNCE_MS 30U

typedef struct {
    uint32_t raw_changed_ms;
    uint8_t raw_level;
    uint8_t stable_level;
    uint8_t armed;
    uint8_t initialized;
} pj_power_input_t;

void pj_power_input_init(pj_power_input_t *input, int level, uint32_t now_ms);

/* Returns one toggle for each debounced active-low press after a release. */
int pj_power_input_update(pj_power_input_t *input, int level, uint32_t now_ms);
int pj_power_input_is_released(const pj_power_input_t *input);

#endif
