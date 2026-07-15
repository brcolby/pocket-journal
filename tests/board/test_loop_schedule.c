#include "pj_loop_schedule.h"

#include <assert.h>
#include <stdio.h>

static void test_refresh_latency_does_not_stretch_second_deadlines(void)
{
    pj_loop_schedule_t schedule;
    pj_loop_schedule_init(&schedule, 100);

    pj_loop_schedule_events_t events = pj_loop_schedule_poll(&schedule, 100);
    assert(!events.second_due && !events.minute_due && events.status_due);
    assert(!pj_loop_schedule_poll(&schedule, 1099).second_due);
    assert(pj_loop_schedule_poll(&schedule, 1100).second_due);

    /* A 590 ms blocking display update still leaves the next tick at 2100. */
    assert(!pj_loop_schedule_poll(&schedule, 1690).second_due);
    assert(!pj_loop_schedule_poll(&schedule, 2099).second_due);
    assert(pj_loop_schedule_poll(&schedule, 2100).second_due);
}

static void test_late_poll_coalesces_missed_deadlines_without_drift(void)
{
    pj_loop_schedule_t schedule;
    pj_loop_schedule_init(&schedule, 100);
    (void)pj_loop_schedule_poll(&schedule, 100);

    assert(pj_loop_schedule_poll(&schedule, 4600).second_due);
    assert(schedule.next_second_ms == 5100);
    assert(!pj_loop_schedule_poll(&schedule, 5099).second_due);
    assert(pj_loop_schedule_poll(&schedule, 5100).second_due);
    assert(schedule.next_second_ms == 6100);
}

static void test_fifty_ms_polling_emits_exactly_one_tick_per_second(void)
{
    pj_loop_schedule_t schedule;
    pj_loop_schedule_init(&schedule, 0);

    unsigned second_ticks = 0;
    for (uint64_t now_ms = 0; now_ms <= 10000; now_ms += 50) {
        if (pj_loop_schedule_poll(&schedule, now_ms).second_due) {
            second_ticks++;
        }
    }

    assert(second_ticks == 10);
    assert(schedule.next_second_ms == 11000);
}

static void test_minute_and_status_deadlines_are_absolute(void)
{
    pj_loop_schedule_t schedule;
    pj_loop_schedule_init(&schedule, 100);
    (void)pj_loop_schedule_poll(&schedule, 100);

    pj_loop_schedule_events_t events = pj_loop_schedule_poll(&schedule, 60100);
    assert(events.second_due && events.minute_due && !events.status_due);
    events = pj_loop_schedule_poll(&schedule, 300100);
    assert(events.second_due && events.minute_due && events.status_due);
}

static void test_explicit_time_update_rebases_minute_tick(void)
{
    pj_loop_schedule_t schedule;
    pj_loop_schedule_init(&schedule, 100);
    pj_loop_schedule_rebase_minute(&schedule, 42000);

    assert(!pj_loop_schedule_poll(&schedule, 101999).minute_due);
    assert(pj_loop_schedule_poll(&schedule, 102000).minute_due);
}

int main(void)
{
    test_refresh_latency_does_not_stretch_second_deadlines();
    test_late_poll_coalesces_missed_deadlines_without_drift();
    test_fifty_ms_polling_emits_exactly_one_tick_per_second();
    test_minute_and_status_deadlines_are_absolute();
    test_explicit_time_update_rebases_minute_tick();
    puts("loop schedule tests passed");
    return 0;
}
