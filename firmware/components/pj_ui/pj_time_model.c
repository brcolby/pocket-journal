#include "pj_time_model.h"

#include <limits.h>
#include <string.h>

#define PJ_TIME_RECORD_VERSION 1u
#define PJ_TIME_RECORD_CRC_OFFSET (PJ_TIME_STATE_RECORD_BYTES - 4u)
#define PJ_TIME_MAX_LOCAL_MINUTE ((int64_t)INT32_MAX * 1440 + 1439)

typedef struct {
    uint8_t *data;
    size_t offset;
} writer_t;

typedef struct {
    const uint8_t *data;
    size_t offset;
} reader_t;

static int source_valid(uint8_t source)
{
    return source >= PJ_TIME_ALERT_ALARM && source <= PJ_TIME_ALERT_INTERVAL;
}

static int alert_valid(const pj_time_alert_t *alert, int allow_empty)
{
    if (alert->source == PJ_TIME_ALERT_NONE) {
        return allow_empty && alert->id == 0 && alert->occurrence == 0 &&
               alert->skipped_occurrences == 0 && alert->reason == 0 && alert->recovered == 0;
    }
    return source_valid(alert->source) && alert->id != 0 &&
           alert->reason >= PJ_TIME_ALERT_SCHEDULED &&
           alert->reason <= PJ_TIME_ALERT_EXPIRED && alert->recovered <= 1;
}

int pj_time_clock_valid(const pj_time_clock_t *clock)
{
    return clock != NULL && clock->boot_id != 0 && clock->local_day >= 0 &&
           clock->local_second < 86400u &&
           clock->reboot_elapsed_valid <= 1;
}

static int countdown_valid(const pj_time_countdown_t *countdown)
{
    return countdown->running <= 1 && countdown->remaining_ms <= PJ_TIME_MAX_DURATION_MS &&
           (!countdown->running ||
            (countdown->anchor_boot_id != 0 && countdown->remaining_ms != 0));
}

static int64_t local_minute(const pj_time_clock_t *clock)
{
    return (int64_t)clock->local_day * 1440 + (int64_t)(clock->local_second / 60u);
}

void pj_time_state_defaults(pj_time_state_t *state, const pj_time_clock_t *clock)
{
    if (state == NULL || !pj_time_clock_valid(clock)) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->alarm_hour = 7;
    state->alarm_minute = 30;
    state->alarm_generation = 1;
    state->alarm_last_local_minute = local_minute(clock);
    state->next_alert_id = 1;
}

int pj_time_state_valid(const pj_time_state_t *state)
{
    if (state == NULL || state->alarm_enabled > 1 || state->alarm_hour > 23 ||
        state->alarm_minute > 59 || state->alarm_generation == 0 ||
        state->alarm_last_local_minute < 0 ||
        state->alarm_last_local_minute > PJ_TIME_MAX_LOCAL_MINUTE ||
        !countdown_valid(&state->snooze) || !countdown_valid(&state->timer) ||
        !countdown_valid(&state->interval) || state->interval_work_ms > PJ_TIME_MAX_DURATION_MS ||
        state->interval_rest_ms > PJ_TIME_MAX_DURATION_MS ||
        (state->interval.running &&
         (state->interval_work_ms == 0 || state->interval_rest_ms == 0)) ||
        state->stopwatch_running > 1 ||
        (state->stopwatch_running && state->stopwatch_anchor_boot_id == 0) ||
        state->next_alert_id == 0 || state->pending_count > PJ_TIME_PENDING_ALERTS ||
        state->recovery_time_uncertain > 1 || !alert_valid(&state->active_alert, 1)) {
        return 0;
    }
    if (state->alarm_last_occurrence != 0 &&
        ((uint32_t)(state->alarm_last_occurrence >> 32u) != state->alarm_generation ||
         (uint32_t)state->alarm_last_occurrence > INT32_MAX)) {
        return 0;
    }
    if (state->pending_count > 0 && state->active_alert.source == PJ_TIME_ALERT_NONE) {
        return 0;
    }
    uint64_t maximum_alert_id = state->active_alert.id;
    for (uint8_t i = 0; i < state->pending_count; i++) {
        if (!alert_valid(&state->pending[i], 0)) {
            return 0;
        }
        if (state->active_alert.source == state->pending[i].source) {
            return 0;
        }
        if (state->active_alert.id == state->pending[i].id) {
            return 0;
        }
        for (uint8_t j = 0; j < i; j++) {
            if (state->pending[j].source == state->pending[i].source ||
                state->pending[j].id == state->pending[i].id) {
                return 0;
            }
        }
        if (state->pending[i].id > maximum_alert_id) {
            maximum_alert_id = state->pending[i].id;
        }
    }
    for (uint8_t i = state->pending_count; i < PJ_TIME_PENDING_ALERTS; i++) {
        if (state->pending[i].source != PJ_TIME_ALERT_NONE ||
            !alert_valid(&state->pending[i], 1)) {
            return 0;
        }
    }
    return state->next_alert_id > maximum_alert_id;
}

