#pragma once

#include <stdint.h>

typedef struct {
    uint64_t next_second_ms;
    uint64_t next_minute_ms;
    uint64_t next_status_ms;
} pj_loop_schedule_t;

typedef struct {
    int second_due;
    int minute_due;
    int status_due;
} pj_loop_schedule_events_t;

void pj_loop_schedule_init(pj_loop_schedule_t *schedule, uint64_t now_ms);
pj_loop_schedule_events_t pj_loop_schedule_poll(
    pj_loop_schedule_t *schedule, uint64_t now_ms);
void pj_loop_schedule_rebase_minute(pj_loop_schedule_t *schedule,
                                    uint64_t now_ms);
