#include "pj_rtc_wake.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t registers[0x12];
    pj_rtc_wake_plan_t persisted;
    int operation;
    int fail_at;
    int mismatch_alarm;
} mock_io_t;

static int mock_read(void *context, uint8_t reg, uint8_t *data, size_t size)
{
    mock_io_t *mock = context;
    mock->operation++;
    if (mock->operation == mock->fail_at || reg + size > sizeof(mock->registers)) {
        return 0;
    }
    memcpy(data, &mock->registers[reg], size);
    if (mock->mismatch_alarm && reg == 0x0B && size > 0) {
        data[0] ^= 1u;
    }
    return 1;
}

static int mock_write(void *context, uint8_t reg, const uint8_t *data, size_t size)
{
    mock_io_t *mock = context;
    mock->operation++;
    if (mock->operation == mock->fail_at || reg + size > sizeof(mock->registers)) {
        return 0;
    }
    memcpy(&mock->registers[reg], data, size);
    return 1;
}

static int mock_persist(void *context, const pj_rtc_wake_plan_t *plan)
{
    mock_io_t *mock = context;
    mock->operation++;
    if (mock->operation == mock->fail_at) {
        return 0;
    }
    mock->persisted = *plan;
    return 1;
}

static pj_rtc_wake_io_t mock_ops(mock_io_t *mock)
{
    return (pj_rtc_wake_io_t) {
        .context = mock,
        .read = mock_read,
        .write = mock_write,
        .persist = mock_persist,
    };
}

static pj_time_clock_t civil_clock(int year, int month, int day,
                                   int hour, int minute, int second,
                                   uint64_t fraction_ms)
{
    pj_time_clock_anchor_t anchor;
    assert(pj_time_clock_anchor_set(&anchor, year, month, day, hour, minute, second, 0));
    pj_time_clock_t clock;
    assert(pj_time_clock_snapshot(&anchor, 1, fraction_ms, &clock));
    return clock;
}

static pj_time_state_t default_state(const pj_time_clock_t *clock)
{
    pj_time_state_t state;
    pj_time_state_defaults(&state, clock);
    return state;
}

static void test_plan_none_due_and_exact(void)
{
    pj_time_clock_t clock = civil_clock(2026, 7, 11, 6, 59, 59, 500);
    pj_time_state_t state = default_state(&clock);
    pj_rtc_wake_plan_t plan;
    assert(pj_rtc_wake_plan(&state, &clock, &plan));
    assert(plan.state == PJ_RTC_WAKE_NONE);
    assert(pj_time_alarm_configure(&state, 1, 7, 0, &clock));
    assert(pj_rtc_wake_plan(&state, &clock, &plan));
    assert(plan.state == PJ_RTC_WAKE_ARMED);
    assert(plan.delay_ms == 500);
    assert(plan.target_local_second == 7u * 3600u);
    assert(plan.source_mask == PJ_TIME_WAKE_ALARM);
    assert(!plan.checkpoint);
    assert(plan.alarm[0] == 0x00 && plan.alarm[1] == 0x00 &&
           plan.alarm[2] == 0x07 && plan.alarm[3] == 0x11 &&
           plan.alarm[4] == 0x80);
    assert(pj_rtc_wake_plan_valid(&plan));

    state.active_alert = (pj_time_alert_t) {
        .id = 1,
        .occurrence = 1,
        .source = PJ_TIME_ALERT_TIMER,
        .reason = PJ_TIME_ALERT_EXPIRED,
    };
    state.next_alert_id = 2;
    assert(pj_rtc_wake_plan(&state, &clock, &plan));
    assert(plan.state == PJ_RTC_WAKE_DUE);
}

