#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t samples;
    uint64_t started_ms;
} pj_touch_candidate_t;

void pj_touch_candidate_reset(pj_touch_candidate_t *candidate);
int pj_touch_candidate_update(pj_touch_candidate_t *candidate,
                              uint16_t x, uint16_t y, uint64_t now_ms,
                              uint16_t move_tolerance,
                              uint16_t required_samples,
                              uint64_t *started_ms);

#ifdef __cplusplus
}
#endif
