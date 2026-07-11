#include "pj_time_model.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static pj_time_clock_t clock_at(uint32_t boot, uint64_t monotonic_ms,
                                int64_t wall_ms, int32_t day, uint32_t second)
{
    return (pj_time_clock_t) {
        .boot_id = boot,
        .monotonic_ms = monotonic_ms,
        .wall_utc_ms = wall_ms,
        .local_day = day,
        .local_second = second,
    };
}

static void test_record_round_trip_and_corruption(void)
{
    pj_time_clock_t clock = clock_at(1, 100, 100000, 20, 3600);
    pj_time_state_t state;
    pj_time_state_defaults(&state, &clock);
    assert(pj_time_state_valid(&state));
    assert(pj_time_timer_start(&state, 30000, &clock));

    uint8_t record[PJ_TIME_STATE_RECORD_BYTES];
    assert(pj_time_state_encode(&state, record, sizeof(record)) == sizeof(record));
    pj_time_state_t decoded;
    assert(pj_time_state_decode(record, sizeof(record), &decoded));
    assert(decoded.timer.running);
    assert(decoded.timer.remaining_ms == 30000);
    assert(decoded.alarm_hour == 7);

    record[80] ^= 0x40;
    assert(!pj_time_state_decode(record, sizeof(record), &decoded));
    assert(!pj_time_state_decode(record, sizeof(record) - 1, &decoded));
}

static void test_alarm_missed_recovery_dedup_and_snooze(void)
{
    pj_time_clock_t before = clock_at(1, 0, 0, 100, 6 * 3600 + 59 * 60);
    pj_time_state_t state;
    pj_time_state_defaults(&state, &before);
    assert(pj_time_alarm_configure(&state, 1, 7, 0, &before));

    pj_time_clock_t due = clock_at(1, 60000, 60000, 100, 7 * 3600);
    assert(pj_time_advance(&state, &due));
    const pj_time_alert_t *alert = pj_time_active_alert(&state);
    assert(alert != NULL && alert->source == PJ_TIME_ALERT_ALARM);
    assert(alert->reason == PJ_TIME_ALERT_SCHEDULED && !alert->recovered);
    uint64_t first_id = alert->id;

    assert(!pj_time_advance(&state, &due));
    assert(pj_time_active_alert(&state)->id == first_id);
    assert(!pj_time_alarm_snooze(&state, first_id + 1, 300000, &due));
    assert(pj_time_alarm_snooze(&state, first_id, 300000, &due));
    assert(pj_time_active_alert(&state) == NULL);

    pj_time_clock_t wall_changed = clock_at(1, 359999, -999999999, 100, 7 * 3600);
    assert(pj_time_advance(&state, &wall_changed));
    wall_changed.monotonic_ms = 360000;
    assert(pj_time_advance(&state, &wall_changed));
    alert = pj_time_active_alert(&state);
    assert(alert->source == PJ_TIME_ALERT_ALARM && alert->reason == PJ_TIME_ALERT_SNOOZE);
    assert(pj_time_alert_dismiss(&state, alert->id));

    pj_time_clock_t rewind = clock_at(1, 400000, 0, 100, 6 * 3600 + 30 * 60);
    assert(pj_time_advance(&state, &rewind));
    rewind.monotonic_ms = 500000;
    rewind.local_second = 7 * 3600 + 30 * 60;
    assert(pj_time_advance(&state, &rewind));
    assert(pj_time_active_alert(&state) == NULL);

    pj_time_clock_t missed = clock_at(1, 600000, 0, 101, 9 * 3600);
    assert(pj_time_advance(&state, &missed));
    alert = pj_time_active_alert(&state);
    assert(alert->source == PJ_TIME_ALERT_ALARM && alert->recovered);
    assert(alert->skipped_occurrences == 0);
}

static void test_alarm_coalesces_multiple_missed_days(void)
{
    pj_time_clock_t before = clock_at(2, 0, 0, 10, 6 * 3600 + 59 * 60);
    pj_time_state_t state;
    pj_time_state_defaults(&state, &before);
    assert(pj_time_alarm_configure(&state, 1, 7, 0, &before));
    pj_time_clock_t much_later = clock_at(3, 10, 0, 13, 8 * 3600);
    much_later.reboot_elapsed_valid = 1;
    much_later.reboot_elapsed_ms = 3u * 86400000u;
    assert(pj_time_advance(&state, &much_later));
    assert(state.active_alert.source == PJ_TIME_ALERT_ALARM);
    assert(state.active_alert.recovered);
    assert(state.active_alert.skipped_occurrences == 3);
}