static void test_month_checkpoint_and_calendar_boundaries(void)
{
    pj_rtc_wake_plan_t plan;
    pj_time_clock_t jan31 = civil_clock(2026, 1, 31, 12, 0, 0, 0);
    pj_time_state_t state = default_state(&jan31);
    assert(pj_time_timer_start(&state, 30ull * 24ull * 60ull * 60ull * 1000ull, &jan31));
    assert(pj_rtc_wake_plan(&state, &jan31, &plan));
    assert(plan.checkpoint);
    int year = 0;
    int month = 0;
    int day = 0;
    assert(pj_time_clock_civil_from_day(plan.target_local_day, &year, &month, &day));
    assert(year == 2026 && month == 2 && day == 1);
    assert(plan.target_local_second == 0);

    state = default_state(&jan31);
    assert(pj_time_timer_start(&state, 12ull * 60ull * 60ull * 1000ull, &jan31));
    assert(pj_rtc_wake_plan(&state, &jan31, &plan));
    assert(!plan.checkpoint);
    assert(pj_time_clock_civil_from_day(plan.target_local_day, &year, &month, &day));
    assert(year == 2026 && month == 2 && day == 1);

    pj_time_clock_t leap = civil_clock(2028, 2, 28, 23, 59, 59, 0);
    state = default_state(&leap);
    assert(pj_time_timer_start(&state, 1000, &leap));
    assert(pj_rtc_wake_plan(&state, &leap, &plan));
    assert(pj_time_clock_civil_from_day(plan.target_local_day, &year, &month, &day));
    assert(year == 2028 && month == 2 && day == 29);

    pj_time_clock_t year_end = civil_clock(2026, 12, 31, 23, 59, 59, 0);
    state = default_state(&year_end);
    assert(pj_time_timer_start(&state, 1000, &year_end));
    assert(pj_rtc_wake_plan(&state, &year_end, &plan));
    assert(pj_time_clock_civil_from_day(plan.target_local_day, &year, &month, &day));
    assert(year == 2027 && month == 1 && day == 1);
}

static void test_equal_deadlines_coalesce_sources(void)
{
    pj_time_clock_t clock = civil_clock(2026, 7, 11, 12, 0, 0, 0);
    pj_time_state_t state = default_state(&clock);
    assert(pj_time_timer_start(&state, 60000, &clock));
    assert(pj_time_interval_start(&state, 60000, 30000, &clock));
    pj_time_wake_deadline_t deadline;
    assert(pj_time_next_wake(&state, &clock, &deadline));
    assert(deadline.delay_ms == 60000);
    assert(deadline.source_mask == (PJ_TIME_WAKE_TIMER | PJ_TIME_WAKE_INTERVAL));
    assert(deadline.fingerprint != 0);
    pj_rtc_wake_plan_t plan;
    assert(pj_rtc_wake_plan(&state, &clock, &plan));
    assert(plan.source_mask == deadline.source_mask);
    assert(plan.deadline_fingerprint == deadline.fingerprint);
}

static void test_early_wake_clock_changes_and_duplicate_prevention(void)
{
    pj_time_clock_t start = civil_clock(2026, 7, 11, 12, 0, 0, 0);
    pj_time_state_t state = default_state(&start);
    assert(pj_time_timer_start(&state, 60000, &start));
    pj_rtc_wake_plan_t original;
    assert(pj_rtc_wake_plan(&state, &start, &original));

    pj_time_clock_t early = start;
    early.monotonic_ms += 30000;
    early.wall_utc_ms += 30000;
    early.local_second += 30;
    (void)pj_time_advance(&state, &early);
    pj_rtc_wake_plan_t replanned;
    assert(pj_rtc_wake_plan(&state, &early, &replanned));
    assert(replanned.token == original.token);
    assert(pj_time_active_alert(&state) == NULL);

    pj_time_clock_t forward = early;
    forward.wall_utc_ms += 3600000;
    forward.local_second += 3600;
    assert(pj_rtc_wake_plan(&state, &forward, &replanned));
    assert(replanned.token != original.token);
    pj_time_clock_t backward = early;
    backward.wall_utc_ms -= 3600000;
    backward.local_second -= 3600;
    assert(pj_rtc_wake_plan(&state, &backward, &replanned));
    assert(replanned.token != original.token);

    pj_time_clock_t crossed = early;
    crossed.monotonic_ms += 31000;
    crossed.wall_utc_ms += 31000;
    crossed.local_second += 31;
    assert(pj_time_advance(&state, &crossed));
    const pj_time_alert_t *alert = pj_time_active_alert(&state);
    assert(alert != NULL && alert->source == PJ_TIME_ALERT_TIMER);
    uint64_t alert_id = alert->id;
    crossed.monotonic_ms += 1000;
    crossed.wall_utc_ms += 1000;
    crossed.local_second += 1;
    (void)pj_time_advance(&state, &crossed);
    alert = pj_time_active_alert(&state);
    assert(alert != NULL && alert->id == alert_id);
}