static void countdown_anchor(pj_time_countdown_t *countdown, const pj_time_clock_t *clock)
{
    countdown->anchor_boot_id = clock->boot_id;
    countdown->anchor_monotonic_ms = clock->monotonic_ms;
    countdown->anchor_wall_utc_ms = clock->wall_utc_ms;
}

static uint64_t elapsed_since(uint32_t boot_id, uint64_t monotonic_ms,
                              const pj_time_clock_t *clock, uint8_t *uncertain)
{
    if (boot_id == clock->boot_id) {
        if (clock->monotonic_ms >= monotonic_ms) {
            return clock->monotonic_ms - monotonic_ms;
        }
        *uncertain = 1;
        return 0;
    }
    if (clock->reboot_elapsed_valid) {
        return clock->reboot_elapsed_ms;
    }
    *uncertain = 1;
    return 0;
}

static uint64_t countdown_consume(pj_time_countdown_t *countdown,
                                  const pj_time_clock_t *clock, uint8_t *uncertain)
{
    if (!countdown->running) {
        return 0;
    }
    uint64_t elapsed = elapsed_since(countdown->anchor_boot_id,
                                     countdown->anchor_monotonic_ms, clock, uncertain);
    countdown_anchor(countdown, clock);
    if (elapsed >= countdown->remaining_ms) {
        uint64_t overrun = elapsed - countdown->remaining_ms;
        countdown->remaining_ms = 0;
        countdown->running = 0;
        return overrun + 1u;
    }
    countdown->remaining_ms -= elapsed;
    return 0;
}

static int alert_priority(uint8_t source)
{
    return source == PJ_TIME_ALERT_ALARM ? 3 : source == PJ_TIME_ALERT_TIMER ? 2 : 1;
}

static int enqueue_alert(pj_time_state_t *state, uint8_t source, uint8_t reason,
                         uint64_t occurrence, uint32_t skipped, int recovered)
{
    if (state->active_alert.source == source) {
        state->active_alert.occurrence = occurrence;
        uint64_t additional = (uint64_t)skipped + 1u;
        if (additional > UINT32_MAX - state->active_alert.skipped_occurrences) {
            state->active_alert.skipped_occurrences = UINT32_MAX;
        } else {
            state->active_alert.skipped_occurrences += (uint32_t)additional;
        }
        state->active_alert.recovered = 1;
        return 1;
    }
    for (uint8_t i = 0; i < state->pending_count; i++) {
        if (state->pending[i].source == source) {
            state->pending[i].occurrence = occurrence;
            uint64_t additional = (uint64_t)skipped + 1u;
            if (additional > UINT32_MAX - state->pending[i].skipped_occurrences) {
                state->pending[i].skipped_occurrences = UINT32_MAX;
            } else {
                state->pending[i].skipped_occurrences += (uint32_t)additional;
            }
            state->pending[i].recovered = 1;
            return 1;
        }
    }
    if (state->next_alert_id == UINT64_MAX) {
        return 0;
    }
    pj_time_alert_t alert = {
        .id = state->next_alert_id++,
        .occurrence = occurrence,
        .skipped_occurrences = skipped,
        .source = source,
        .reason = reason,
        .recovered = recovered ? 1 : 0,
    };
    if (state->active_alert.source == PJ_TIME_ALERT_NONE) {
        state->active_alert = alert;
        return 1;
    }
    if (alert_priority(source) > alert_priority(state->active_alert.source)) {
        if (state->pending_count < PJ_TIME_PENDING_ALERTS) {
            state->pending[state->pending_count++] = state->active_alert;
        }
        state->active_alert = alert;
        return 1;
    }
    if (state->pending_count < PJ_TIME_PENDING_ALERTS) {
        state->pending[state->pending_count++] = alert;
    }
    return 1;
}

