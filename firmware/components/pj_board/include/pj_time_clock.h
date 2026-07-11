#ifndef PJ_TIME_CLOCK_H
#define PJ_TIME_CLOCK_H

#include <stdint.h>

#include "pj_time_model.h"

typedef struct {
    int32_t local_day;
    uint32_t local_second;
    int64_t wall_ms;
    uint64_t monotonic_ms;
    uint8_t valid;
} pj_time_clock_anchor_t;

int pj_time_clock_civil_valid(int year, int month, int day,
                              int hour, int minute, int second);
int pj_time_clock_civil_from_day(int32_t local_day,
                                 int *year, int *month, int *day);
int pj_time_clock_anchor_set(pj_time_clock_anchor_t *anchor,
                             int year, int month, int day,
                             int hour, int minute, int second,
                             uint64_t monotonic_ms);
int pj_time_clock_snapshot(const pj_time_clock_anchor_t *anchor,
                           uint32_t boot_id, uint64_t monotonic_ms,
                           pj_time_clock_t *clock);

#endif
