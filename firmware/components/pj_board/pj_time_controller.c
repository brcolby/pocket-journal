#include "pj_time_controller.h"

#include <stddef.h>
#include <string.h>

#define PJ_TIME_CONTROLLER_BOOT_ATTEMPTS 64

static int boot_id_collides(const pj_time_state_t *state, uint32_t boot_id)
{
    return boot_id == 0 || state->snooze.anchor_boot_id == boot_id ||
           state->timer.anchor_boot_id == boot_id ||
           state->interval.anchor_boot_id == boot_id ||
           state->stopwatch_anchor_boot_id == boot_id;
}

static int persisted_wall_anchor(const pj_time_state_t *state, int64_t *wall_ms)
{
    int found = 0;
#define CHECK_ANCHOR(running, value) do { \
        if (running) { \
            if (found && *wall_ms != (value)) { \
                return 0; \
            } \
            *wall_ms = (value); \
            found = 1; \
        } \
    } while (0)
    CHECK_ANCHOR(state->snooze.running, state->snooze.anchor_wall_utc_ms);
    CHECK_ANCHOR(state->timer.running, state->timer.anchor_wall_utc_ms);
    CHECK_ANCHOR(state->interval.running, state->interval.anchor_wall_utc_ms);
    CHECK_ANCHOR(state->stopwatch_running, state->stopwatch_anchor_wall_utc_ms);
#undef CHECK_ANCHOR
    return found;
}

static int running(const pj_time_state_t *state)
{
    return state->snooze.running || state->timer.running ||
           state->interval.running || state->stopwatch_running;
}

static int transition_changed(const pj_time_state_t *before,
                              const pj_time_state_t *after)
{
    return memcmp(&before->active_alert, &after->active_alert,
           sizeof(before->active_alert)) != 0 ||
           before->pending_count != after->pending_count ||
           memcmp(before->pending, after->pending,
                  sizeof(before->pending)) != 0 ||
           before->timer.running != after->timer.running ||
           before->interval.running != after->interval.running ||
           before->interval_phase != after->interval_phase ||
           before->snooze.running != after->snooze.running;
}

static pj_time_controller_diagnostic_t diagnostic(
    const pj_time_controller_t *controller)
{
    if (controller->settings_diagnostic != PJ_TIME_CONTROLLER_DIAGNOSTIC_NONE) {
        return controller->settings_diagnostic;
    }
    if (controller->save_diagnostic != PJ_TIME_CONTROLLER_DIAGNOSTIC_NONE) {
        return controller->save_diagnostic;
    }
    if (controller->wake_diagnostic != PJ_TIME_CONTROLLER_DIAGNOSTIC_NONE) {
        return controller->wake_diagnostic;
    }
    if (controller->load_diagnostic != PJ_TIME_CONTROLLER_DIAGNOSTIC_NONE) {
        return controller->load_diagnostic;
    }
    return controller->state.recovery_time_uncertain
        ? PJ_TIME_CONTROLLER_DIAGNOSTIC_TIME_UNCERTAIN
        : PJ_TIME_CONTROLLER_DIAGNOSTIC_NONE;
}

static void finish_result(pj_time_controller_t *controller,
                          pj_time_controller_result_t *result)
{
    result->ready = controller->ready;
    result->dirty = controller->dirty;
    result->diagnostic = diagnostic(controller);
    if (controller->io.publish_status != NULL) {
        controller->io.publish_status(controller->io.context, result);
    }
}

static int reconcile_alarm(pj_time_controller_t *controller,
                           const pj_time_clock_t *clock)
{
    pj_time_controller_alarm_settings_t settings;
    if (controller->io.alarm_settings == NULL ||
        !controller->io.alarm_settings(controller->io.context, &settings)) {
        controller->settings_diagnostic =
            PJ_TIME_CONTROLLER_DIAGNOSTIC_SETTINGS_ERROR;
        return -1;
    }
    controller->settings_diagnostic = PJ_TIME_CONTROLLER_DIAGNOSTIC_NONE;
    if (controller->state.alarm_enabled == settings.enabled &&
        controller->state.alarm_hour == settings.hour &&
        controller->state.alarm_minute == settings.minute) {
        return 0;
    }
    return pj_time_alarm_configure(&controller->state, settings.enabled,
                                   settings.hour, settings.minute, clock);
}