static void promote_alert(pj_time_state_t *state)
{
    memset(&state->active_alert, 0, sizeof(state->active_alert));
    if (state->pending_count == 0) {
        return;
    }
    uint8_t best = 0;
    for (uint8_t i = 1; i < state->pending_count; i++) {
        int priority = alert_priority(state->pending[i].source);
        int best_priority = alert_priority(state->pending[best].source);
        if (priority > best_priority ||
            (priority == best_priority && state->pending[i].id < state->pending[best].id)) {
            best = i;
        }
    }
    state->active_alert = state->pending[best];
    for (uint8_t i = best + 1; i < state->pending_count; i++) {
        state->pending[i - 1] = state->pending[i];
    }
    state->pending_count--;
    memset(&state->pending[state->pending_count], 0, sizeof(state->pending[0]));
}

static void remove_source_alerts(pj_time_state_t *state, uint8_t source)
{
    if (state->active_alert.source == source) {
        promote_alert(state);
    }
    for (uint8_t i = 0; i < state->pending_count;) {
        if (state->pending[i].source != source) {
            i++;
            continue;
        }
        for (uint8_t j = i + 1; j < state->pending_count; j++) {
            state->pending[j - 1] = state->pending[j];
        }
        state->pending_count--;
        memset(&state->pending[state->pending_count], 0, sizeof(state->pending[0]));
    }
}

int pj_time_alarm_configure(pj_time_state_t *state, int enabled, int hour, int minute,
                            const pj_time_clock_t *clock)
{
    if (!pj_time_state_valid(state) || !pj_time_clock_valid(clock) ||
        (enabled != 0 && enabled != 1) || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return 0;
    }
    state->alarm_enabled = (uint8_t)enabled;
    state->alarm_hour = (uint8_t)hour;
    state->alarm_minute = (uint8_t)minute;
    state->alarm_generation++;
    if (state->alarm_generation == 0) {
        state->alarm_generation = 1;
    }
    state->alarm_last_local_minute = local_minute(clock);
    state->alarm_last_occurrence = 0;
    memset(&state->snooze, 0, sizeof(state->snooze));
    remove_source_alerts(state, PJ_TIME_ALERT_ALARM);
    return 1;
}

int pj_time_timer_start(pj_time_state_t *state, uint64_t duration_ms,
                        const pj_time_clock_t *clock)
{
    if (!pj_time_state_valid(state) || !pj_time_clock_valid(clock) ||
        duration_ms == 0 || duration_ms > PJ_TIME_MAX_DURATION_MS) {
        return 0;
    }
    state->timer.running = 1;
    state->timer.remaining_ms = duration_ms;
    countdown_anchor(&state->timer, clock);
    remove_source_alerts(state, PJ_TIME_ALERT_TIMER);
    return 1;
}

int pj_time_timer_pause(pj_time_state_t *state, const pj_time_clock_t *clock)
{
    if (!pj_time_state_valid(state) || !pj_time_clock_valid(clock)) {
        return 0;
    }
    (void)pj_time_advance(state, clock);
    state->timer.running = 0;
    return 1;
}

void pj_time_timer_reset(pj_time_state_t *state)
{
    if (pj_time_state_valid(state)) {
        memset(&state->timer, 0, sizeof(state->timer));
        remove_source_alerts(state, PJ_TIME_ALERT_TIMER);
    }
}

int pj_time_interval_start(pj_time_state_t *state, uint64_t work_ms, uint64_t rest_ms,
                           const pj_time_clock_t *clock)
{
    if (!pj_time_state_valid(state) || !pj_time_clock_valid(clock) || work_ms == 0 ||
        rest_ms == 0 || work_ms > PJ_TIME_MAX_DURATION_MS || rest_ms > PJ_TIME_MAX_DURATION_MS) {
        return 0;
    }
    state->interval_work_ms = work_ms;
    state->interval_rest_ms = rest_ms;
    state->interval_phase = 0;
    state->interval.running = 1;
    state->interval.remaining_ms = work_ms;
    countdown_anchor(&state->interval, clock);
    remove_source_alerts(state, PJ_TIME_ALERT_INTERVAL);
    return 1;
}

int pj_time_interval_pause(pj_time_state_t *state, const pj_time_clock_t *clock)
{
    if (!pj_time_state_valid(state) || !pj_time_clock_valid(clock)) {
        return 0;
    }
    (void)pj_time_advance(state, clock);
    state->interval.running = 0;
    return 1;
}

int pj_time_interval_resume(pj_time_state_t *state, const pj_time_clock_t *clock)
{
    if (!pj_time_state_valid(state) || !pj_time_clock_valid(clock) ||
        state->interval.running || state->interval.remaining_ms == 0 ||
        state->interval_work_ms == 0 || state->interval_rest_ms == 0) {
        return 0;
    }
    state->interval.running = 1;
    countdown_anchor(&state->interval, clock);
    remove_source_alerts(state, PJ_TIME_ALERT_INTERVAL);
    return 1;
}

