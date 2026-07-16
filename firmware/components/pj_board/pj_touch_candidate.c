#include "pj_touch_candidate.h"

#include <stddef.h>

void pj_touch_candidate_reset(pj_touch_candidate_t *candidate)
{
    if (candidate != NULL) {
        *candidate = (pj_touch_candidate_t) {0};
    }
}

static int coordinate_delta(uint16_t left, uint16_t right)
{
    int delta = (int)left - (int)right;
    return delta < 0 ? -delta : delta;
}

int pj_touch_candidate_update(pj_touch_candidate_t *candidate,
                              uint16_t x, uint16_t y, uint64_t now_ms,
                              uint16_t move_tolerance,
                              uint16_t required_samples,
                              uint64_t *started_ms)
{
    if (candidate == NULL || required_samples == 0U) {
        return 0;
    }
    if (candidate->samples == 0U ||
        coordinate_delta(x, candidate->x) > move_tolerance ||
        coordinate_delta(y, candidate->y) > move_tolerance) {
        candidate->samples = 1U;
        candidate->started_ms = now_ms;
    } else if (candidate->samples < required_samples) {
        candidate->samples++;
    }
    candidate->x = x;
    candidate->y = y;
    if (candidate->samples < required_samples) {
        return 0;
    }
    if (started_ms != NULL) {
        *started_ms = candidate->started_ms;
    }
    return 1;
}
