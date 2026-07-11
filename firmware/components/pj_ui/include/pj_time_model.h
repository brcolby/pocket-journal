#ifndef PJ_TIME_MODEL_H
#define PJ_TIME_MODEL_H

#include <stddef.h>
#include <stdint.h>

#define PJ_TIME_PENDING_ALERTS 8u
#define PJ_TIME_STATE_RECORD_BYTES 512u
#define PJ_TIME_MAX_DURATION_MS (30ull * 24ull * 60ull * 60ull * 1000ull)

typedef enum {
    PJ_TIME_ALERT_NONE = 0,
    PJ_TIME_ALERT_ALARM = 1,
    PJ_TIME_ALERT_TIMER = 2,
    PJ_TIME_ALERT_INTERVAL = 3,
} pj_time_alert_source_t;

typedef enum {
    PJ_TIME_ALERT_SCHEDULED = 1,
    PJ_TIME_ALERT_SNOOZE = 2,
    PJ_TIME_ALERT_EXPIRED = 3,
} pj_time_alert_reason_t;

typedef enum {
    PJ_TIME_ACTIVITY_IDLE = 0,
    PJ_TIME_ACTIVITY_PLAYBACK = 1,
    PJ_TIME_ACTIVITY_RECORDING = 2,
} pj_time_activity_t;

typedef enum {
    PJ_TIME_PRESENT = 0,
    PJ_TIME_PREEMPT_PLAYBACK = 1,
    PJ_TIME_VISUAL_DEFER_AUDIO = 2,
} pj_time_conflict_action_t;

typedef struct {
    uint32_t boot_id;
    uint64_t monotonic_ms;
    int64_t wall_utc_ms;
    int32_t local_day;
    uint32_t local_second;
    uint64_t reboot_elapsed_ms;
    uint8_t reboot_elapsed_valid;
} pj_time_clock_t;

typedef struct {
    uint8_t running;
    uint32_t anchor_boot_id;
    uint64_t anchor_monotonic_ms;
    int64_t anchor_wall_utc_ms;
    uint64_t remaining_ms;
} pj_time_countdown_t;

typedef struct {
    uint64_t id;
    uint64_t occurrence;
    uint32_t skipped_occurrences;
    uint8_t source;
    uint8_t reason;
    uint8_t recovered;
} pj_time_alert_t;

typedef struct {
    uint8_t alarm_enabled;
    uint8_t alarm_hour;
    uint8_t alarm_minute;
    uint32_t alarm_generation;
    int64_t alarm_last_local_minute;
    uint64_t alarm_last_occurrence;

    pj_time_countdown_t snooze;
    uint32_t snooze_generation;
    pj_time_countdown_t timer;

    pj_time_countdown_t interval;
    uint64_t interval_work_ms;
    uint64_t interval_rest_ms;
    uint64_t interval_phase;

    uint8_t stopwatch_running;
    uint32_t stopwatch_anchor_boot_id;
    uint64_t stopwatch_anchor_monotonic_ms;
    int64_t stopwatch_anchor_wall_utc_ms;
    uint64_t stopwatch_elapsed_ms;

    uint64_t next_alert_id;
    pj_time_alert_t active_alert;
    pj_time_alert_t pending[PJ_TIME_PENDING_ALERTS];
    uint8_t pending_count;
    uint8_t recovery_time_uncertain;
} pj_time_state_t;

int pj_time_clock_valid(const pj_time_clock_t *clock);
void pj_time_state_defaults(pj_time_state_t *state, const pj_time_clock_t *clock);
int pj_time_state_valid(const pj_time_state_t *state);

int pj_time_alarm_configure(pj_time_state_t *state, int enabled, int hour, int minute,
                            const pj_time_clock_t *clock);
int pj_time_timer_start(pj_time_state_t *state, uint64_t duration_ms,
                        const pj_time_clock_t *clock);
int pj_time_timer_pause(pj_time_state_t *state, const pj_time_clock_t *clock);
void pj_time_timer_reset(pj_time_state_t *state);
int pj_time_interval_start(pj_time_state_t *state, uint64_t work_ms, uint64_t rest_ms,
                           const pj_time_clock_t *clock);
int pj_time_interval_resume(pj_time_state_t *state, const pj_time_clock_t *clock);
int pj_time_interval_pause(pj_time_state_t *state, const pj_time_clock_t *clock);
void pj_time_interval_reset(pj_time_state_t *state);
int pj_time_stopwatch_start(pj_time_state_t *state, const pj_time_clock_t *clock);
int pj_time_stopwatch_pause(pj_time_state_t *state, const pj_time_clock_t *clock);
void pj_time_stopwatch_reset(pj_time_state_t *state);
void pj_time_recovery_acknowledge(pj_time_state_t *state);

/* Returns nonzero whenever checkpoint or alert state changed and should be persisted. */
int pj_time_advance(pj_time_state_t *state, const pj_time_clock_t *clock);
uint64_t pj_time_stopwatch_elapsed(const pj_time_state_t *state);
const pj_time_alert_t *pj_time_active_alert(const pj_time_state_t *state);
uint64_t pj_time_next_wake_delay_ms(const pj_time_state_t *state,
                                    const pj_time_clock_t *clock);
int pj_time_alert_dismiss(pj_time_state_t *state, uint64_t alert_id);
int pj_time_alarm_snooze(pj_time_state_t *state, uint64_t alert_id, uint64_t duration_ms,
                         const pj_time_clock_t *clock);
pj_time_conflict_action_t pj_time_alert_conflict_action(pj_time_alert_source_t source,
                                                        pj_time_activity_t activity);

size_t pj_time_state_encode(const pj_time_state_t *state, uint8_t *record, size_t record_size);
int pj_time_state_decode(const uint8_t *record, size_t record_size, pj_time_state_t *state);

#endif