static pj_rtc_wake_plan_t sample_plan(void)
{
    pj_time_clock_t clock = civil_clock(2026, 7, 11, 12, 0, 0, 0);
    pj_time_state_t state = default_state(&clock);
    assert(pj_time_timer_start(&state, 60000, &clock));
    pj_rtc_wake_plan_t plan;
    assert(pj_rtc_wake_plan(&state, &clock, &plan));
    return plan;
}

static void test_program_sequence_and_failures(void)
{
    pj_rtc_wake_plan_t plan = sample_plan();
    mock_io_t mock = {0};
    mock.registers[0x01] = 0x4Fu;
    pj_rtc_wake_io_t io = mock_ops(&mock);
    assert(pj_rtc_wake_program(&plan, &io) == PJ_RTC_WAKE_OK);
    assert(mock.operation == 12);
    assert((mock.registers[0x00] & 0x04) == 0);
    assert(memcmp(&mock.registers[0x0B], plan.alarm, sizeof(plan.alarm)) == 0);
    assert(mock.registers[0x11] == 0x18);
    assert((mock.registers[0x01] & 0xC8) == 0x80);
    assert(mock.persisted.token == plan.token);

    static const pj_rtc_wake_result_t expected[] = {
        PJ_RTC_WAKE_ERR_READ_CONTROL1,
        PJ_RTC_WAKE_ERR_SANITIZE_CONTROL1,
        PJ_RTC_WAKE_ERR_READ_CONTROL,
        PJ_RTC_WAKE_ERR_DISABLE_CONTROL,
        PJ_RTC_WAKE_ERR_CLEAR_TIMER,
        PJ_RTC_WAKE_ERR_DISABLE_TIMER,
        PJ_RTC_WAKE_ERR_DISABLE_ALARM,
        PJ_RTC_WAKE_ERR_WRITE_ALARM,
        PJ_RTC_WAKE_ERR_READBACK_ALARM,
        PJ_RTC_WAKE_ERR_PERSIST,
        PJ_RTC_WAKE_ERR_ENABLE_CONTROL,
        PJ_RTC_WAKE_ERR_VERIFY_CONTROL,
    };
    for (int failure = 1; failure <= 12; failure++) {
        mock = (mock_io_t) {.fail_at = failure};
        io = mock_ops(&mock);
        assert(pj_rtc_wake_program(&plan, &io) == expected[failure - 1]);
    }
    mock = (mock_io_t) {.mismatch_alarm = 1};
    io = mock_ops(&mock);
    assert(pj_rtc_wake_program(&plan, &io) == PJ_RTC_WAKE_ERR_READBACK_ALARM);
}

static void test_disarm_reports_and_clears_flags(void)
{
    mock_io_t mock = {0};
    mock.registers[0x00] = 0x05;
    mock.registers[0x01] = 0xCF;
    pj_rtc_wake_io_t io = mock_ops(&mock);
    uint8_t flags = 0;
    assert(pj_rtc_wake_disarm(&io, &flags) == PJ_RTC_WAKE_OK);
    assert(flags == (PJ_RTC_WAKE_CONTROL2_AF | PJ_RTC_WAKE_CONTROL2_TF));
    assert(mock.registers[0x00] == 0x01);
    assert((mock.registers[0x01] & 0xC8) == 0);
    assert(mock.registers[0x10] == 0);
    assert(mock.registers[0x11] == 0x18);
    assert(mock.persisted.state == PJ_RTC_WAKE_NONE);
}

int main(void)
{
    test_plan_none_due_and_exact();
    test_month_checkpoint_and_calendar_boundaries();
    test_equal_deadlines_coalesce_sources();
    test_early_wake_clock_changes_and_duplicate_prevention();
    test_program_sequence_and_failures();
    test_disarm_reports_and_clears_flags();
    puts("rtc wake tests passed");
    return 0;
}
