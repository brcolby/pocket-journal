#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pj_home_layout.h"
#include "pj_time_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_DISPLAY_WIDTH 200
#define PJ_DISPLAY_HEIGHT 200
#define PJ_FRAMEBUFFER_BYTES ((PJ_DISPLAY_WIDTH * PJ_DISPLAY_HEIGHT) / 8)
#define PJ_UI_MAX_NOTES 12
#define PJ_UI_NOTE_LABEL_LEN 96

typedef enum {
    PJ_UI_STATE_STATIC = 0,
    PJ_UI_STATE_TIME_TEMP,
    PJ_UI_STATE_HOME,
    PJ_UI_STATE_NOTES,
    PJ_UI_STATE_RECORD,
    PJ_UI_STATE_LISTEN,
    PJ_UI_STATE_READ,
    PJ_UI_STATE_TIME,
    PJ_UI_STATE_ALARM,
    PJ_UI_STATE_STOPWATCH,
    PJ_UI_STATE_TIMER,
    PJ_UI_STATE_INTERVAL,
    PJ_UI_STATE_SETTINGS,
    PJ_UI_STATE_SYNC,
    PJ_UI_STATE_VOLUME,
    PJ_UI_STATE_DISPLAY,
    PJ_UI_STATE_CALENDAR,
    PJ_UI_STATE_NOTE_DETAIL,
    PJ_UI_STATE_COUNT
} pj_ui_state_t;

typedef enum {
    PJ_TOUCH_TAP = 0,
    PJ_TOUCH_LONG_PRESS,
    PJ_TOUCH_SWIPE_LEFT,
    PJ_TOUCH_SWIPE_RIGHT
} pj_touch_kind_t;

typedef struct {
    uint8_t pixels[PJ_FRAMEBUFFER_BYTES];
} pj_framebuffer_t;

typedef enum {
    PJ_RECORD_IDLE = 0,
    PJ_RECORD_ACTIVE,
    PJ_RECORD_STOPPING
} pj_record_state_t;

typedef enum {
    PJ_PLAYBACK_IDLE = 0,
    PJ_PLAYBACK_ACTIVE,
    PJ_PLAYBACK_STOPPING
} pj_playback_state_t;

typedef enum {
    PJ_UI_TIME_COMMAND_NONE = 0,
    PJ_UI_TIME_COMMAND_ALERT_DISMISS,
    PJ_UI_TIME_COMMAND_ALARM_SNOOZE,
    PJ_UI_TIME_COMMAND_RECOVERY_ACKNOWLEDGE,
    PJ_UI_TIME_COMMAND_STOPWATCH_START,
    PJ_UI_TIME_COMMAND_STOPWATCH_PAUSE,
    PJ_UI_TIME_COMMAND_STOPWATCH_RESET,
    PJ_UI_TIME_COMMAND_TIMER_START,
    PJ_UI_TIME_COMMAND_TIMER_PAUSE,
    PJ_UI_TIME_COMMAND_TIMER_RESET,
    PJ_UI_TIME_COMMAND_INTERVAL_START,
    PJ_UI_TIME_COMMAND_INTERVAL_PAUSE,
    PJ_UI_TIME_COMMAND_INTERVAL_RESET
} pj_ui_time_command_type_t;

typedef struct {
    pj_ui_time_command_type_t type;
    uint64_t alert_id;
    uint64_t duration_ms;
    uint64_t secondary_duration_ms;
} pj_ui_time_command_t;

typedef struct {
    int alarm_enabled;
    int alarm_hour;
    int alarm_minute;
    int stopwatch_running;
    uint64_t stopwatch_elapsed_ms;
    int timer_running;
    uint64_t timer_remaining_ms;
    int interval_running;
    uint64_t interval_remaining_ms;
    uint64_t interval_phase;
    pj_time_alert_t active_alert;
    int alert_audio_deferred;
    int recovery_time_uncertain;
} pj_ui_time_projection_t;

typedef struct {
    int x;
    int y;
    int width;
    int height;
    int partial;
} pj_ui_dirty_region_t;

