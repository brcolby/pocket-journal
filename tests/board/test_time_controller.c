#include "pj_time_controller.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_LEN(values) (sizeof(values) / sizeof((values)[0]))

typedef struct {
    char events[128];
    size_t event_count;

    pj_time_controller_load_result_t load_result;
    uint8_t load_record[PJ_TIME_STATE_RECORD_BYTES];
    unsigned load_calls;

    pj_time_controller_save_result_t save_plan[16];
    size_t save_plan_count;
    unsigned save_calls;
    uint8_t saved_record[PJ_TIME_STATE_RECORD_BYTES];
    pj_time_state_t saved_state;

    uint32_t boot_plan[80];
    size_t boot_plan_count;
    unsigned boot_calls;
    uint32_t next_boot_id;

    pj_time_clock_t clock;
    int clock_ok;
    int clock_preserve_boot_id;
    unsigned clock_calls;
    uint32_t clock_boot_id;

    pj_time_controller_alarm_settings_t alarm;
    int alarm_ok;
    unsigned alarm_calls;
    int trusted;
    pj_time_activity_t activity;

    pj_time_controller_wake_result_t wake_plan[16];
    size_t wake_plan_count;
    unsigned wake_calls;
    int wake_armed;
    pj_time_wake_deadline_t wake_deadline;

    unsigned media_calls;
    int media_has_alert;
    pj_time_alert_t media_alert;
    pj_time_conflict_action_t media_action;

    unsigned publish_calls;
    pj_time_controller_result_t published;
} fixture_t;

static void record_event(fixture_t *fixture, char event)
{
    assert(fixture->event_count < sizeof(fixture->events));
    fixture->events[fixture->event_count++] = event;
}

static void assert_events(const fixture_t *fixture, const char *expected)
{
    size_t length = strlen(expected);
    assert(fixture->event_count == length);
    assert(memcmp(fixture->events, expected, length) == 0);
}

static pj_time_clock_t clock_at(uint32_t boot_id, uint64_t monotonic_ms,
                                int64_t wall_ms, int32_t day,
                                uint32_t local_second)
{
    return (pj_time_clock_t) {
        .boot_id = boot_id,
        .monotonic_ms = monotonic_ms,
        .wall_utc_ms = wall_ms,
        .local_day = day,
        .local_second = local_second,
    };
}

static void fixture_defaults(fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->load_result = PJ_TIME_CONTROLLER_LOAD_NOT_FOUND;
    fixture->next_boot_id = 10;
    fixture->clock = clock_at(10, 1000, 100000, 20, 6 * 3600);
    fixture->clock_ok = 1;
    fixture->alarm = (pj_time_controller_alarm_settings_t) {
        .enabled = 0,
        .hour = 7,
        .minute = 0,
    };
    fixture->alarm_ok = 1;
    fixture->trusted = 1;
    fixture->activity = PJ_TIME_ACTIVITY_IDLE;
}

static pj_time_controller_load_result_t load_callback(
    void *context, uint8_t record[PJ_TIME_STATE_RECORD_BYTES])
{
    fixture_t *fixture = context;
    record_event(fixture, 'L');
    fixture->load_calls++;
    memcpy(record, fixture->load_record, sizeof(fixture->load_record));
    return fixture->load_result;
}

static pj_time_controller_save_result_t save_callback(
    void *context, const uint8_t record[PJ_TIME_STATE_RECORD_BYTES])
{
    fixture_t *fixture = context;
    record_event(fixture, 'V');
    size_t index = fixture->save_calls++;
    memcpy(fixture->saved_record, record, sizeof(fixture->saved_record));
    assert(pj_time_state_decode(record, PJ_TIME_STATE_RECORD_BYTES,
                                &fixture->saved_state));
    if (index < fixture->save_plan_count) {
        return fixture->save_plan[index];
    }
    return PJ_TIME_CONTROLLER_SAVE_OK;
}

static uint32_t next_boot_id_callback(void *context)
{
    fixture_t *fixture = context;
    record_event(fixture, 'B');
    size_t index = fixture->boot_calls++;
    if (index < fixture->boot_plan_count) {
        return fixture->boot_plan[index];
    }
    return fixture->next_boot_id++;
}

static int clock_callback(void *context, uint32_t boot_id,
                          pj_time_clock_t *clock)
{
    fixture_t *fixture = context;
    record_event(fixture, 'C');
    fixture->clock_calls++;
    fixture->clock_boot_id = boot_id;
    *clock = fixture->clock;
    if (!fixture->clock_preserve_boot_id) {
        clock->boot_id = boot_id;
    }
    return fixture->clock_ok;
}

static int alarm_settings_callback(
    void *context, pj_time_controller_alarm_settings_t *settings)
{
    fixture_t *fixture = context;
    record_event(fixture, 'S');
    fixture->alarm_calls++;
    *settings = fixture->alarm;
    return fixture->alarm_ok;
}

static int wall_time_trusted_callback(void *context)
{
    fixture_t *fixture = context;
    record_event(fixture, 'T');
    return fixture->trusted;
}

static pj_time_activity_t activity_callback(void *context)
{
    fixture_t *fixture = context;
    record_event(fixture, 'A');
    return fixture->activity;
}

static pj_time_controller_wake_result_t schedule_wake_callback(
    void *context, const pj_time_wake_deadline_t *deadline)
{
    fixture_t *fixture = context;
    record_event(fixture, 'W');
    size_t index = fixture->wake_calls++;
    fixture->wake_armed = deadline != NULL;
    fixture->wake_deadline = deadline == NULL
        ? (pj_time_wake_deadline_t) {0}
        : *deadline;
    if (index < fixture->wake_plan_count) {
        return fixture->wake_plan[index];
    }
    return PJ_TIME_CONTROLLER_WAKE_OK;
}

static void project_media_callback(void *context,
                                   const pj_time_alert_t *alert,
                                   pj_time_conflict_action_t action)
{
    fixture_t *fixture = context;
    record_event(fixture, 'M');
    fixture->media_calls++;
    fixture->media_has_alert = alert != NULL;
    fixture->media_alert = alert == NULL ? (pj_time_alert_t) {0} : *alert;
    fixture->media_action = action;
}

static void publish_status_callback(
    void *context, const pj_time_controller_result_t *result)
{
    fixture_t *fixture = context;
    record_event(fixture, 'P');
    fixture->publish_calls++;
    fixture->published = *result;
}

