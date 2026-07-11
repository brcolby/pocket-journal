#ifndef PJ_TIME_CONTROLLER_H
#define PJ_TIME_CONTROLLER_H

#include <stdint.h>

#include "pj_time_model.h"

#define PJ_TIME_CONTROLLER_CHECKPOINT_MS (15ull * 60ull * 1000ull)
#define PJ_TIME_CONTROLLER_RETRY_MS (60ull * 1000ull)
#define PJ_TIME_CONTROLLER_SNOOZE_MS (10ull * 60ull * 1000ull)

typedef enum {
    PJ_TIME_CONTROLLER_LOAD_NONE = 0,
    PJ_TIME_CONTROLLER_LOAD_VALID,
    PJ_TIME_CONTROLLER_LOAD_NOT_FOUND,
    PJ_TIME_CONTROLLER_LOAD_CORRUPT,
    PJ_TIME_CONTROLLER_LOAD_INCOMPATIBLE,
    PJ_TIME_CONTROLLER_LOAD_IO_ERROR,
} pj_time_controller_load_result_t;

typedef enum {
    PJ_TIME_CONTROLLER_SAVE_NONE = 0,
    PJ_TIME_CONTROLLER_SAVE_OK,
    PJ_TIME_CONTROLLER_SAVE_TRANSIENT_ERROR,
    PJ_TIME_CONTROLLER_SAVE_PERMANENT_ERROR,
} pj_time_controller_save_result_t;

typedef enum {
    PJ_TIME_CONTROLLER_WAKE_NONE = 0,
    PJ_TIME_CONTROLLER_WAKE_OK,
    PJ_TIME_CONTROLLER_WAKE_UNAVAILABLE,
    PJ_TIME_CONTROLLER_WAKE_ERROR,
} pj_time_controller_wake_result_t;

typedef enum {
    PJ_TIME_CONTROLLER_DIAGNOSTIC_NONE = 0,
    PJ_TIME_CONTROLLER_DIAGNOSTIC_LOAD_CORRUPT,
    PJ_TIME_CONTROLLER_DIAGNOSTIC_LOAD_INCOMPATIBLE,
    PJ_TIME_CONTROLLER_DIAGNOSTIC_LOAD_IO_ERROR,
    PJ_TIME_CONTROLLER_DIAGNOSTIC_SAVE_TRANSIENT,
    PJ_TIME_CONTROLLER_DIAGNOSTIC_SAVE_PERMANENT,
    PJ_TIME_CONTROLLER_DIAGNOSTIC_WAKE_ERROR,
    PJ_TIME_CONTROLLER_DIAGNOSTIC_TIME_UNCERTAIN,
    PJ_TIME_CONTROLLER_DIAGNOSTIC_CLOCK_ERROR,
    PJ_TIME_CONTROLLER_DIAGNOSTIC_SETTINGS_ERROR,
    PJ_TIME_CONTROLLER_DIAGNOSTIC_BOOT_ID_ERROR,
} pj_time_controller_diagnostic_t;

typedef enum {
    PJ_TIME_CONTROLLER_COMMAND_ALERT_DISMISS = 1,
    PJ_TIME_CONTROLLER_COMMAND_ALARM_SNOOZE,
    PJ_TIME_CONTROLLER_COMMAND_RECOVERY_ACKNOWLEDGE,
    PJ_TIME_CONTROLLER_COMMAND_STOPWATCH_START,
    PJ_TIME_CONTROLLER_COMMAND_STOPWATCH_PAUSE,
    PJ_TIME_CONTROLLER_COMMAND_STOPWATCH_RESET,
    PJ_TIME_CONTROLLER_COMMAND_TIMER_START,
    PJ_TIME_CONTROLLER_COMMAND_TIMER_PAUSE,
    PJ_TIME_CONTROLLER_COMMAND_TIMER_RESET,
    PJ_TIME_CONTROLLER_COMMAND_INTERVAL_START,
    PJ_TIME_CONTROLLER_COMMAND_INTERVAL_PAUSE,
    PJ_TIME_CONTROLLER_COMMAND_INTERVAL_RESET,
} pj_time_controller_command_type_t;

typedef struct {
    pj_time_controller_command_type_t type;
    uint64_t alert_id;
    uint64_t duration_ms;
    uint64_t secondary_duration_ms;
} pj_time_controller_command_t;

typedef struct {
    int enabled;
    int hour;
    int minute;
} pj_time_controller_alarm_settings_t;

typedef struct pj_time_controller_result {
    uint8_t ready;
    uint8_t state_changed;
    uint8_t transition;
    uint8_t command_attempted;
    uint8_t command_applied;
    uint8_t persistence_attempted;
    uint8_t dirty;
    uint8_t wake_requested;
    uint8_t wake_changed;
    uint8_t media_projected;
    pj_time_controller_load_result_t load_result;
    pj_time_controller_save_result_t save_result;
    pj_time_controller_wake_result_t wake_result;
    pj_time_conflict_action_t media_action;
    pj_time_controller_diagnostic_t diagnostic;
} pj_time_controller_result_t;

typedef struct {
    void *context;
    pj_time_controller_load_result_t (*load)(
        void *context, uint8_t record[PJ_TIME_STATE_RECORD_BYTES]);
    pj_time_controller_save_result_t (*save)(
        void *context, const uint8_t record[PJ_TIME_STATE_RECORD_BYTES]);
    uint32_t (*next_boot_id)(void *context);
    int (*clock)(void *context, uint32_t boot_id, pj_time_clock_t *clock);
    int (*alarm_settings)(void *context,
                          pj_time_controller_alarm_settings_t *settings);
    int (*wall_time_trusted)(void *context);
    pj_time_activity_t (*activity)(void *context);
    pj_time_controller_wake_result_t (*schedule_wake)(
        void *context, const pj_time_wake_deadline_t *deadline);
    void (*project_media)(void *context, const pj_time_alert_t *alert,
                          pj_time_conflict_action_t action);
    void (*publish_status)(void *context,
                           const pj_time_controller_result_t *result);
} pj_time_controller_io_t;

typedef struct {
    pj_time_controller_io_t io;
    pj_time_state_t state;
    uint32_t boot_id;
    uint64_t last_persist_ms;
    uint64_t last_retry_ms;
    uint64_t media_alert_id;
    uint64_t wake_fingerprint;
    uint8_t wake_source_mask;
    uint8_t ready;
    uint8_t dirty;
    uint8_t wake_dirty;
    uint8_t wake_armed;
    uint8_t media_valid;
    uint8_t wall_time_trusted;
    pj_time_conflict_action_t media_action;
    pj_time_controller_diagnostic_t load_diagnostic;
    pj_time_controller_diagnostic_t settings_diagnostic;
    pj_time_controller_diagnostic_t save_diagnostic;
    pj_time_controller_diagnostic_t wake_diagnostic;
} pj_time_controller_t;

int pj_time_controller_init(pj_time_controller_t *controller,
                            const pj_time_controller_io_t *io,
                            pj_time_controller_result_t *result);
int pj_time_controller_update(pj_time_controller_t *controller,
                              pj_time_controller_result_t *result);
int pj_time_controller_apply(pj_time_controller_t *controller,
                             const pj_time_controller_command_t *command,
                             pj_time_controller_result_t *result);
int pj_time_controller_time_changed(pj_time_controller_t *controller,
                                    int wall_time_trusted,
                                    pj_time_controller_result_t *result);

const pj_time_state_t *pj_time_controller_state(
    const pj_time_controller_t *controller);
int pj_time_controller_ready(const pj_time_controller_t *controller);

#endif