static int take_clock(pj_time_controller_t *controller, pj_time_clock_t *clock,
                      pj_time_controller_result_t *result)
{
    if (controller->io.clock != NULL &&
        controller->io.clock(controller->io.context, controller->boot_id, clock) &&
        pj_time_clock_valid(clock) && clock->boot_id == controller->boot_id) {
        return 1;
    }
    result->ready = controller->ready;
    result->dirty = controller->dirty;
    result->diagnostic = PJ_TIME_CONTROLLER_DIAGNOSTIC_CLOCK_ERROR;
    if (controller->io.publish_status != NULL) {
        controller->io.publish_status(controller->io.context, result);
    }
    return 0;
}

static void persist(pj_time_controller_t *controller,
                    const pj_time_clock_t *clock, int force,
                    pj_time_controller_result_t *result)
{
    uint64_t now = clock->monotonic_ms;
    if (!force) {
        if (controller->dirty && now < controller->last_retry_ms) {
            controller->last_retry_ms = now;
            return;
        }
        if (!controller->dirty && now < controller->last_persist_ms) {
            controller->last_persist_ms = now;
            return;
        }
        if (controller->dirty &&
            now - controller->last_retry_ms < PJ_TIME_CONTROLLER_RETRY_MS) {
            return;
        }
        if (!controller->dirty &&
            (!running(&controller->state) ||
             now - controller->last_persist_ms <
                 PJ_TIME_CONTROLLER_CHECKPOINT_MS)) {
            return;
        }
    }

    uint8_t record[PJ_TIME_STATE_RECORD_BYTES];
    pj_time_controller_save_result_t save_result =
        PJ_TIME_CONTROLLER_SAVE_PERMANENT_ERROR;
    if (controller->io.save != NULL &&
        pj_time_state_encode(&controller->state, record, sizeof(record)) ==
            sizeof(record)) {
        save_result = controller->io.save(controller->io.context, record);
    }
    result->persistence_attempted = 1;
    result->save_result = save_result;
    if (save_result == PJ_TIME_CONTROLLER_SAVE_OK) {
        controller->dirty = 0;
        controller->last_persist_ms = now;
        controller->save_diagnostic = PJ_TIME_CONTROLLER_DIAGNOSTIC_NONE;
    } else {
        controller->dirty = 1;
        controller->last_retry_ms = now;
        controller->save_diagnostic =
            save_result == PJ_TIME_CONTROLLER_SAVE_TRANSIENT_ERROR
                ? PJ_TIME_CONTROLLER_DIAGNOSTIC_SAVE_TRANSIENT
                : PJ_TIME_CONTROLLER_DIAGNOSTIC_SAVE_PERMANENT;
    }
}

static void schedule_wake(pj_time_controller_t *controller,
                          const pj_time_clock_t *clock,
                          pj_time_controller_result_t *result)
{
    pj_time_wake_deadline_t deadline;
    int armed = controller->wall_time_trusted &&
        pj_time_next_wake(&controller->state, clock, &deadline) &&
        deadline.delay_ms != UINT64_MAX && deadline.source_mask != 0;
    int changed = armed != controller->wake_armed ||
        (armed && (deadline.fingerprint != controller->wake_fingerprint ||
                   deadline.source_mask != controller->wake_source_mask));
    result->wake_requested = 1;
    result->wake_changed = changed;
    result->wake_result = controller->io.schedule_wake == NULL
        ? PJ_TIME_CONTROLLER_WAKE_UNAVAILABLE
        : controller->io.schedule_wake(controller->io.context,
                                       armed ? &deadline : NULL);
    if (result->wake_result == PJ_TIME_CONTROLLER_WAKE_OK ||
        result->wake_result == PJ_TIME_CONTROLLER_WAKE_UNAVAILABLE) {
        controller->wake_dirty = 0;
        controller->wake_diagnostic = PJ_TIME_CONTROLLER_DIAGNOSTIC_NONE;
        controller->wake_armed = armed;
        controller->wake_fingerprint = armed ? deadline.fingerprint : 0;
        controller->wake_source_mask = armed ? deadline.source_mask : 0;
    } else {
        controller->wake_dirty = 1;
        controller->wake_diagnostic = PJ_TIME_CONTROLLER_DIAGNOSTIC_WAKE_ERROR;
    }
}