static pj_time_controller_io_t fixture_io(fixture_t *fixture)
{
    return (pj_time_controller_io_t) {
        .context = fixture,
        .load = load_callback,
        .save = save_callback,
        .next_boot_id = next_boot_id_callback,
        .clock = clock_callback,
        .alarm_settings = alarm_settings_callback,
        .wall_time_trusted = wall_time_trusted_callback,
        .activity = activity_callback,
        .schedule_wake = schedule_wake_callback,
        .project_media = project_media_callback,
        .publish_status = publish_status_callback,
    };
}

static void fixture_load_state(fixture_t *fixture,
                               const pj_time_state_t *state)
{
    fixture->load_result = PJ_TIME_CONTROLLER_LOAD_VALID;
    assert(pj_time_state_encode(state, fixture->load_record,
                                sizeof(fixture->load_record)) ==
           sizeof(fixture->load_record));
}

static void clear_observations(fixture_t *fixture)
{
    fixture->event_count = 0;
    fixture->save_calls = 0;
    fixture->save_plan_count = 0;
    fixture->wake_calls = 0;
    fixture->wake_plan_count = 0;
    fixture->media_calls = 0;
    fixture->publish_calls = 0;
}

static pj_time_controller_t init_default(fixture_t *fixture)
{
    pj_time_controller_t controller;
    pj_time_controller_result_t result;
    pj_time_controller_io_t io = fixture_io(fixture);
    assert(pj_time_controller_init(&controller, &io, &result));
    assert(result.ready);
    assert(result.persistence_attempted);
    assert(result.save_result == PJ_TIME_CONTROLLER_SAVE_OK);
    assert(result.wake_requested);
    assert(result.media_projected);
    assert(fixture->publish_calls == 1);
    assert(fixture->published.ready);
    return controller;
}

static pj_time_controller_result_t apply(
    pj_time_controller_t *controller, fixture_t *fixture,
    pj_time_controller_command_type_t type, uint64_t value,
    uint64_t secondary)
{
    pj_time_controller_command_t command = {
        .type = type,
        .alert_id = value,
        .duration_ms = value,
        .secondary_duration_ms = secondary,
    };
    pj_time_controller_result_t result;
    assert(pj_time_controller_apply(controller, &command, &result));
    (void)fixture;
    return result;
}

static void test_argument_validation_and_load_classes(void)
{
    fixture_t fixture;
    fixture_defaults(&fixture);
    pj_time_controller_io_t io = fixture_io(&fixture);
    pj_time_controller_t controller;
    pj_time_controller_result_t result;

    assert(!pj_time_controller_init(NULL, &io, &result));
    assert(!pj_time_controller_init(&controller, NULL, &result));
    assert(!pj_time_controller_init(&controller, &io, NULL));
    pj_time_controller_io_t missing = io;
    missing.save = NULL;
    assert(!pj_time_controller_init(&controller, &missing, &result));
    assert(pj_time_controller_state(NULL) == NULL);
    assert(!pj_time_controller_ready(NULL));

    assert(pj_time_controller_init(&controller, &io, &result));
    assert(result.load_result == PJ_TIME_CONTROLLER_LOAD_NOT_FOUND);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_NONE);
    assert(pj_time_controller_ready(&controller));
    assert(pj_time_controller_state(&controller) != NULL);

    fixture_defaults(&fixture);
    fixture.load_result = PJ_TIME_CONTROLLER_LOAD_CORRUPT;
    io = fixture_io(&fixture);
    assert(pj_time_controller_init(&controller, &io, &result));
    assert(result.load_result == PJ_TIME_CONTROLLER_LOAD_CORRUPT);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_LOAD_CORRUPT);

    fixture_defaults(&fixture);
    fixture.load_result = PJ_TIME_CONTROLLER_LOAD_NONE;
    io = fixture_io(&fixture);
    assert(pj_time_controller_init(&controller, &io, &result));
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_LOAD_CORRUPT);

    fixture_defaults(&fixture);
    fixture.load_result = PJ_TIME_CONTROLLER_LOAD_VALID;
    memcpy(fixture.load_record, "BAD!", 4);
    io = fixture_io(&fixture);
    assert(pj_time_controller_init(&controller, &io, &result));
    assert(result.load_result == PJ_TIME_CONTROLLER_LOAD_CORRUPT);

    pj_time_state_t state;
    pj_time_clock_t old_clock = clock_at(1, 0, 100000, 20, 100);
    pj_time_state_defaults(&state, &old_clock);
    fixture_defaults(&fixture);
    fixture_load_state(&fixture, &state);
    fixture.load_record[4] = 2;
    io = fixture_io(&fixture);
    assert(pj_time_controller_init(&controller, &io, &result));
    assert(result.load_result == PJ_TIME_CONTROLLER_LOAD_INCOMPATIBLE);
    assert(result.diagnostic ==
           PJ_TIME_CONTROLLER_DIAGNOSTIC_LOAD_INCOMPATIBLE);

    fixture_defaults(&fixture);
    fixture_load_state(&fixture, &state);
    fixture.load_record[100] ^= 1;
    io = fixture_io(&fixture);
    assert(pj_time_controller_init(&controller, &io, &result));
    assert(result.load_result == PJ_TIME_CONTROLLER_LOAD_CORRUPT);

    fixture_defaults(&fixture);
    fixture.load_result = PJ_TIME_CONTROLLER_LOAD_IO_ERROR;
    io = fixture_io(&fixture);
    assert(!pj_time_controller_init(&controller, &io, &result));
    assert(!result.ready);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_LOAD_IO_ERROR);
    assert(fixture.save_calls == 0 && fixture.clock_calls == 0);
}

static void test_boot_collision_and_boot_failure(void)
{
    fixture_t fixture;
    fixture_defaults(&fixture);
    pj_time_clock_t old_clock = clock_at(1, 0, 100000, 20, 100);
    pj_time_state_t state;
    pj_time_state_defaults(&state, &old_clock);
    state.snooze.anchor_boot_id = 11;
    state.timer.anchor_boot_id = 12;
    state.interval.anchor_boot_id = 13;
    state.stopwatch_anchor_boot_id = 14;
    fixture_load_state(&fixture, &state);
    const uint32_t candidates[] = {0, 11, 12, 13, 14, 15};
    memcpy(fixture.boot_plan, candidates, sizeof(candidates));
    fixture.boot_plan_count = ARRAY_LEN(candidates);
    pj_time_controller_io_t io = fixture_io(&fixture);
    pj_time_controller_t controller;
    pj_time_controller_result_t result;
    assert(pj_time_controller_init(&controller, &io, &result));
    assert(controller.boot_id == 15);
    assert(fixture.boot_calls == ARRAY_LEN(candidates));
    assert(fixture.clock_boot_id == 15);

    fixture_defaults(&fixture);
    memset(fixture.boot_plan, 0, sizeof(fixture.boot_plan));
    fixture.boot_plan_count = ARRAY_LEN(fixture.boot_plan);
    io = fixture_io(&fixture);
    assert(!pj_time_controller_init(&controller, &io, &result));
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_BOOT_ID_ERROR);
    assert(fixture.boot_calls == 64);
    assert(fixture.clock_calls == 0 && fixture.save_calls == 0);
}

