#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_DISPLAY_WIDTH 200
#define PJ_DISPLAY_HEIGHT 200
#define PJ_FRAMEBUFFER_BYTES ((PJ_DISPLAY_WIDTH * PJ_DISPLAY_HEIGHT) / 8)

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
    PJ_UI_STATE_TBD,
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

typedef struct {
    pj_ui_state_t state;
} pj_ui_context_t;

void pj_ui_init(pj_ui_context_t *ctx);
pj_ui_state_t pj_ui_current_state(const pj_ui_context_t *ctx);
const char *pj_ui_state_name(pj_ui_state_t state);
pj_ui_state_t pj_ui_parent_state(pj_ui_state_t state);
int pj_ui_handle_touch(pj_ui_context_t *ctx, int x, int y, pj_touch_kind_t kind);
void pj_ui_render(const pj_ui_context_t *ctx, pj_framebuffer_t *fb);
int pj_framebuffer_get(const pj_framebuffer_t *fb, int x, int y);

#ifdef __cplusplus
}
#endif