void pj_time_interval_reset(pj_time_state_t *state)
{
    if (pj_time_state_valid(state)) {
        memset(&state->interval, 0, sizeof(state->interval));
        state->interval_phase = 0;
        remove_source_alerts(state, PJ_TIME_ALERT_INTERVAL);
    }
}

int pj_time_stopwatch_start(pj_time_state_t *state, const pj_time_clock_t *clock)
{
    if (!pj_time_state_valid(state) || !pj_time_clock_valid(clock)) {
        return 0;
    }
    if (!state->stopwatch_running) {
        state->stopwatch_running = 1;
        state->stopwatch_anchor_boot_id = clock->boot_id;
        state->stopwatch_anchor_monotonic_ms = clock->monotonic_ms;
        state->stopwatch_anchor_wall_utc_ms = clock->wall_utc_ms;
    }
    return 1;
}

static void stopwatch_checkpoint(pj_time_state_t *state, const pj_time_clock_t *clock)
{
    if (!state->stopwatch_running) {
        return;
    }
    uint64_t elapsed = elapsed_since(state->stopwatch_anchor_boot_id,
                                     state->stopwatch_anchor_monotonic_ms, clock,
                                     &state->recovery_time_uncertain);
    if (UINT64_MAX - state->stopwatch_elapsed_ms < elapsed) {
        state->stopwatch_elapsed_ms = UINT64_MAX;
    } else {
        state->stopwatch_elapsed_ms += elapsed;
    }
    state->stopwatch_anchor_boot_id = clock->boot_id;
    state->stopwatch_anchor_monotonic_ms = clock->monotonic_ms;
    state->stopwatch_anchor_wall_utc_ms = clock->wall_utc_ms;
}

int pj_time_stopwatch_pause(pj_time_state_t *state, const pj_time_clock_t *clock)
{
    if (!pj_time_state_valid(state) || !pj_time_clock_valid(clock)) {
        return 0;
    }
    stopwatch_checkpoint(state, clock);
    state->stopwatch_running = 0;
    return 1;
}

void pj_time_stopwatch_reset(pj_time_state_t *state)
{
    if (state != NULL) {
        state->stopwatch_running = 0;
        state->stopwatch_elapsed_ms = 0;
        state->stopwatch_anchor_boot_id = 0;
        state->stopwatch_anchor_monotonic_ms = 0;
        state->stopwatch_anchor_wall_utc_ms = 0;
    }
}

void pj_time_recovery_acknowledge(pj_time_state_t *state)
{
    if (state != NULL) {
        state->recovery_time_uncertain = 0;
    }
}

static int advance_alarm(pj_time_state_t *state, const pj_time_clock_t *clock)
{
    int64_t now = local_minute(clock);
    int64_t previous = state->alarm_last_local_minute;
    state->alarm_last_local_minute = now;
    if (!state->alarm_enabled || now <= previous) {
        return 0;
    }
    int64_t minute_of_day = (int64_t)state->alarm_hour * 60 + state->alarm_minute;
    int64_t due_day = now / 1440;
    int64_t due = due_day * 1440 + minute_of_day;
    if (due > now) {
        due -= 1440;
        due_day--;
    }
    if (due <= previous) {
        return 0;
    }
    uint64_t occurrence = ((uint64_t)state->alarm_generation << 32u) |
        (uint32_t)due_day;
    if (occurrence <= state->alarm_last_occurrence) {
        return 0;
    }
    state->alarm_last_occurrence = occurrence;
    int64_t first_due_day = (previous - minute_of_day) / 1440;
    if ((previous - minute_of_day) < 0 && (previous - minute_of_day) % 1440 != 0) {
        first_due_day--;
    }
    first_due_day++;
    uint64_t crossed = (uint64_t)(due_day - first_due_day + 1);
    uint32_t skipped = crossed > UINT32_MAX ? UINT32_MAX : (uint32_t)(crossed - 1u);
    return enqueue_alert(state, PJ_TIME_ALERT_ALARM, PJ_TIME_ALERT_SCHEDULED,
                         occurrence, skipped, now > due);
}

static int advance_snooze(pj_time_state_t *state, const pj_time_clock_t *clock)
{
    uint32_t boot_before = state->snooze.anchor_boot_id;
    uint8_t uncertain_before = state->recovery_time_uncertain;
    uint64_t expired = countdown_consume(&state->snooze, clock,
                                         &state->recovery_time_uncertain);
    if (!expired) {
        return 0;
    }
    uint64_t occurrence = (1ull << 63u) | ++state->snooze_generation;
    return enqueue_alert(state, PJ_TIME_ALERT_ALARM, PJ_TIME_ALERT_SNOOZE,
                         occurrence, 0,
                         boot_before != clock->boot_id ||
                         uncertain_before != state->recovery_time_uncertain || expired > 1);
}