static void test_restore_trusted_and_untrusted_elapsed(void)
{
    pj_time_clock_t old_clock = clock_at(1, 1000, 100000, 20, 100);
    pj_time_state_t state;
    pj_time_state_defaults(&state, &old_clock);
    assert(pj_time_timer_start(&state, 10000, &old_clock));
    assert(pj_time_stopwatch_start(&state, &old_clock));

    fixture_t fixture;
    fixture_defaults(&fixture);
    fixture_load_state(&fixture, &state);
    fixture.clock = clock_at(10, 50, 104000, 20, 104);
    fixture.trusted = 1;
    pj_time_controller_t controller = init_default(&fixture);
    const pj_time_state_t *restored = pj_time_controller_state(&controller);
    assert(restored->timer.remaining_ms == 6000);
    assert(restored->stopwatch_elapsed_ms == 4000);
    assert(!restored->recovery_time_uncertain);
    assert(fixture.saved_state.timer.remaining_ms == 6000);

    fixture_defaults(&fixture);
    fixture_load_state(&fixture, &state);
    fixture.clock = clock_at(10, 50, 104000, 20, 104);
    fixture.trusted = 0;
    controller = init_default(&fixture);
    restored = pj_time_controller_state(&controller);
    assert(restored->timer.remaining_ms == 10000);
    assert(restored->stopwatch_elapsed_ms == 0);
    assert(restored->recovery_time_uncertain);
    assert(fixture.published.diagnostic ==
           PJ_TIME_CONTROLLER_DIAGNOSTIC_TIME_UNCERTAIN);
}

static void test_alarm_settings_reconcile_persists_and_rearms(void)
{
    fixture_t fixture;
    fixture_defaults(&fixture);
    fixture.alarm.enabled = 1;
    fixture.alarm.hour = 8;
    fixture.alarm.minute = 30;
    pj_time_controller_t controller = init_default(&fixture);
    assert(controller.state.alarm_enabled);
    assert(controller.state.alarm_hour == 8);
    assert(controller.state.alarm_minute == 30);
    assert(fixture.saved_state.alarm_hour == 8);

    clear_observations(&fixture);
    fixture.alarm.hour = 9;
    fixture.alarm.minute = 45;
    fixture.clock.monotonic_ms += 1000;
    fixture.clock.wall_utc_ms += 1000;
    pj_time_controller_result_t result;
    assert(pj_time_controller_update(&controller, &result));
    assert(result.state_changed);
    assert(result.persistence_attempted);
    assert(fixture.saved_state.alarm_hour == 9);
    assert(fixture.saved_state.alarm_minute == 45);
    assert(result.wake_requested);
    assert(fixture.wake_calls == 1);
}

static void test_every_command_and_immediate_persistence(void)
{
    fixture_t fixture;
    fixture_defaults(&fixture);
    pj_time_controller_t controller = init_default(&fixture);
    clear_observations(&fixture);

    pj_time_controller_result_t result = apply(
        &controller, &fixture, PJ_TIME_CONTROLLER_COMMAND_STOPWATCH_START, 0, 0);
    assert(result.command_attempted && result.command_applied);
    assert(result.persistence_attempted && fixture.save_calls == 1);
    assert(fixture.saved_state.stopwatch_running);

    fixture.clock.monotonic_ms += 4000;
    fixture.clock.wall_utc_ms += 4000;
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_STOPWATCH_PAUSE, 0, 0);
    assert(result.command_applied && !controller.state.stopwatch_running);
    assert(controller.state.stopwatch_elapsed_ms == 4000);
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_STOPWATCH_RESET, 0, 0);
    assert(result.command_applied && controller.state.stopwatch_elapsed_ms == 0);

    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_TIMER_START, 10000, 0);
    assert(result.command_applied && controller.state.timer.running);
    assert(controller.state.timer.remaining_ms == 10000);
    fixture.clock.monotonic_ms += 2000;
    fixture.clock.wall_utc_ms += 2000;
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_TIMER_PAUSE, 0, 0);
    assert(result.command_applied && !controller.state.timer.running);
    assert(controller.state.timer.remaining_ms == 8000);
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_TIMER_RESET, 0, 0);
    assert(result.command_applied && controller.state.timer.remaining_ms == 0);

    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_INTERVAL_START, 9000, 3000);
    assert(result.command_applied && controller.state.interval.running);
    fixture.clock.monotonic_ms += 2000;
    fixture.clock.wall_utc_ms += 2000;
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_INTERVAL_PAUSE, 0, 0);
    assert(result.command_applied && !controller.state.interval.running);
    assert(controller.state.interval.remaining_ms == 7000);
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_INTERVAL_START, 7000, 999999);
    assert(result.command_applied && controller.state.interval.running);
    assert(controller.state.interval_work_ms == 9000);
    assert(controller.state.interval_rest_ms == 3000);
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_INTERVAL_RESET, 0, 0);
    assert(result.command_applied && controller.state.interval.remaining_ms == 0);

    result = apply(&controller, &fixture,
                   (pj_time_controller_command_type_t)99, 0, 0);
    assert(result.command_attempted && !result.command_applied);
    assert(!result.state_changed && !result.persistence_attempted);
}