static void project_media(pj_time_controller_t *controller,
                          pj_time_controller_result_t *result)
{
    const pj_time_alert_t *alert = pj_time_active_alert(&controller->state);
    pj_time_activity_t activity = controller->io.activity == NULL
        ? PJ_TIME_ACTIVITY_IDLE
        : controller->io.activity(controller->io.context);
    pj_time_conflict_action_t action = alert == NULL ? PJ_TIME_PRESENT
        : pj_time_alert_conflict_action((pj_time_alert_source_t)alert->source,
                                        activity);
    uint64_t alert_id = alert == NULL ? 0 : alert->id;
    if (!controller->media_valid || alert_id != controller->media_alert_id ||
        action != controller->media_action) {
        controller->media_valid = 1;
        controller->media_alert_id = alert_id;
        controller->media_action = action;
        result->media_projected = 1;
        if (controller->io.project_media != NULL) {
            controller->io.project_media(controller->io.context, alert, action);
        }
    }
    result->media_action = action;
}

static int advance_and_reconcile(pj_time_controller_t *controller,
                                 pj_time_clock_t *clock,
                                 pj_time_state_t *before,
                                 pj_time_controller_result_t *result)
{
    *before = controller->state;
    if (!take_clock(controller, clock, result)) {
        return 0;
    }
    (void)pj_time_advance(&controller->state, clock);
    int alarm_changed = reconcile_alarm(controller, clock);
    result->state_changed =
        memcmp(before, &controller->state, sizeof(*before)) != 0;
    if (alarm_changed < 0) {
        controller->dirty |= result->state_changed;
        result->dirty = controller->dirty;
        result->ready = controller->ready;
        result->diagnostic = diagnostic(controller);
        if (controller->io.publish_status != NULL) {
            controller->io.publish_status(controller->io.context, result);
        }
        return 0;
    }
    result->transition = alarm_changed ||
        transition_changed(before, &controller->state);
    return 1;
}

static int classify_record(pj_time_controller_load_result_t load_result,
                           const uint8_t *record, pj_time_state_t *state)
{
    if (load_result != PJ_TIME_CONTROLLER_LOAD_VALID) {
        return load_result;
    }
    if (record[0] != 'P' || record[1] != 'J' || record[2] != 'T' ||
        record[3] != 'M') {
        return PJ_TIME_CONTROLLER_LOAD_CORRUPT;
    }
    if (record[4] != 1) {
        return PJ_TIME_CONTROLLER_LOAD_INCOMPATIBLE;
    }
    return pj_time_state_decode(record, PJ_TIME_STATE_RECORD_BYTES, state)
        ? PJ_TIME_CONTROLLER_LOAD_VALID
        : PJ_TIME_CONTROLLER_LOAD_CORRUPT;
}