typedef struct {
    pj_ui_state_t state;
    int dark_mode;
    int volume;
    int sync_pending;
    int sync_transferred;
    int sync_online;
    int battery_percent;
    int temperature_c;
    int humidity_percent;
    int clock_24h;
    int temperature_fahrenheit;
    int transcript_font_size;
    int hour;
    int minute;
    int year;
    int day;
    int month;
    int weekday;
    int stopwatch_running;
    int stopwatch_seconds;
    int timer_running;
    int timer_seconds;
    int timer_preset_seconds;
    int interval_running;
    int interval_seconds;
    int interval_preset_seconds;
    int interval_round;
    int alarm_on;
    int alarm_hour;
    int alarm_minute;
    int note_page;
    int selected_note;
    int focus_index;
    int note_detail_transcript;
    pj_home_layout_t home_layout;
    char note_labels[PJ_UI_MAX_NOTES][PJ_UI_NOTE_LABEL_LEN];
    uint8_t static_art[PJ_FRAMEBUFFER_BYTES];
    int static_art_valid;
    pj_record_state_t record_state;
    int recording_seconds;
    pj_playback_state_t playback_state;
    int playback_exit_pending;
    pj_time_alert_t active_alert;
    int alert_audio_deferred;
    int recovery_time_uncertain;
    pj_ui_time_command_t time_command;
    int note_count;
    pj_ui_dirty_region_t dirty;
} pj_ui_context_t;

void pj_ui_init(pj_ui_context_t *ctx);
pj_ui_state_t pj_ui_current_state(const pj_ui_context_t *ctx);
const char *pj_ui_state_name(pj_ui_state_t state);
pj_ui_state_t pj_ui_parent_state(pj_ui_state_t state);
int pj_ui_handle_touch(pj_ui_context_t *ctx, int x, int y, pj_touch_kind_t kind);
int pj_ui_handle_aux_short(pj_ui_context_t *ctx);
int pj_ui_handle_aux_long(pj_ui_context_t *ctx);
int pj_ui_handle_aux_double(pj_ui_context_t *ctx);
void pj_ui_wake(pj_ui_context_t *ctx);
void pj_ui_sleep(pj_ui_context_t *ctx);
void pj_ui_set_status(pj_ui_context_t *ctx, int battery_percent, int temperature_c,
                      int humidity_percent);
void pj_ui_set_preferences(pj_ui_context_t *ctx, int clock_24h,
                           int temperature_fahrenheit, int transcript_font_size);
void pj_ui_set_time(pj_ui_context_t *ctx, int hour, int minute, int year, int month, int day);
void pj_ui_set_notes(pj_ui_context_t *ctx, int count, const char labels[][PJ_UI_NOTE_LABEL_LEN]);
void pj_ui_set_audio_state(pj_ui_context_t *ctx, int recording, int playback_active);
void pj_ui_set_time_projection(pj_ui_context_t *ctx, const pj_ui_time_projection_t *projection);
int pj_ui_consume_time_command(pj_ui_context_t *ctx, pj_ui_time_command_t *command);
void pj_ui_set_sync_state(pj_ui_context_t *ctx, int pending, int transferred, int online);
void pj_ui_set_static_art(pj_ui_context_t *ctx, const uint8_t *pixels, size_t pixel_bytes);
int pj_ui_set_home_layout(pj_ui_context_t *ctx, const pj_home_layout_t *layout);
void pj_ui_restore_default_home(pj_ui_context_t *ctx);
int pj_ui_tick(pj_ui_context_t *ctx);
int pj_ui_is_dirty(const pj_ui_context_t *ctx);
void pj_ui_mark_displayed(pj_ui_context_t *ctx);
void pj_ui_request_full_refresh(pj_ui_context_t *ctx);
pj_ui_dirty_region_t pj_ui_dirty_region(const pj_ui_context_t *ctx);
const char *pj_ui_default_font_name(void);
void pj_ui_render(const pj_ui_context_t *ctx, pj_framebuffer_t *fb);
int pj_framebuffer_get(const pj_framebuffer_t *fb, int x, int y);

#ifdef __cplusplus
}
#endif