static void test_advance_before_alert_commands(void)
{
    fixture_t fixture;
    fixture_defaults(&fixture);
    pj_time_controller_t controller = init_default(&fixture);
    pj_time_controller_result_t result = apply(
        &controller, &fixture, PJ_TIME_CONTROLLER_COMMAND_TIMER_START, 1000, 0);
    assert(result.command_applied);
    fixture.clock.monotonic_ms += 1000;
    fixture.clock.wall_utc_ms += 1000;
    clear_observations(&fixture);
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_ALERT_DISMISS, 1, 0);
    assert(result.command_applied);
    assert(result.transition);
    assert(controller.state.active_alert.source == PJ_TIME_ALERT_NONE);
    assert(!controller.state.timer.running);
    assert(fixture.saved_state.active_alert.source == PJ_TIME_ALERT_NONE);

    fixture.alarm.enabled = 1;
    fixture.alarm.hour = 7;
    fixture.alarm.minute = 0;
    fixture.clock.local_second = 6 * 3600 + 59 * 60;
    assert(pj_time_controller_time_changed(&controller, 1, &result));
    fixture.clock.monotonic_ms += 60000;
    fixture.clock.wall_utc_ms += 60000;
    fixture.clock.local_second = 7 * 3600;
    assert(pj_time_controller_update(&controller, &result));
    assert(controller.state.active_alert.source == PJ_TIME_ALERT_ALARM);
    uint64_t alarm_id = controller.state.active_alert.id;
    pj_time_controller_command_t snooze = {
        .type = PJ_TIME_CONTROLLER_COMMAND_ALARM_SNOOZE,
        .alert_id = alarm_id,
    };
    assert(pj_time_controller_apply(&controller, &snooze, &result));
    assert(result.command_applied);
    assert(controller.state.snooze.running);
    assert(controller.state.snooze.remaining_ms ==
           PJ_TIME_CONTROLLER_SNOOZE_MS);
    assert(controller.state.active_alert.source == PJ_TIME_ALERT_NONE);
}

static void test_save_failure_latest_state_and_retry(void)
{
    fixture_t fixture;
    fixture_defaults(&fixture);
    pj_time_controller_t controller = init_default(&fixture);
    clear_observations(&fixture);
    fixture.save_plan[0] = PJ_TIME_CONTROLLER_SAVE_TRANSIENT_ERROR;
    fixture.save_plan[1] = PJ_TIME_CONTROLLER_SAVE_OK;
    fixture.save_plan_count = 2;
    pj_time_controller_result_t result = apply(
        &controller, &fixture, PJ_TIME_CONTROLLER_COMMAND_TIMER_START, 120000, 0);
    assert(result.persistence_attempted);
    assert(result.save_result == PJ_TIME_CONTROLLER_SAVE_TRANSIENT_ERROR);
    assert(result.dirty);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_SAVE_TRANSIENT);

    fixture.clock.monotonic_ms += 30000;
    fixture.clock.wall_utc_ms += 30000;
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_ALERT_DISMISS, 999, 0);
    assert(!result.command_applied);
    assert(!result.persistence_attempted);
    assert(fixture.save_calls == 1);

    fixture.clock.monotonic_ms += 30000;
    fixture.clock.wall_utc_ms += 30000;
    assert(pj_time_controller_update(&controller, &result));
    assert(result.persistence_attempted);
    assert(result.save_result == PJ_TIME_CONTROLLER_SAVE_OK);
    assert(!result.dirty);
    assert(fixture.saved_state.timer.remaining_ms == 60000);
    assert(fixture.save_calls == 2);

    clear_observations(&fixture);
    fixture.save_plan[0] = PJ_TIME_CONTROLLER_SAVE_PERMANENT_ERROR;
    fixture.save_plan[1] = PJ_TIME_CONTROLLER_SAVE_OK;
    fixture.save_plan_count = 2;
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_STOPWATCH_START, 0, 0);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_SAVE_PERMANENT);
    fixture.clock.monotonic_ms += 1000;
    fixture.clock.wall_utc_ms += 1000;
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_TIMER_PAUSE, 0, 0);
    assert(result.command_applied && result.persistence_attempted);
    assert(!result.dirty);
    assert(fixture.saved_state.stopwatch_running);
    assert(!fixture.saved_state.timer.running);
}

static void test_checkpoint_dirty_retry_and_idle_no_writes(void)
{
    fixture_t fixture;
    fixture_defaults(&fixture);
    pj_time_controller_t controller = init_default(&fixture);
    clear_observations(&fixture);
    pj_time_controller_result_t result;
    fixture.clock.monotonic_ms += PJ_TIME_CONTROLLER_CHECKPOINT_MS;
    fixture.clock.wall_utc_ms += PJ_TIME_CONTROLLER_CHECKPOINT_MS;
    assert(pj_time_controller_update(&controller, &result));
    assert(!result.state_changed);
    assert(!result.persistence_attempted);
    assert(fixture.save_calls == 0);

    assert(apply(&controller, &fixture,
                 PJ_TIME_CONTROLLER_COMMAND_STOPWATCH_START, 0, 0)
               .command_applied);
    clear_observations(&fixture);
    fixture.clock.monotonic_ms += PJ_TIME_CONTROLLER_CHECKPOINT_MS - 1;
    fixture.clock.wall_utc_ms += PJ_TIME_CONTROLLER_CHECKPOINT_MS - 1;
    assert(pj_time_controller_update(&controller, &result));
    assert(result.state_changed && !result.persistence_attempted);
    fixture.clock.monotonic_ms += 1;
    fixture.clock.wall_utc_ms += 1;
    assert(pj_time_controller_update(&controller, &result));
    assert(result.persistence_attempted);
    assert(fixture.saved_state.stopwatch_elapsed_ms ==
           PJ_TIME_CONTROLLER_CHECKPOINT_MS);

    clear_observations(&fixture);
    fixture.save_plan[0] = PJ_TIME_CONTROLLER_SAVE_TRANSIENT_ERROR;
    fixture.save_plan[1] = PJ_TIME_CONTROLLER_SAVE_OK;
    fixture.save_plan_count = 2;
    assert(apply(&controller, &fixture,
                 PJ_TIME_CONTROLLER_COMMAND_STOPWATCH_PAUSE, 0, 0)
               .persistence_attempted);
    fixture.clock.monotonic_ms += PJ_TIME_CONTROLLER_RETRY_MS - 1;
    fixture.clock.wall_utc_ms += PJ_TIME_CONTROLLER_RETRY_MS - 1;
    assert(pj_time_controller_update(&controller, &result));
    assert(!result.persistence_attempted);
    fixture.clock.monotonic_ms += 1;
    fixture.clock.wall_utc_ms += 1;
    assert(pj_time_controller_update(&controller, &result));
    assert(result.persistence_attempted && !result.dirty);
}