static void test_alarm_rewind_cannot_replay_older_occurrences(void)
{
    pj_time_clock_t before = clock_at(7, 0, 0, 100, 6 * 3600 + 59 * 60);
    pj_time_state_t state;
    pj_time_state_defaults(&state, &before);
    assert(pj_time_alarm_configure(&state, 1, 7, 0, &before));
    pj_time_clock_t due = clock_at(7, 60000, 0, 100, 7 * 3600);
    assert(pj_time_advance(&state, &due));
    assert(pj_time_alert_dismiss(&state, state.active_alert.id));

    uint8_t record[PJ_TIME_STATE_RECORD_BYTES];
    pj_time_state_t restored;
    assert(pj_time_state_encode(&state, record, sizeof(record)) == sizeof(record));
    assert(pj_time_state_decode(record, sizeof(record), &restored));
    state = restored;

    pj_time_clock_t rewind = clock_at(7, 120000, 0, 90, 6 * 3600 + 59 * 60);
    assert(pj_time_advance(&state, &rewind));
    rewind.monotonic_ms += 60000;
    rewind.local_second = 7 * 3600;
    assert(pj_time_advance(&state, &rewind));
    assert(pj_time_active_alert(&state) == NULL);

    pj_time_clock_t recross = clock_at(7, 240000, 0, 100, 7 * 3600);
    assert(pj_time_advance(&state, &recross));
    assert(pj_time_active_alert(&state) == NULL);
    recross.monotonic_ms += 60000;
    recross.local_day = 101;
    assert(pj_time_advance(&state, &recross));
    assert(state.active_alert.source == PJ_TIME_ALERT_ALARM);
}

static void test_snooze_exact_reboot_deadline_is_recovered(void)
{
    pj_time_clock_t before = clock_at(20, 0, 0, 20, 6 * 3600 + 59 * 60);
    pj_time_state_t state;
    pj_time_state_defaults(&state, &before);
    assert(pj_time_alarm_configure(&state, 1, 7, 0, &before));
    pj_time_clock_t due = clock_at(20, 60000, 60000, 20, 7 * 3600);
    assert(pj_time_advance(&state, &due));
    assert(pj_time_alarm_snooze(&state, state.active_alert.id, 300000, &due));

    pj_time_clock_t reboot = clock_at(21, 10, 360000, 20, 7 * 3600 + 5 * 60);
    reboot.reboot_elapsed_valid = 1;
    reboot.reboot_elapsed_ms = 300000;
    assert(pj_time_advance(&state, &reboot));
    assert(state.active_alert.source == PJ_TIME_ALERT_ALARM);
    assert(state.active_alert.reason == PJ_TIME_ALERT_SNOOZE);
    assert(state.active_alert.recovered);
}

static void test_timer_uses_monotonic_and_reboot_policy(void)
{
    pj_time_clock_t start = clock_at(10, 1000, 100000, 1, 100);
    pj_time_state_t state;
    pj_time_state_defaults(&state, &start);
    assert(pj_time_timer_start(&state, 10000, &start));

    pj_time_clock_t changed = clock_at(10, 5000, 900000000000ll, 400, 200);
    assert(pj_time_advance(&state, &changed));
    assert(state.timer.remaining_ms == 6000);

    pj_time_clock_t unknown_reboot = clock_at(11, 50, -400000, 2, 100);
    assert(pj_time_advance(&state, &unknown_reboot));
    assert(state.timer.remaining_ms == 6000);
    assert(state.recovery_time_uncertain);
    pj_time_recovery_acknowledge(&state);
    assert(!state.recovery_time_uncertain);

    pj_time_state_defaults(&state, &start);
    assert(pj_time_timer_start(&state, 10000, &start));
    pj_time_clock_t trusted_reboot = clock_at(12, 25, 777777, 2, 100);
    trusted_reboot.reboot_elapsed_valid = 1;
    trusted_reboot.reboot_elapsed_ms = 12000;
    assert(pj_time_advance(&state, &trusted_reboot));
    const pj_time_alert_t *alert = pj_time_active_alert(&state);
    assert(alert != NULL && alert->source == PJ_TIME_ALERT_TIMER && alert->recovered);
    uint64_t id = alert->id;

    uint8_t record[PJ_TIME_STATE_RECORD_BYTES];
    pj_time_state_t restored;
    assert(pj_time_state_encode(&state, record, sizeof(record)) == sizeof(record));
    assert(pj_time_state_decode(record, sizeof(record), &restored));
    assert(pj_time_active_alert(&restored)->id == id);
    (void)pj_time_advance(&restored, &trusted_reboot);
    assert(pj_time_active_alert(&restored)->id == id);
}