static int advance_timer(pj_time_state_t *state, const pj_time_clock_t *clock)
{
    uint32_t boot_before = state->timer.anchor_boot_id;
    uint64_t expired = countdown_consume(&state->timer, clock,
                                         &state->recovery_time_uncertain);
    if (!expired) {
        return 0;
    }
    return enqueue_alert(state, PJ_TIME_ALERT_TIMER, PJ_TIME_ALERT_EXPIRED,
                         state->next_alert_id, 0,
                         boot_before != clock->boot_id || expired > 1);
}

static int advance_interval(pj_time_state_t *state, const pj_time_clock_t *clock)
{
    if (!state->interval.running) {
        return 0;
    }
    uint32_t boot_before = state->interval.anchor_boot_id;
    uint8_t uncertain_before = state->recovery_time_uncertain;
    uint64_t overrun_plus_one = countdown_consume(&state->interval, clock,
                                                  &state->recovery_time_uncertain);
    if (!overrun_plus_one) {
        return 0;
    }
    uint64_t overrun = overrun_plus_one - 1u;
    uint64_t crossed = 1;
    state->interval_phase++;
    uint64_t duration = (state->interval_phase % 2u) == 0 ?
        state->interval_work_ms : state->interval_rest_ms;
    if (overrun >= duration) {
        overrun -= duration;
        state->interval_phase++;
        crossed++;
    }
    uint64_t cycle_ms = state->interval_work_ms + state->interval_rest_ms;
    uint64_t cycles = overrun / cycle_ms;
    state->interval_phase += cycles * 2u;
    crossed += cycles * 2u;
    overrun %= cycle_ms;
    duration = (state->interval_phase % 2u) == 0 ?
        state->interval_work_ms : state->interval_rest_ms;
    if (overrun >= duration) {
        overrun -= duration;
        state->interval_phase++;
        crossed++;
        duration = (state->interval_phase % 2u) == 0 ?
            state->interval_work_ms : state->interval_rest_ms;
    }
    state->interval.remaining_ms = duration - overrun;
    state->interval.running = 1;
    countdown_anchor(&state->interval, clock);
    return enqueue_alert(state, PJ_TIME_ALERT_INTERVAL, PJ_TIME_ALERT_EXPIRED,
                         state->interval_phase,
                         crossed - 1u > UINT32_MAX ? UINT32_MAX : (uint32_t)(crossed - 1u),
                         boot_before != clock->boot_id || crossed > 1 ||
                         uncertain_before != state->recovery_time_uncertain);
}

int pj_time_advance(pj_time_state_t *state, const pj_time_clock_t *clock)
{
    if (!pj_time_state_valid(state) || !pj_time_clock_valid(clock)) {
        return 0;
    }
    pj_time_state_t before = *state;
    stopwatch_checkpoint(state, clock);
    (void)advance_alarm(state, clock);
    (void)advance_snooze(state, clock);
    (void)advance_timer(state, clock);
    (void)advance_interval(state, clock);
    return memcmp(&before, state, sizeof(*state)) != 0;
}

uint64_t pj_time_stopwatch_elapsed(const pj_time_state_t *state)
{
    return state == NULL ? 0 : state->stopwatch_elapsed_ms;
}

const pj_time_alert_t *pj_time_active_alert(const pj_time_state_t *state)
{
    return state == NULL || state->active_alert.source == PJ_TIME_ALERT_NONE ?
        NULL : &state->active_alert;
}

static uint64_t countdown_wake_delay(const pj_time_countdown_t *countdown,
                                     const pj_time_clock_t *clock)
{
    if (!countdown->running) {
        return UINT64_MAX;
    }
    uint8_t uncertain = 0;
    uint64_t elapsed = elapsed_since(countdown->anchor_boot_id,
                                     countdown->anchor_monotonic_ms,
                                     clock, &uncertain);
    if (uncertain) {
        return 0;
    }
    return elapsed >= countdown->remaining_ms ? 0 :
           countdown->remaining_ms - elapsed;
}