static void test_time_changes_and_recovery_acknowledge(void)
{
    pj_time_clock_t old_clock = clock_at(1, 0, 100000, 20, 100);
    pj_time_state_t state;
    pj_time_state_defaults(&state, &old_clock);
    assert(pj_time_timer_start(&state, 10000, &old_clock));
    fixture_t fixture;
    fixture_defaults(&fixture);
    fixture_load_state(&fixture, &state);
    fixture.clock = clock_at(10, 0, 104000, 20, 104);
    fixture.trusted = 0;
    pj_time_controller_t controller = init_default(&fixture);
    assert(controller.state.recovery_time_uncertain);

    clear_observations(&fixture);
    pj_time_controller_result_t result = apply(
        &controller, &fixture,
        PJ_TIME_CONTROLLER_COMMAND_RECOVERY_ACKNOWLEDGE, 0, 0);
    assert(result.command_applied);
    assert(!controller.state.recovery_time_uncertain);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_NONE);
    assert(result.persistence_attempted);

    clear_observations(&fixture);
    fixture.clock.wall_utc_ms += 5000;
    fixture.clock.monotonic_ms += 5000;
    assert(pj_time_controller_time_changed(&controller, 1, &result));
    assert(controller.wall_time_trusted);
    assert(result.persistence_attempted);
    assert(result.wake_requested);
    assert(fixture.save_calls == 1 && fixture.wake_calls == 1);

    fixture.clock_ok = 0;
    assert(!pj_time_controller_time_changed(&controller, 0, &result));
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_CLOCK_ERROR);
    assert(fixture.publish_calls == 2);
}

static void test_wake_arm_disarm_and_error_retry(void)
{
    fixture_t fixture;
    fixture_defaults(&fixture);
    pj_time_controller_t controller = init_default(&fixture);
    assert(!fixture.wake_armed);
    clear_observations(&fixture);

    pj_time_controller_result_t result = apply(
        &controller, &fixture, PJ_TIME_CONTROLLER_COMMAND_TIMER_START, 5000, 0);
    assert(result.wake_requested && result.wake_changed);
    assert(fixture.wake_armed);
    assert(fixture.wake_deadline.delay_ms == 5000);
    assert(fixture.wake_deadline.source_mask == PJ_TIME_WAKE_TIMER);

    clear_observations(&fixture);
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_TIMER_RESET, 0, 0);
    assert(result.wake_requested && result.wake_changed);
    assert(!fixture.wake_armed);

    clear_observations(&fixture);
    fixture.wake_plan[0] = PJ_TIME_CONTROLLER_WAKE_ERROR;
    fixture.wake_plan[1] = PJ_TIME_CONTROLLER_WAKE_OK;
    fixture.wake_plan_count = 2;
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_TIMER_START, 7000, 0);
    assert(result.wake_result == PJ_TIME_CONTROLLER_WAKE_ERROR);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_WAKE_ERROR);
    assert(controller.wake_dirty);
    fixture.clock.monotonic_ms += 100;
    fixture.clock.wall_utc_ms += 100;
    assert(pj_time_controller_update(&controller, &result));
    assert(result.wake_requested);
    assert(result.wake_result == PJ_TIME_CONTROLLER_WAKE_OK);
    assert(!controller.wake_dirty);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_NONE);
    assert(fixture.wake_deadline.delay_ms == 6900);
}

static void test_media_playback_recording_and_queued_transitions(void)
{
    fixture_t fixture;
    fixture_defaults(&fixture);
    pj_time_controller_t controller = init_default(&fixture);
    clear_observations(&fixture);
    pj_time_controller_result_t result = apply(
        &controller, &fixture, PJ_TIME_CONTROLLER_COMMAND_TIMER_START, 1000, 0);
    assert(!result.media_projected);
    fixture.activity = PJ_TIME_ACTIVITY_PLAYBACK;
    fixture.clock.monotonic_ms += 1000;
    fixture.clock.wall_utc_ms += 1000;
    assert(pj_time_controller_update(&controller, &result));
    assert(result.media_projected);
    assert(result.media_action == PJ_TIME_PREEMPT_PLAYBACK);
    assert(fixture.media_alert.source == PJ_TIME_ALERT_TIMER);
    uint64_t timer_id = fixture.media_alert.id;

    fixture.activity = PJ_TIME_ACTIVITY_RECORDING;
    assert(pj_time_controller_update(&controller, &result));
    assert(result.media_projected);
    assert(result.media_action == PJ_TIME_VISUAL_DEFER_AUDIO);
    assert(fixture.media_alert.id == timer_id);
    assert(pj_time_controller_update(&controller, &result));
    assert(!result.media_projected);

    fixture.clock.monotonic_ms += 1000;
    fixture.clock.wall_utc_ms += 1000;
    assert(apply(&controller, &fixture,
                 PJ_TIME_CONTROLLER_COMMAND_INTERVAL_START, 1000, 1000)
               .command_applied);
    fixture.clock.monotonic_ms += 1000;
    fixture.clock.wall_utc_ms += 1000;
    assert(pj_time_controller_update(&controller, &result));
    assert(controller.state.pending_count == 1);
    assert(controller.state.active_alert.id == timer_id);
    assert(!result.media_projected);

    fixture.activity = PJ_TIME_ACTIVITY_IDLE;
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_ALERT_DISMISS, timer_id, 0);
    assert(result.command_applied && result.media_projected);
    assert(fixture.media_has_alert);
    assert(fixture.media_alert.source == PJ_TIME_ALERT_INTERVAL);
    assert(result.media_action == PJ_TIME_PRESENT);
    uint64_t interval_id = fixture.media_alert.id;
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_ALERT_DISMISS, interval_id, 0);
    assert(result.media_projected);
    assert(!fixture.media_has_alert);
    assert(result.media_action == PJ_TIME_PRESENT);
}