static void test_interval_catchup_and_stopwatch(void)
{
    pj_time_clock_t start = clock_at(4, 100, 1000, 4, 200);
    pj_time_state_t state;
    pj_time_state_defaults(&state, &start);
    assert(pj_time_interval_start(&state, 10000, 5000, &start));
    assert(pj_time_stopwatch_start(&state, &start));

    pj_time_clock_t later = clock_at(4, 32100, -9000000, 2, 1);
    assert(pj_time_advance(&state, &later));
    assert(state.interval.running);
    assert(state.interval_phase == 4);
    assert(state.interval.remaining_ms == 8000);
    const pj_time_alert_t *alert = pj_time_active_alert(&state);
    assert(alert->source == PJ_TIME_ALERT_INTERVAL);
    assert(alert->skipped_occurrences == 3 && alert->recovered);
    assert(pj_time_stopwatch_elapsed(&state) == 32000);

    pj_time_clock_t reboot = clock_at(5, 20, 100, 2, 2);
    reboot.reboot_elapsed_valid = 1;
    reboot.reboot_elapsed_ms = 3000;
    assert(pj_time_advance(&state, &reboot));
    assert(state.interval.remaining_ms == 5000);
    assert(pj_time_stopwatch_elapsed(&state) == 35000);
    assert(pj_time_stopwatch_pause(&state, &reboot));
    assert(pj_time_stopwatch_elapsed(&state) == 35000);
}

static void test_interval_resume_preserves_phase_and_remaining(void)
{
    pj_time_clock_t start = clock_at(30, 0, 0, 30, 100);
    pj_time_state_t state;
    pj_time_state_defaults(&state, &start);
    assert(pj_time_interval_start(&state, 10000, 5000, &start));

    pj_time_clock_t mid_work = clock_at(30, 4000, 999999, 30, 104);
    assert(pj_time_advance(&state, &mid_work));
    assert(state.interval_phase == 0 && state.interval.remaining_ms == 6000);
    assert(pj_time_interval_pause(&state, &mid_work));

    uint8_t record[PJ_TIME_STATE_RECORD_BYTES];
    pj_time_state_t restored;
    assert(pj_time_state_encode(&state, record, sizeof(record)) == sizeof(record));
    assert(pj_time_state_decode(record, sizeof(record), &restored));
    pj_time_clock_t reboot = clock_at(31, 100, 50, 30, 200);
    assert(pj_time_interval_resume(&restored, &reboot));
    assert(restored.interval_phase == 0 && restored.interval.remaining_ms == 6000);

    reboot.monotonic_ms = 6100;
    assert(pj_time_advance(&restored, &reboot));
    assert(restored.interval_phase == 1 && restored.interval.remaining_ms == 5000);
    assert(restored.active_alert.source == PJ_TIME_ALERT_INTERVAL);
    assert(pj_time_alert_dismiss(&restored, restored.active_alert.id));

    reboot.monotonic_ms = 8100;
    assert(pj_time_advance(&restored, &reboot));
    assert(restored.interval_phase == 1 && restored.interval.remaining_ms == 3000);
    assert(pj_time_interval_pause(&restored, &reboot));
    assert(pj_time_state_encode(&restored, record, sizeof(record)) == sizeof(record));
    assert(pj_time_state_decode(record, sizeof(record), &state));

    pj_time_clock_t second_reboot = clock_at(32, 500, -500, 29, 100);
    assert(pj_time_interval_resume(&state, &second_reboot));
    assert(state.interval_phase == 1 && state.interval.remaining_ms == 3000);
    second_reboot.monotonic_ms = 3500;
    assert(pj_time_advance(&state, &second_reboot));
    assert(state.interval_phase == 2 && state.interval.remaining_ms == 10000);
    assert(state.active_alert.source == PJ_TIME_ALERT_INTERVAL);
}

