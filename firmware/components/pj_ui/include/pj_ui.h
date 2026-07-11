#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_DISPLAY_WIDTH 200
#define PJ_DISPLAY_HEIGHT 200
#define PJ_FRAMEBUFFER_BYTES ((PJ_DISPLAY_WIDTH * PJ_DISPLAY_HEIGHT) / 8)
#define PJ_UI_MAX_NOTES 12
#define PJ_UI_NOTE_LABEL_LEN 18

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
    int battery_percent;
    int temperature_c;
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
    int interval_running;
    int interval_seconds;
    int interval_round;
    int alarm_on;
    int alarm_hour;
    int alarm_minute;
    int note_page;
    int selected_note;
    char note_labels[PJ_UI_MAX_NOTES][PJ_UI_NOTE_LABEL_LEN];
    pj_record_state_t record_state;
    pj_playback_state_t playback_state;
    int playback_exit_pending;
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
void pj_ui_set_status(pj_ui_context_t *ctx, int battery_percent, int temperature_c);
void pj_ui_set_time(pj_ui_context_t *ctx, int hour, int minute, int year, int month, int day);
void pj_ui_set_notes(pj_ui_context_t *ctx, int count, const char labels[][PJ_UI_NOTE_LABEL_LEN]);
void pj_ui_set_audio_state(pj_ui_context_t *ctx, int recording, int playback_active);
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