static void test_interval_projects_and_acknowledges_each_round(void)
{
    fixture_t fixture;
    fixture_defaults(&fixture);
    pj_time_controller_t controller = init_default(&fixture);
    assert(apply(&controller, &fixture,
                 PJ_TIME_CONTROLLER_COMMAND_INTERVAL_START, 1000, 1000)
               .command_applied);

    clear_observations(&fixture);
    fixture.clock.monotonic_ms += 1000;
    fixture.clock.wall_utc_ms += 1000;
    pj_time_controller_result_t result;
    assert(pj_time_controller_update(&controller, &result));
    assert(result.media_projected && fixture.media_calls == 1);
    assert(fixture.media_has_alert);
    assert(fixture.media_alert.source == PJ_TIME_ALERT_INTERVAL);
    assert(controller.state.interval_phase == 1);
    uint64_t first_id = fixture.media_alert.id;

    clear_observations(&fixture);
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_ALERT_DISMISS, first_id, 0);
    assert(result.command_applied && result.media_projected);
    assert(!fixture.media_has_alert);
    assert(controller.state.interval.running && controller.state.interval_phase == 1);

    clear_observations(&fixture);
    fixture.clock.monotonic_ms += 1000;
    fixture.clock.wall_utc_ms += 1000;
    assert(pj_time_controller_update(&controller, &result));
    assert(result.media_projected && fixture.media_calls == 1);
    assert(fixture.media_has_alert);
    assert(fixture.media_alert.source == PJ_TIME_ALERT_INTERVAL);
    assert(fixture.media_alert.id != 0 && fixture.media_alert.id != first_id);
    assert(controller.state.interval_phase == 2);
    assert(controller.state.pending_count == 0);

    uint64_t second_id = fixture.media_alert.id;
    clear_observations(&fixture);
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_ALERT_DISMISS, second_id, 0);
    assert(result.command_applied && result.media_projected);
    assert(!fixture.media_has_alert);
    assert(controller.state.interval.running && controller.state.interval_phase == 2);

    clear_observations(&fixture);
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_INTERVAL_RESET, 0, 0);
    assert(result.command_applied && !result.media_projected);
    assert(fixture.media_calls == 0);
    assert(!fixture.media_has_alert);
    assert(controller.state.active_alert.source == PJ_TIME_ALERT_NONE);
    assert(!controller.state.interval.running && controller.state.interval_phase == 0);
}

static void test_monotonic_rollback_resets_cadence_baselines(void)
{
    fixture_t fixture;
    fixture_defaults(&fixture);
    pj_time_controller_t controller = init_default(&fixture);
    assert(apply(&controller, &fixture,
                 PJ_TIME_CONTROLLER_COMMAND_STOPWATCH_START, 0, 0)
               .command_applied);

    clear_observations(&fixture);
    fixture.clock.monotonic_ms = 500;
    pj_time_controller_result_t result;
    assert(pj_time_controller_update(&controller, &result));
    assert(result.state_changed);
    assert(!result.persistence_attempted);
    assert(controller.last_persist_ms == 500);
    assert(controller.state.recovery_time_uncertain);

    fixture.clock.monotonic_ms =
        500 + PJ_TIME_CONTROLLER_CHECKPOINT_MS - 1;
    assert(pj_time_controller_update(&controller, &result));
    assert(!result.persistence_attempted);
    fixture.clock.monotonic_ms++;
    assert(pj_time_controller_update(&controller, &result));
    assert(result.persistence_attempted);
    assert(fixture.saved_state.stopwatch_elapsed_ms ==
           PJ_TIME_CONTROLLER_CHECKPOINT_MS);

    clear_observations(&fixture);
    fixture.save_plan[0] = PJ_TIME_CONTROLLER_SAVE_TRANSIENT_ERROR;
    fixture.save_plan[1] = PJ_TIME_CONTROLLER_SAVE_OK;
    fixture.save_plan_count = 2;
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_STOPWATCH_PAUSE, 0, 0);
    assert(result.persistence_attempted && result.dirty);
    uint64_t failed_at = fixture.clock.monotonic_ms;
    assert(controller.last_retry_ms == failed_at);

    fixture.clock.monotonic_ms = 100;
    assert(pj_time_controller_update(&controller, &result));
    assert(!result.persistence_attempted);
    assert(controller.last_retry_ms == 100);
    fixture.clock.monotonic_ms = 100 + PJ_TIME_CONTROLLER_RETRY_MS - 1;
    assert(pj_time_controller_update(&controller, &result));
    assert(!result.persistence_attempted);
    fixture.clock.monotonic_ms++;
    assert(pj_time_controller_update(&controller, &result));
    assert(result.persistence_attempted && !result.dirty);
    assert(fixture.save_calls == 2);
}

static void test_settings_failure_is_conservative_and_recovers(void)
{
    fixture_t fixture;
    fixture_defaults(&fixture);
    fixture.alarm_ok = 0;
    pj_time_controller_io_t io = fixture_io(&fixture);
    pj_time_controller_t controller;
    pj_time_controller_result_t result;
    assert(!pj_time_controller_init(&controller, &io, &result));
    assert(!result.ready);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_SETTINGS_ERROR);
    assert(fixture.save_calls == 0 && fixture.wake_calls == 0);
    assert(fixture.media_calls == 0);
    assert_events(&fixture, "LBCTSP");

    fixture_defaults(&fixture);
    controller = init_default(&fixture);
    assert(apply(&controller, &fixture,
                 PJ_TIME_CONTROLLER_COMMAND_TIMER_START, 120000, 0)
               .command_applied);
    clear_observations(&fixture);
    fixture.alarm = (pj_time_controller_alarm_settings_t) {
        .enabled = 1,
        .hour = 9,
        .minute = 15,
    };
    fixture.alarm_ok = 0;
    fixture.clock.monotonic_ms = 2000;
    assert(!pj_time_controller_update(&controller, &result));
    assert(result.ready && result.dirty);
    assert(result.state_changed);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_SETTINGS_ERROR);
    assert(controller.state.timer.remaining_ms == 119000);
    assert(!controller.state.alarm_enabled);
    assert(fixture.save_calls == 0 && fixture.wake_calls == 0);
    assert(fixture.media_calls == 0);
    assert_events(&fixture, "CSP");

    clear_observations(&fixture);
    fixture.alarm_ok = 1;
    fixture.clock.monotonic_ms = PJ_TIME_CONTROLLER_RETRY_MS;
    assert(pj_time_controller_update(&controller, &result));
    assert(result.state_changed && result.transition);
    assert(result.persistence_attempted && !result.dirty);
    assert(result.wake_requested);
    assert(controller.state.alarm_enabled);
    assert(controller.state.alarm_hour == 9);
    assert(controller.state.alarm_minute == 15);
    assert(fixture.saved_state.timer.remaining_ms == 61000);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_NONE);
}