static uint64_t wake_fingerprint(uint8_t source, const pj_time_clock_t *clock,
                                 uint64_t delay)
{
    uint64_t target = delay;
    if (clock->wall_utc_ms >= 0 && delay <= (uint64_t)(INT64_MAX - clock->wall_utc_ms)) {
        target = (uint64_t)clock->wall_utc_ms + delay;
    }
    uint64_t hash = 1469598103934665603ull;
    hash ^= source;
    hash *= 1099511628211ull;
    for (unsigned shift = 0; shift < 64; shift += 8) {
        hash ^= (uint8_t)(target >> shift);
        hash *= 1099511628211ull;
    }
    return hash == 0 ? 1 : hash;
}

static void wake_consider(pj_time_wake_deadline_t *deadline, uint64_t delay,
                          uint8_t source, const pj_time_clock_t *clock)
{
    uint64_t fingerprint = wake_fingerprint(source, clock, delay);
    if (delay < deadline->delay_ms) {
        deadline->delay_ms = delay;
        deadline->source_mask = source;
        deadline->fingerprint = fingerprint;
    } else if (delay == deadline->delay_ms) {
        deadline->source_mask |= source;
        deadline->fingerprint ^= fingerprint;
        if (deadline->fingerprint == 0) {
            deadline->fingerprint = 1;
        }
    }
}

static int alarm_occurrence_pending(const pj_time_state_t *state,
                                    const pj_time_clock_t *clock)
{
    int64_t now = local_minute(clock);
    int64_t previous = state->alarm_last_local_minute;
    if (!state->alarm_enabled || now <= previous) {
        return 0;
    }
    int64_t minute_of_day = (int64_t)state->alarm_hour * 60 + state->alarm_minute;
    int64_t due_day = now / 1440;
    int64_t due = due_day * 1440 + minute_of_day;
    if (due > now) {
        due -= 1440;
        due_day--;
    }
    uint64_t occurrence = ((uint64_t)state->alarm_generation << 32u) |
        (uint32_t)due_day;
    return due > previous && occurrence > state->alarm_last_occurrence;
}

int pj_time_next_wake(const pj_time_state_t *state, const pj_time_clock_t *clock,
                      pj_time_wake_deadline_t *deadline)
{
    if (!pj_time_state_valid(state) || !pj_time_clock_valid(clock) || deadline == NULL) {
        return 0;
    }
    *deadline = (pj_time_wake_deadline_t) {.delay_ms = UINT64_MAX};
    if (state->active_alert.source != PJ_TIME_ALERT_NONE) {
        uint8_t source = state->active_alert.source == PJ_TIME_ALERT_TIMER ?
            PJ_TIME_WAKE_TIMER : state->active_alert.source == PJ_TIME_ALERT_INTERVAL ?
            PJ_TIME_WAKE_INTERVAL : state->active_alert.reason == PJ_TIME_ALERT_SNOOZE ?
            PJ_TIME_WAKE_SNOOZE : PJ_TIME_WAKE_ALARM;
        wake_consider(deadline, 0, source, clock);
        return 1;
    }
    wake_consider(deadline, countdown_wake_delay(&state->snooze, clock),
                  PJ_TIME_WAKE_SNOOZE, clock);
    wake_consider(deadline, countdown_wake_delay(&state->timer, clock),
                  PJ_TIME_WAKE_TIMER, clock);
    wake_consider(deadline, countdown_wake_delay(&state->interval, clock),
                  PJ_TIME_WAKE_INTERVAL, clock);
    if (state->alarm_enabled) {
        if (alarm_occurrence_pending(state, clock)) {
            wake_consider(deadline, 0, PJ_TIME_WAKE_ALARM, clock);
            return 1;
        }
        uint32_t target_second = (uint32_t)state->alarm_hour * 3600u +
                                 (uint32_t)state->alarm_minute * 60u;
        uint32_t seconds = target_second > clock->local_second ?
            target_second - clock->local_second :
            86400u - clock->local_second + target_second;
        uint64_t alarm_delay = (uint64_t)seconds * 1000u;
        if (clock->wall_utc_ms >= 0) {
            uint64_t fraction = (uint64_t)clock->wall_utc_ms % 1000u;
            alarm_delay = alarm_delay > fraction ? alarm_delay - fraction : 0;
        }
        wake_consider(deadline, alarm_delay, PJ_TIME_WAKE_ALARM, clock);
    }
    if (deadline->delay_ms == UINT64_MAX) {
        deadline->source_mask = 0;
        deadline->fingerprint = 0;
    }
    return 1;
}

uint64_t pj_time_next_wake_delay_ms(const pj_time_state_t *state,
                                    const pj_time_clock_t *clock)
{
    pj_time_wake_deadline_t deadline;
    return pj_time_next_wake(state, clock, &deadline) ? deadline.delay_ms : UINT64_MAX;
}