int pj_time_controller_init(pj_time_controller_t *controller,
                            const pj_time_controller_io_t *io,
                            pj_time_controller_result_t *result)
{
    if (controller == NULL || io == NULL || result == NULL || io->load == NULL ||
        io->save == NULL || io->next_boot_id == NULL || io->clock == NULL ||
        io->alarm_settings == NULL) {
        return 0;
    }
    memset(controller, 0, sizeof(*controller));
    memset(result, 0, sizeof(*result));
    controller->io = *io;

    uint8_t record[PJ_TIME_STATE_RECORD_BYTES] = {0};
    pj_time_state_t loaded = {0};
    pj_time_controller_load_result_t load_result =
        io->load(io->context, record);
    load_result = (pj_time_controller_load_result_t)
        classify_record(load_result, record, &loaded);
    result->load_result = load_result;
    if (load_result == PJ_TIME_CONTROLLER_LOAD_IO_ERROR) {
        controller->load_diagnostic =
            PJ_TIME_CONTROLLER_DIAGNOSTIC_LOAD_IO_ERROR;
        finish_result(controller, result);
        return 0;
    }
    int restored = load_result == PJ_TIME_CONTROLLER_LOAD_VALID;
    if (load_result == PJ_TIME_CONTROLLER_LOAD_CORRUPT) {
        controller->load_diagnostic =
            PJ_TIME_CONTROLLER_DIAGNOSTIC_LOAD_CORRUPT;
    } else if (load_result == PJ_TIME_CONTROLLER_LOAD_INCOMPATIBLE) {
        controller->load_diagnostic =
            PJ_TIME_CONTROLLER_DIAGNOSTIC_LOAD_INCOMPATIBLE;
    } else if (!restored && load_result != PJ_TIME_CONTROLLER_LOAD_NOT_FOUND) {
        controller->load_diagnostic =
            PJ_TIME_CONTROLLER_DIAGNOSTIC_LOAD_CORRUPT;
    }
    if (restored) {
        controller->state = loaded;
    }

    for (int attempt = 0; attempt < PJ_TIME_CONTROLLER_BOOT_ATTEMPTS; ++attempt) {
        uint32_t candidate = io->next_boot_id(io->context);
        if (!boot_id_collides(restored ? &loaded : &controller->state,
                              candidate)) {
            controller->boot_id = candidate;
            break;
        }
    }
    if (controller->boot_id == 0) {
        controller->load_diagnostic =
            PJ_TIME_CONTROLLER_DIAGNOSTIC_BOOT_ID_ERROR;
        finish_result(controller, result);
        return 0;
    }

    pj_time_clock_t clock;
    if (!take_clock(controller, &clock, result)) {
        return 0;
    }
    controller->wall_time_trusted = io->wall_time_trusted != NULL &&
        io->wall_time_trusted(io->context);
    if (!restored) {
        pj_time_state_defaults(&controller->state, &clock);
    } else {
        int64_t persisted_wall_ms = 0;
        if (!controller->wall_time_trusted) {
            clock.reboot_elapsed_valid = 0;
            clock.reboot_elapsed_ms = 0;
        }
        if (!clock.reboot_elapsed_valid && controller->wall_time_trusted &&
            persisted_wall_anchor(&controller->state, &persisted_wall_ms) &&
            clock.wall_utc_ms >= persisted_wall_ms) {
            clock.reboot_elapsed_valid = 1;
            clock.reboot_elapsed_ms =
                (uint64_t)(clock.wall_utc_ms - persisted_wall_ms);
        }
        (void)pj_time_advance(&controller->state, &clock);
    }
    if (reconcile_alarm(controller, &clock) < 0) {
        finish_result(controller, result);
        return 0;
    }
    if (!pj_time_state_valid(&controller->state)) {
        controller->load_diagnostic =
            PJ_TIME_CONTROLLER_DIAGNOSTIC_LOAD_CORRUPT;
        finish_result(controller, result);
        return 0;
    }
    controller->ready = 1;
    result->state_changed = !restored ||
        memcmp(&loaded, &controller->state, sizeof(loaded)) != 0;
    persist(controller, &clock, 1, result);
    schedule_wake(controller, &clock, result);
    project_media(controller, result);
    finish_result(controller, result);
    return 1;
}

int pj_time_controller_update(pj_time_controller_t *controller,
                              pj_time_controller_result_t *result)
{
    if (controller == NULL || result == NULL || !controller->ready) {
        return 0;
    }
    memset(result, 0, sizeof(*result));
    pj_time_clock_t clock;
    pj_time_state_t before;
    if (!advance_and_reconcile(controller, &clock, &before, result)) {
        return 0;
    }
    if (result->transition) {
        persist(controller, &clock, 1, result);
    } else if (result->state_changed || controller->dirty) {
        persist(controller, &clock, 0, result);
    }
    if (result->transition || controller->wake_dirty) {
        schedule_wake(controller, &clock, result);
    }
    project_media(controller, result);
    finish_result(controller, result);
    return 1;
}