static void test_simultaneous_priority_and_conflicts(void)
{
    pj_time_clock_t start = clock_at(1, 0, 0, 10, 6 * 3600 + 59 * 60);
    pj_time_state_t state;
    pj_time_state_defaults(&state, &start);
    assert(pj_time_alarm_configure(&state, 1, 7, 0, &start));
    assert(pj_time_timer_start(&state, 60000, &start));
    assert(pj_time_interval_start(&state, 60000, 30000, &start));

    pj_time_clock_t due = clock_at(1, 60000, 60000, 10, 7 * 3600);
    assert(pj_time_advance(&state, &due));
    assert(state.pending_count == 2);
    assert(state.active_alert.source == PJ_TIME_ALERT_ALARM);
    assert(pj_time_alert_dismiss(&state, state.active_alert.id));
    assert(state.active_alert.source == PJ_TIME_ALERT_TIMER);
    assert(pj_time_alert_dismiss(&state, state.active_alert.id));
    assert(state.active_alert.source == PJ_TIME_ALERT_INTERVAL);

    assert(pj_time_alert_conflict_action(PJ_TIME_ALERT_ALARM,
                                         PJ_TIME_ACTIVITY_PLAYBACK) ==
           PJ_TIME_PREEMPT_PLAYBACK);
    assert(pj_time_alert_conflict_action(PJ_TIME_ALERT_TIMER,
                                         PJ_TIME_ACTIVITY_RECORDING) ==
           PJ_TIME_VISUAL_DEFER_AUDIO);
    assert(pj_time_alert_conflict_action(PJ_TIME_ALERT_INTERVAL,
                                         PJ_TIME_ACTIVITY_IDLE) == PJ_TIME_PRESENT);
}

static void test_pause_at_expiry_and_canonical_validation(void)
{
    pj_time_clock_t start = clock_at(9, 100, 100, 9, 100);
    pj_time_state_t state;
    pj_time_state_defaults(&state, &start);
    assert(pj_time_timer_start(&state, 5000, &start));
    pj_time_clock_t due = clock_at(9, 5100, 999999999, 9, 105);
    assert(pj_time_timer_pause(&state, &due));
    assert(!state.timer.running);
    assert(state.active_alert.source == PJ_TIME_ALERT_TIMER);
    pj_time_timer_reset(&state);
    assert(state.active_alert.source == PJ_TIME_ALERT_NONE);
    assert(!pj_time_alert_dismiss(&state, 0));

    pj_time_alert_t stale = {.id = 99, .source = PJ_TIME_ALERT_TIMER,
                             .reason = PJ_TIME_ALERT_EXPIRED};
    pj_time_state_t invalid = state;
    invalid.pending[invalid.pending_count] = stale;
    assert(!pj_time_state_valid(&invalid));
    invalid = state;
    invalid.next_alert_id = state.active_alert.id;
    assert(!pj_time_state_valid(&invalid));

    invalid = state;
    invalid.active_alert = (pj_time_alert_t) {
        .id = 1, .source = PJ_TIME_ALERT_TIMER, .reason = PJ_TIME_ALERT_EXPIRED,
    };
    invalid.pending_count = 1;
    invalid.pending[0] = (pj_time_alert_t) {
        .id = 1, .source = PJ_TIME_ALERT_INTERVAL, .reason = PJ_TIME_ALERT_EXPIRED,
    };
    assert(!pj_time_state_valid(&invalid));

    memset(&invalid.active_alert, 0, sizeof(invalid.active_alert));
    assert(!pj_time_state_valid(&invalid));
}