int pj_time_alert_dismiss(pj_time_state_t *state, uint64_t alert_id)
{
    if (!pj_time_state_valid(state) || alert_id == 0 ||
        state->active_alert.source == PJ_TIME_ALERT_NONE ||
        state->active_alert.id != alert_id) {
        return 0;
    }
    promote_alert(state);
    return 1;
}

int pj_time_alarm_snooze(pj_time_state_t *state, uint64_t alert_id, uint64_t duration_ms,
                         const pj_time_clock_t *clock)
{
    if (!pj_time_state_valid(state) || !pj_time_clock_valid(clock) || duration_ms == 0 ||
        duration_ms > PJ_TIME_MAX_DURATION_MS || state->active_alert.id != alert_id ||
        state->active_alert.source != PJ_TIME_ALERT_ALARM) {
        return 0;
    }
    promote_alert(state);
    state->snooze.running = 1;
    state->snooze.remaining_ms = duration_ms;
    countdown_anchor(&state->snooze, clock);
    return 1;
}

pj_time_conflict_action_t pj_time_alert_conflict_action(pj_time_alert_source_t source,
                                                        pj_time_activity_t activity)
{
    if (!source_valid((uint8_t)source) || activity == PJ_TIME_ACTIVITY_IDLE) {
        return PJ_TIME_PRESENT;
    }
    if (activity == PJ_TIME_ACTIVITY_RECORDING) {
        return PJ_TIME_VISUAL_DEFER_AUDIO;
    }
    return PJ_TIME_PREEMPT_PLAYBACK;
}

static uint32_t crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1u) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return ~crc;
}

static void put_u8(writer_t *w, uint8_t value) { w->data[w->offset++] = value; }
static void put_u32(writer_t *w, uint32_t value)
{
    for (int i = 0; i < 4; i++) put_u8(w, (uint8_t)(value >> (i * 8)));
}
static void put_u64(writer_t *w, uint64_t value)
{
    for (int i = 0; i < 8; i++) put_u8(w, (uint8_t)(value >> (i * 8)));
}
static void put_i64(writer_t *w, int64_t value) { put_u64(w, (uint64_t)value); }
static uint8_t get_u8(reader_t *r) { return r->data[r->offset++]; }
static uint32_t get_u32(reader_t *r)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) value |= (uint32_t)get_u8(r) << (i * 8);
    return value;
}
static uint64_t get_u64(reader_t *r)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) value |= (uint64_t)get_u8(r) << (i * 8);
    return value;
}
static int64_t get_i64(reader_t *r) { return (int64_t)get_u64(r); }

static void put_countdown(writer_t *w, const pj_time_countdown_t *value)
{
    put_u8(w, value->running);
    put_u32(w, value->anchor_boot_id);
    put_u64(w, value->anchor_monotonic_ms);
    put_i64(w, value->anchor_wall_utc_ms);
    put_u64(w, value->remaining_ms);
}

static void get_countdown(reader_t *r, pj_time_countdown_t *value)
{
    value->running = get_u8(r);
    value->anchor_boot_id = get_u32(r);
    value->anchor_monotonic_ms = get_u64(r);
    value->anchor_wall_utc_ms = get_i64(r);
    value->remaining_ms = get_u64(r);
}

static void put_alert(writer_t *w, const pj_time_alert_t *value)
{
    put_u64(w, value->id);
    put_u64(w, value->occurrence);
    put_u32(w, value->skipped_occurrences);
    put_u8(w, value->source);
    put_u8(w, value->reason);
    put_u8(w, value->recovered);
}

static void get_alert(reader_t *r, pj_time_alert_t *value)
{
    value->id = get_u64(r);
    value->occurrence = get_u64(r);
    value->skipped_occurrences = get_u32(r);
    value->source = get_u8(r);
    value->reason = get_u8(r);
    value->recovered = get_u8(r);
}