static void test_operational_diagnostic_precedence(void)
{
    pj_time_clock_t old_clock = clock_at(1, 0, 100000, 20, 100);
    pj_time_state_t state;
    pj_time_state_defaults(&state, &old_clock);
    assert(pj_time_timer_start(&state, 120000, &old_clock));

    fixture_t fixture;
    fixture_defaults(&fixture);
    fixture_load_state(&fixture, &state);
    fixture.trusted = 0;
    fixture.save_plan[0] = PJ_TIME_CONTROLLER_SAVE_TRANSIENT_ERROR;
    fixture.save_plan_count = 1;
    fixture.wake_plan[0] = PJ_TIME_CONTROLLER_WAKE_ERROR;
    fixture.wake_plan_count = 1;
    pj_time_controller_t controller;
    pj_time_controller_result_t result;
    pj_time_controller_io_t io = fixture_io(&fixture);
    assert(pj_time_controller_init(&controller, &io, &result));
    assert(controller.state.recovery_time_uncertain);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_SAVE_TRANSIENT);

    clear_observations(&fixture);
    fixture.alarm_ok = 0;
    assert(!pj_time_controller_update(&controller, &result));
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_SETTINGS_ERROR);

    clear_observations(&fixture);
    fixture.alarm_ok = 1;
    fixture.wake_plan[0] = PJ_TIME_CONTROLLER_WAKE_ERROR;
    fixture.wake_plan_count = 1;
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_TIMER_START, 60000, 0);
    assert(result.save_result == PJ_TIME_CONTROLLER_SAVE_OK);
    assert(result.wake_result == PJ_TIME_CONTROLLER_WAKE_ERROR);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_WAKE_ERROR);

    clear_observations(&fixture);
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_TIMER_RESET, 0, 0);
    assert(result.wake_result == PJ_TIME_CONTROLLER_WAKE_OK);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_TIME_UNCERTAIN);

    fixture_defaults(&fixture);
    fixture.load_result = PJ_TIME_CONTROLLER_LOAD_CORRUPT;
    fixture.wake_plan[0] = PJ_TIME_CONTROLLER_WAKE_ERROR;
    fixture.wake_plan_count = 1;
    io = fixture_io(&fixture);
    assert(pj_time_controller_init(&controller, &io, &result));
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_WAKE_ERROR);
    clear_observations(&fixture);
    assert(pj_time_controller_time_changed(&controller, 1, &result));
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_LOAD_CORRUPT);
}

static void test_clock_boot_id_must_match_controller(void)
{
    fixture_t fixture;
    fixture_defaults(&fixture);
    fixture.clock_preserve_boot_id = 1;
    fixture.clock.boot_id = 999;
    pj_time_controller_t controller;
    pj_time_controller_result_t result;
    pj_time_controller_io_t io = fixture_io(&fixture);
    assert(!pj_time_controller_init(&controller, &io, &result));
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_CLOCK_ERROR);
    assert(!result.ready);
    assert(fixture.alarm_calls == 0 && fixture.save_calls == 0);
    assert_events(&fixture, "LBCP");

    fixture_defaults(&fixture);
    controller = init_default(&fixture);
    pj_time_state_t before = controller.state;
    clear_observations(&fixture);
    fixture.clock_preserve_boot_id = 1;
    fixture.clock.boot_id = controller.boot_id + 1;
    assert(!pj_time_controller_update(&controller, &result));
    assert(result.ready);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_CLOCK_ERROR);
    assert(memcmp(&before, &controller.state, sizeof(before)) == 0);
    assert_events(&fixture, "CP");

    clear_observations(&fixture);
    fixture.clock_preserve_boot_id = 0;
    assert(pj_time_controller_update(&controller, &result));
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_NONE);
}

static void test_forward_and_backward_civil_changes(void)
{
    fixture_t fixture;
    fixture_defaults(&fixture);
    fixture.clock.local_day = 20;
    fixture.clock.local_second = 6 * 3600 + 59 * 60;
    fixture.alarm = (pj_time_controller_alarm_settings_t) {
        .enabled = 1,
        .hour = 7,
        .minute = 0,
    };
    pj_time_controller_t controller = init_default(&fixture);
    clear_observations(&fixture);

    fixture.clock.monotonic_ms += 120000;
    fixture.clock.wall_utc_ms += 2 * 86400000ll + 120000;
    fixture.clock.local_day = 22;
    fixture.clock.local_second = 9 * 3600;
    pj_time_controller_result_t result;
    assert(pj_time_controller_time_changed(&controller, 1, &result));
    assert(result.transition && result.persistence_attempted);
    assert(controller.state.active_alert.source == PJ_TIME_ALERT_ALARM);
    assert(controller.state.active_alert.recovered);
    uint64_t handled_occurrence = controller.state.active_alert.occurrence;
    uint64_t alert_id = controller.state.active_alert.id;
    assert_events(&fixture, "CSVWAMP");
    assert(apply(&controller, &fixture,
                 PJ_TIME_CONTROLLER_COMMAND_ALERT_DISMISS, alert_id, 0)
               .command_applied);

    clear_observations(&fixture);
    fixture.clock.monotonic_ms += 1000;
    fixture.clock.wall_utc_ms -= 3 * 86400000ll;
    fixture.clock.local_day = 19;
    fixture.clock.local_second = 6 * 3600;
    assert(pj_time_controller_time_changed(&controller, 1, &result));
    assert(controller.state.active_alert.source == PJ_TIME_ALERT_NONE);

    fixture.clock.monotonic_ms += 1000;
    fixture.clock.wall_utc_ms += 3 * 86400000ll;
    fixture.clock.local_day = 22;
    fixture.clock.local_second = 7 * 3600;
    assert(pj_time_controller_time_changed(&controller, 1, &result));
    assert(controller.state.active_alert.source == PJ_TIME_ALERT_NONE);
    assert(controller.state.alarm_last_occurrence == handled_occurrence);

    fixture.clock.monotonic_ms += 86400000;
    fixture.clock.wall_utc_ms += 86400000;
    fixture.clock.local_day = 23;
    assert(pj_time_controller_time_changed(&controller, 1, &result));
    assert(controller.state.active_alert.source == PJ_TIME_ALERT_ALARM);
    assert(controller.state.active_alert.occurrence > handled_occurrence);
}

static void test_callback_operation_ordering(void)
{
    fixture_t fixture;
    fixture_defaults(&fixture);
    pj_time_controller_t controller = init_default(&fixture);
    assert_events(&fixture, "LBCTSVWAMP");

    clear_observations(&fixture);
    pj_time_controller_result_t result = apply(
        &controller, &fixture, PJ_TIME_CONTROLLER_COMMAND_TIMER_START, 1000, 0);
    assert(result.command_applied);
    assert_events(&fixture, "CSVWAP");

    clear_observations(&fixture);
    fixture.clock.monotonic_ms += 1000;
    fixture.clock.wall_utc_ms += 1000;
    assert(pj_time_controller_update(&controller, &result));
    assert(result.transition);
    assert_events(&fixture, "CSVWAMP");

    clear_observations(&fixture);
    fixture.alarm_ok = 0;
    assert(!pj_time_controller_update(&controller, &result));
    assert_events(&fixture, "CSP");
}