static void test_local_minute_and_occurrence_boundaries(void)
{
    pj_time_clock_t maximum = clock_at(1, 0, 0, INT32_MAX, 86399);
    pj_time_state_t state;
    pj_time_state_defaults(&state, &maximum);
    assert(pj_time_state_valid(&state));
    assert(state.alarm_last_local_minute == (int64_t)INT32_MAX * 1440 + 1439);
    assert(!pj_time_advance(&state, &maximum));

    pj_time_state_t invalid = state;
    invalid.alarm_last_local_minute = -1;
    assert(!pj_time_state_valid(&invalid));
    invalid = state;
    invalid.alarm_last_local_minute = (int64_t)INT32_MAX * 1440 + 1440;
    assert(!pj_time_state_valid(&invalid));
    invalid = state;
    invalid.alarm_last_occurrence = ((uint64_t)state.alarm_generation << 32u) |
        ((uint64_t)INT32_MAX + 1u);
    assert(!pj_time_state_valid(&invalid));
    invalid = state;
    invalid.alarm_last_occurrence = ((uint64_t)(state.alarm_generation + 1u) << 32u) | 1u;
    assert(!pj_time_state_valid(&invalid));
}

static void test_next_wake_delay(void)
{
    pj_time_clock_t clock = clock_at(50, 1000, 100500, 50,
                                     6 * 3600 + 59 * 60 + 59);
    pj_time_state_t state;
    pj_time_state_defaults(&state, &clock);
    assert(pj_time_next_wake_delay_ms(&state, &clock) == UINT64_MAX);

    assert(pj_time_alarm_configure(&state, 1, 7, 0, &clock));
    assert(pj_time_next_wake_delay_ms(&state, &clock) == 500);

    pj_time_clock_t crossed_alarm = clock;
    crossed_alarm.monotonic_ms += 500;
    crossed_alarm.wall_utc_ms += 500;
    crossed_alarm.local_second += 1;
    assert(pj_time_next_wake_delay_ms(&state, &crossed_alarm) == 0);
    assert(pj_time_alarm_configure(&state, 0, 7, 0, &clock));

    assert(pj_time_timer_start(&state, 30000, &clock));
    pj_time_clock_t later = clock;
    later.monotonic_ms += 5000;
    later.wall_utc_ms += 5000;
    later.local_second += 5;
    assert(pj_time_next_wake_delay_ms(&state, &later) == 25000);

    assert(pj_time_interval_start(&state, 10000, 5000, &later));
    assert(pj_time_next_wake_delay_ms(&state, &later) == 10000);

    pj_time_clock_t due = later;
    due.monotonic_ms += 10000;
    due.wall_utc_ms += 10000;
    due.local_second += 10;
    assert(pj_time_next_wake_delay_ms(&state, &due) == 0);

    (void)pj_time_advance(&state, &due);
    assert(pj_time_active_alert(&state) != NULL);
    assert(pj_time_next_wake_delay_ms(&state, &due) == 0);
}

static void test_next_wake_delay_after_trusted_reboot(void)
{
    pj_time_clock_t start = clock_at(60, 0, 100000, 60, 100);
    pj_time_state_t state;
    pj_time_state_defaults(&state, &start);
    assert(pj_time_timer_start(&state, 60000, &start));

    pj_time_clock_t reboot = clock_at(61, 0, 125000, 60, 125);
    reboot.reboot_elapsed_valid = 1;
    reboot.reboot_elapsed_ms = 25000;
    assert(pj_time_next_wake_delay_ms(&state, &reboot) == 35000);
}

int main(void)
{
    test_record_round_trip_and_corruption();
    test_alarm_missed_recovery_dedup_and_snooze();
    test_alarm_coalesces_multiple_missed_days();
    test_alarm_rewind_cannot_replay_older_occurrences();
    test_snooze_exact_reboot_deadline_is_recovered();
    test_timer_uses_monotonic_and_reboot_policy();
    test_interval_catchup_and_stopwatch();
    test_interval_resume_preserves_phase_and_remaining();
    test_simultaneous_priority_and_conflicts();
    test_pause_at_expiry_and_canonical_validation();
    test_local_minute_and_occurrence_boundaries();
    test_next_wake_delay();
    test_next_wake_delay_after_trusted_reboot();
    puts("time model tests passed");
    return 0;
}
