#include "pj_loop_schedule.h"

#include <stddef.h>

#define PJ_LOOP_SECOND_MS UINT64_C(1000)
#define PJ_LOOP_MINUTE_MS UINT64_C(60000)
#define PJ_LOOP_STATUS_MS UINT64_C(300000)

static uint64_t deadline_after(uint64_t now_ms, uint64_t delay_ms)
{
    return UINT64_MAX - now_ms < delay_ms ? UINT64_MAX : now_ms + delay_ms;
}

static int consume_deadline(uint64_t now_ms, uint64_t period_ms,
                            uint64_t *deadline_ms)
{
    if (deadline_ms == NULL || now_ms < *deadline_ms) {
        return 0;
    }
    uint64_t periods = (now_ms - *deadline_ms) / period_ms + 1u;
    if (periods > (UINT64_MAX - *deadline_ms) / period_ms) {
        *deadline_ms = UINT64_MAX;
    } else {
        *deadline_ms += periods * period_ms;
    }
    return 1;
}

void pj_loop_schedule_init(pj_loop_schedule_t *schedule, uint64_t now_ms)
{
    if (schedule == NULL) {
        return;
    }
    schedule->next_second_ms = deadline_after(now_ms, PJ_LOOP_SECOND_MS);
    schedule->next_minute_ms = deadline_after(now_ms, PJ_LOOP_MINUTE_MS);
    schedule->next_status_ms = now_ms;
}

pj_loop_schedule_events_t pj_loop_schedule_poll(
    pj_loop_schedule_t *schedule, uint64_t now_ms)
{
    pj_loop_schedule_events_t events = {0};
    if (schedule == NULL) {
        return events;
    }
    events.second_due = consume_deadline(
        now_ms, PJ_LOOP_SECOND_MS, &schedule->next_second_ms);
    events.minute_due = consume_deadline(
        now_ms, PJ_LOOP_MINUTE_MS, &schedule->next_minute_ms);
    events.status_due = consume_deadline(
        now_ms, PJ_LOOP_STATUS_MS, &schedule->next_status_ms);
    return events;
}

void pj_loop_schedule_rebase_minute(pj_loop_schedule_t *schedule,
                                    uint64_t now_ms)
{
    if (schedule != NULL) {
        schedule->next_minute_ms = deadline_after(now_ms, PJ_LOOP_MINUTE_MS);
    }
}