size_t pj_time_state_encode(const pj_time_state_t *state, uint8_t *record, size_t record_size)
{
    if (!pj_time_state_valid(state) || record == NULL ||
        record_size < PJ_TIME_STATE_RECORD_BYTES) {
        return 0;
    }
    memset(record, 0, PJ_TIME_STATE_RECORD_BYTES);
    writer_t w = {record, 0};
    put_u8(&w, 'P'); put_u8(&w, 'J'); put_u8(&w, 'T'); put_u8(&w, 'M');
    put_u8(&w, PJ_TIME_RECORD_VERSION);
    put_u8(&w, state->alarm_enabled);
    put_u8(&w, state->alarm_hour);
    put_u8(&w, state->alarm_minute);
    put_u32(&w, state->alarm_generation);
    put_i64(&w, state->alarm_last_local_minute);
    put_u64(&w, state->alarm_last_occurrence);
    put_countdown(&w, &state->snooze);
    put_u32(&w, state->snooze_generation);
    put_countdown(&w, &state->timer);
    put_countdown(&w, &state->interval);
    put_u64(&w, state->interval_work_ms);
    put_u64(&w, state->interval_rest_ms);
    put_u64(&w, state->interval_phase);
    put_u8(&w, state->stopwatch_running);
    put_u32(&w, state->stopwatch_anchor_boot_id);
    put_u64(&w, state->stopwatch_anchor_monotonic_ms);
    put_i64(&w, state->stopwatch_anchor_wall_utc_ms);
    put_u64(&w, state->stopwatch_elapsed_ms);
    put_u64(&w, state->next_alert_id);
    put_alert(&w, &state->active_alert);
    put_u8(&w, state->pending_count);
    put_u8(&w, state->recovery_time_uncertain);
    for (size_t i = 0; i < PJ_TIME_PENDING_ALERTS; i++) {
        put_alert(&w, &state->pending[i]);
    }
    if (w.offset > PJ_TIME_RECORD_CRC_OFFSET) {
        return 0;
    }
    uint32_t crc = crc32(record, PJ_TIME_RECORD_CRC_OFFSET);
    record[PJ_TIME_RECORD_CRC_OFFSET] = (uint8_t)crc;
    record[PJ_TIME_RECORD_CRC_OFFSET + 1] = (uint8_t)(crc >> 8u);
    record[PJ_TIME_RECORD_CRC_OFFSET + 2] = (uint8_t)(crc >> 16u);
    record[PJ_TIME_RECORD_CRC_OFFSET + 3] = (uint8_t)(crc >> 24u);
    return PJ_TIME_STATE_RECORD_BYTES;
}

int pj_time_state_decode(const uint8_t *record, size_t record_size, pj_time_state_t *state)
{
    if (record == NULL || state == NULL || record_size != PJ_TIME_STATE_RECORD_BYTES ||
        record[0] != 'P' || record[1] != 'J' || record[2] != 'T' || record[3] != 'M' ||
        record[4] != PJ_TIME_RECORD_VERSION) {
        return 0;
    }
    uint32_t expected = (uint32_t)record[PJ_TIME_RECORD_CRC_OFFSET] |
        (uint32_t)record[PJ_TIME_RECORD_CRC_OFFSET + 1] << 8u |
        (uint32_t)record[PJ_TIME_RECORD_CRC_OFFSET + 2] << 16u |
        (uint32_t)record[PJ_TIME_RECORD_CRC_OFFSET + 3] << 24u;
    if (expected != crc32(record, PJ_TIME_RECORD_CRC_OFFSET)) {
        return 0;
    }
    pj_time_state_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    reader_t r = {record, 5};
    decoded.alarm_enabled = get_u8(&r);
    decoded.alarm_hour = get_u8(&r);
    decoded.alarm_minute = get_u8(&r);
    decoded.alarm_generation = get_u32(&r);
    decoded.alarm_last_local_minute = get_i64(&r);
    decoded.alarm_last_occurrence = get_u64(&r);
    get_countdown(&r, &decoded.snooze);
    decoded.snooze_generation = get_u32(&r);
    get_countdown(&r, &decoded.timer);
    get_countdown(&r, &decoded.interval);
    decoded.interval_work_ms = get_u64(&r);
    decoded.interval_rest_ms = get_u64(&r);
    decoded.interval_phase = get_u64(&r);
    decoded.stopwatch_running = get_u8(&r);
    decoded.stopwatch_anchor_boot_id = get_u32(&r);
    decoded.stopwatch_anchor_monotonic_ms = get_u64(&r);
    decoded.stopwatch_anchor_wall_utc_ms = get_i64(&r);
    decoded.stopwatch_elapsed_ms = get_u64(&r);
    decoded.next_alert_id = get_u64(&r);
    get_alert(&r, &decoded.active_alert);
    decoded.pending_count = get_u8(&r);
    decoded.recovery_time_uncertain = get_u8(&r);
    for (size_t i = 0; i < PJ_TIME_PENDING_ALERTS; i++) {
        get_alert(&r, &decoded.pending[i]);
    }
    if (!pj_time_state_valid(&decoded)) {
        return 0;
    }
    *state = decoded;
    return 1;
}