static int apply_command(pj_time_controller_t *controller,
                         const pj_time_controller_command_t *command,
                         const pj_time_clock_t *clock)
{
    pj_time_state_t before = controller->state;
    switch (command->type) {
    case PJ_TIME_CONTROLLER_COMMAND_ALERT_DISMISS:
        (void)pj_time_alert_dismiss(&controller->state, command->alert_id);
        break;
    case PJ_TIME_CONTROLLER_COMMAND_ALARM_SNOOZE:
        (void)pj_time_alarm_snooze(&controller->state, command->alert_id,
                                   command->duration_ms != 0
                                       ? command->duration_ms
                                       : PJ_TIME_CONTROLLER_SNOOZE_MS,
                                   clock);
        break;
    case PJ_TIME_CONTROLLER_COMMAND_RECOVERY_ACKNOWLEDGE:
        pj_time_recovery_acknowledge(&controller->state);
        break;
    case PJ_TIME_CONTROLLER_COMMAND_STOPWATCH_START:
        (void)pj_time_stopwatch_start(&controller->state, clock);
        break;
    case PJ_TIME_CONTROLLER_COMMAND_STOPWATCH_PAUSE:
        (void)pj_time_stopwatch_pause(&controller->state, clock);
        break;
    case PJ_TIME_CONTROLLER_COMMAND_STOPWATCH_RESET:
        pj_time_stopwatch_reset(&controller->state);
        break;
    case PJ_TIME_CONTROLLER_COMMAND_TIMER_START:
        (void)pj_time_timer_start(&controller->state, command->duration_ms,
                                  clock);
        break;
    case PJ_TIME_CONTROLLER_COMMAND_TIMER_PAUSE:
        (void)pj_time_timer_pause(&controller->state, clock);
        break;
    case PJ_TIME_CONTROLLER_COMMAND_TIMER_RESET:
        pj_time_timer_reset(&controller->state);
        break;
    case PJ_TIME_CONTROLLER_COMMAND_INTERVAL_START:
        if (!controller->state.interval.running &&
            controller->state.interval.remaining_ms != 0 &&
            command->duration_ms / 1000u ==
                controller->state.interval.remaining_ms / 1000u +
                (controller->state.interval.remaining_ms % 1000u != 0)) {
            (void)pj_time_interval_resume(&controller->state, clock);
        } else {
            (void)pj_time_interval_start(&controller->state,
                                         command->duration_ms,
                                         command->secondary_duration_ms,
                                         clock);
        }
        break;
    case PJ_TIME_CONTROLLER_COMMAND_INTERVAL_PAUSE:
        (void)pj_time_interval_pause(&controller->state, clock);
        break;
    case PJ_TIME_CONTROLLER_COMMAND_INTERVAL_RESET:
        pj_time_interval_reset(&controller->state);
        break;
    default:
        return 0;
    }
    return memcmp(&before, &controller->state, sizeof(before)) != 0;
}

int pj_time_controller_apply(pj_time_controller_t *controller,
                             const pj_time_controller_command_t *command,
                             pj_time_controller_result_t *result)
{
    if (controller == NULL || command == NULL || result == NULL ||
        !controller->ready) {
        return 0;
    }
    memset(result, 0, sizeof(*result));
    result->command_attempted = 1;
    pj_time_clock_t clock;
    pj_time_state_t before;
    if (!advance_and_reconcile(controller, &clock, &before, result)) {
        return 0;
    }
    result->command_applied = apply_command(controller, command, &clock);
    result->state_changed =
        memcmp(&before, &controller->state, sizeof(before)) != 0;
    result->transition = result->transition ||
        transition_changed(&before, &controller->state);
    if (result->command_applied || result->transition) {
        persist(controller, &clock, 1, result);
        schedule_wake(controller, &clock, result);
    } else if (result->state_changed || controller->dirty) {
        persist(controller, &clock, 0, result);
        if (controller->wake_dirty) {
            schedule_wake(controller, &clock, result);
        }
    }
    project_media(controller, result);
    finish_result(controller, result);
    return 1;
}

int pj_time_controller_time_changed(pj_time_controller_t *controller,
                                    int wall_time_trusted,
                                    pj_time_controller_result_t *result)
{
    if (controller == NULL || result == NULL || !controller->ready) {
        return 0;
    }
    memset(result, 0, sizeof(*result));
    controller->wall_time_trusted = wall_time_trusted != 0;
    pj_time_clock_t clock;
    pj_time_state_t before;
    if (!advance_and_reconcile(controller, &clock, &before, result)) {
        return 0;
    }
    persist(controller, &clock, 1, result);
    schedule_wake(controller, &clock, result);
    project_media(controller, result);
    finish_result(controller, result);
    return 1;
}

const pj_time_state_t *pj_time_controller_state(
    const pj_time_controller_t *controller)
{
    return controller != NULL && controller->ready ? &controller->state : NULL;
}

int pj_time_controller_ready(const pj_time_controller_t *controller)
{
    return controller != NULL && controller->ready;
}