static void test_staged_save_failures_preserve_latest_state(void)
{
    fixture_t fixture;
    fixture_defaults(&fixture);
    fixture.save_plan[0] = PJ_TIME_CONTROLLER_SAVE_TRANSIENT_ERROR;
    fixture.save_plan_count = 1;
    pj_time_controller_t controller;
    pj_time_controller_result_t result;
    pj_time_controller_io_t io = fixture_io(&fixture);
    assert(pj_time_controller_init(&controller, &io, &result));
    assert(result.dirty);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_SAVE_TRANSIENT);

    clear_observations(&fixture);
    fixture.save_plan[0] = PJ_TIME_CONTROLLER_SAVE_PERMANENT_ERROR;
    fixture.save_plan_count = 1;
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_STOPWATCH_START, 0, 0);
    assert(result.command_applied && result.dirty);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_SAVE_PERMANENT);
    assert(fixture.saved_state.stopwatch_running);

    clear_observations(&fixture);
    fixture.save_plan[0] = PJ_TIME_CONTROLLER_SAVE_TRANSIENT_ERROR;
    fixture.save_plan_count = 1;
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_TIMER_START, 120000, 0);
    assert(result.command_applied && result.dirty);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_SAVE_TRANSIENT);
    assert(fixture.saved_state.stopwatch_running);
    assert(fixture.saved_state.timer.running);

    clear_observations(&fixture);
    fixture.clock.monotonic_ms += PJ_TIME_CONTROLLER_RETRY_MS;
    fixture.clock.wall_utc_ms += PJ_TIME_CONTROLLER_RETRY_MS;
    assert(pj_time_controller_update(&controller, &result));
    assert(result.persistence_attempted && !result.dirty);
    assert(result.save_result == PJ_TIME_CONTROLLER_SAVE_OK);
    assert(fixture.saved_state.stopwatch_running);
    assert(fixture.saved_state.stopwatch_elapsed_ms ==
           PJ_TIME_CONTROLLER_RETRY_MS);
    assert(fixture.saved_state.timer.remaining_ms ==
           120000 - PJ_TIME_CONTROLLER_RETRY_MS);
    assert(result.diagnostic == PJ_TIME_CONTROLLER_DIAGNOSTIC_NONE);
}

static void test_untrusted_time_disarms_and_retrusted_time_rearms(void)
{
    fixture_t fixture;
    fixture_defaults(&fixture);
    pj_time_controller_t controller = init_default(&fixture);
    pj_time_controller_result_t result = apply(
        &controller, &fixture, PJ_TIME_CONTROLLER_COMMAND_TIMER_START, 60000, 0);
    assert(result.wake_requested && fixture.wake_armed);

    clear_observations(&fixture);
    assert(pj_time_controller_time_changed(&controller, 0, &result));
    assert(result.wake_requested && result.wake_changed);
    assert(!fixture.wake_armed && !controller.wake_armed);
    assert(controller.state.timer.running);

    clear_observations(&fixture);
    fixture.clock.monotonic_ms += 1000;
    fixture.clock.wall_utc_ms += 1000;
    assert(pj_time_controller_time_changed(&controller, 1, &result));
    assert(result.wake_requested && result.wake_changed);
    assert(fixture.wake_armed && controller.wake_armed);
    assert(fixture.wake_deadline.delay_ms == 59000);
}

static void test_idempotent_interval_reset_is_persistence_barrier(void)
{
    fixture_t fixture;
    fixture_defaults(&fixture);
    pj_time_controller_t controller = init_default(&fixture);
    pj_time_controller_result_t result = apply(
        &controller, &fixture, PJ_TIME_CONTROLLER_COMMAND_INTERVAL_START,
        90000, 90000);
    assert(result.command_applied && controller.state.interval.running);

    clear_observations(&fixture);
    fixture.save_plan[0] = PJ_TIME_CONTROLLER_SAVE_TRANSIENT_ERROR;
    fixture.save_plan_count = 1;
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_INTERVAL_RESET, 0, 0);
    assert(result.command_applied && result.persistence_attempted);
    assert(result.save_result == PJ_TIME_CONTROLLER_SAVE_TRANSIENT_ERROR);
    assert(result.dirty && !controller.state.interval.running);
    assert(controller.state.interval.remaining_ms == 0);

    clear_observations(&fixture);
    result = apply(&controller, &fixture,
                   PJ_TIME_CONTROLLER_COMMAND_INTERVAL_RESET, 0, 0);
    assert(result.command_attempted && !result.command_applied);
    assert(!result.state_changed && result.persistence_attempted);
    assert(result.save_result == PJ_TIME_CONTROLLER_SAVE_OK);
    assert(fixture.save_calls == 1 && fixture.wake_calls == 1);
    assert(!result.dirty && !fixture.saved_state.interval.running);
    assert(fixture.saved_state.interval.remaining_ms == 0);
    assert(fixture.saved_state.active_alert.source == PJ_TIME_ALERT_NONE);

    fixture_t reboot;
    fixture_defaults(&reboot);
    reboot.load_result = PJ_TIME_CONTROLLER_LOAD_VALID;
    memcpy(reboot.load_record, fixture.saved_record,
           sizeof(reboot.load_record));
    pj_time_controller_t restored = init_default(&reboot);
    assert(!restored.state.interval.running);
    assert(restored.state.interval.remaining_ms == 0);
    assert(restored.state.active_alert.source == PJ_TIME_ALERT_NONE);
}

int main(void)
{
    test_argument_validation_and_load_classes();
    test_boot_collision_and_boot_failure();
    test_restore_trusted_and_untrusted_elapsed();
    test_alarm_settings_reconcile_persists_and_rearms();
    test_every_command_and_immediate_persistence();
    test_advance_before_alert_commands();
    test_save_failure_latest_state_and_retry();
    test_checkpoint_dirty_retry_and_idle_no_writes();
    test_time_changes_and_recovery_acknowledge();
    test_wake_arm_disarm_and_error_retry();
    test_media_playback_recording_and_queued_transitions();
    test_interval_projects_and_acknowledges_each_round();
    test_monotonic_rollback_resets_cadence_baselines();
    test_settings_failure_is_conservative_and_recovers();
    test_operational_diagnostic_precedence();
    test_clock_boot_id_must_match_controller();
    test_forward_and_backward_civil_changes();
    test_callback_operation_ordering();
    test_staged_save_failures_preserve_latest_state();
    test_untrusted_time_disarms_and_retrusted_time_rearms();
    test_idempotent_interval_reset_is_persistence_barrier();
    puts("time controller tests passed");
    return 0;
}
