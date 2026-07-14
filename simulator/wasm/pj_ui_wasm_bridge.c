#include "pj_ui.h"

#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

static pj_ui_context_t g_ctx;
static pj_framebuffer_t g_fb;
static char g_note_labels[PJ_UI_MAX_NOTES][PJ_UI_NOTE_LABEL_LEN];
static int g_note_count;

static void sync_notes(void)
{
    pj_ui_set_notes(&g_ctx, g_note_count, (const char (*)[PJ_UI_NOTE_LABEL_LEN])g_note_labels);
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_init(void)
{
    pj_ui_init(&g_ctx);
    memset(g_note_labels, 0, sizeof(g_note_labels));
    g_note_count = 0;
    pj_ui_render(&g_ctx, &g_fb);
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_reset(void)
{
    pj_sim_init();
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_wake(void)
{
    pj_ui_wake(&g_ctx);
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_sleep(void)
{
    pj_ui_sleep(&g_ctx);
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_aux_short(void)
{
    return pj_ui_handle_aux_short(&g_ctx);
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_aux_long(void)
{
    return pj_ui_handle_aux_long(&g_ctx);
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_aux_double(void)
{
    return pj_ui_handle_aux_double(&g_ctx);
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_touch_tap(int x, int y)
{
    return pj_ui_handle_touch(&g_ctx, x, y, PJ_TOUCH_TAP);
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_tick(void)
{
    return pj_ui_tick(&g_ctx);
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_set_status(int battery_percent, int temperature_c, int humidity_percent)
{
    pj_ui_set_status(&g_ctx, battery_percent, temperature_c, humidity_percent);
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_set_preferences(int clock_24h, int temperature_fahrenheit,
                            int transcript_font_size)
{
    pj_ui_set_preferences(&g_ctx, clock_24h, temperature_fahrenheit,
                          transcript_font_size);
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_set_time(int hour, int minute, int year, int month, int day)
{
    pj_ui_set_time(&g_ctx, hour, minute, year, month, day);
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_set_audio_state(int recording, int playback_active)
{
    pj_ui_set_audio_state(&g_ctx, recording, playback_active);
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_set_alert_detail(int source, int alert_id, int recovered)
{
    pj_ui_time_projection_t projection;
    memset(&projection, 0, sizeof(projection));
    projection.active_alert.id = source > 0 && alert_id > 0 ? (uint64_t)alert_id : 0u;
    projection.active_alert.source = source;
    projection.active_alert.recovered = recovered != 0;
    pj_ui_set_time_projection(&g_ctx, &projection);
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_set_alert(int source)
{
    pj_sim_set_alert_detail(source, 42, 0);
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_record_state(void)
{
    return (int)g_ctx.record_state;
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_playback_state(void)
{
    return (int)g_ctx.playback_state;
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_set_note_count(int count)
{
    if (count < 0) {
        count = 0;
    } else if (count > PJ_UI_MAX_NOTES) {
        count = PJ_UI_MAX_NOTES;
    }
    g_note_count = count;
    sync_notes();
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_set_note_label(int index, const char *label)
{
    if (index < 0 || index >= PJ_UI_MAX_NOTES || label == NULL) {
        return;
    }
    strncpy(g_note_labels[index], label, PJ_UI_NOTE_LABEL_LEN - 1);
    g_note_labels[index][PJ_UI_NOTE_LABEL_LEN - 1] = '\0';
    sync_notes();
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_seed_review_notes(void)
{
    static const char *labels[] = {
        "Walked by the river after the rain and remembered the cedar trees.",
        "Project reflection",
        "Morning idea",
        "Midweek review",
        "Tuesday follow-up",
        "Monday plan",
        "Sunday walk",
    };
    memset(g_note_labels, 0, sizeof(g_note_labels));
    g_note_count = (int)(sizeof(labels) / sizeof(labels[0]));
    for (int i = 0; i < g_note_count; i++) {
        strncpy(g_note_labels[i], labels[i], PJ_UI_NOTE_LABEL_LEN - 1);
    }
    sync_notes();
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_render(void)
{
    pj_ui_render(&g_ctx, &g_fb);
}

EMSCRIPTEN_KEEPALIVE
const uint8_t *pj_sim_framebuffer(void)
{
    return g_fb.pixels;
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_framebuffer_bytes(void)
{
    return PJ_FRAMEBUFFER_BYTES;
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_display_width(void)
{
    return PJ_DISPLAY_WIDTH;
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_display_height(void)
{
    return PJ_DISPLAY_HEIGHT;
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_state(void)
{
    return (int)pj_ui_current_state(&g_ctx);
}

EMSCRIPTEN_KEEPALIVE
const char *pj_sim_state_name(void)
{
    return pj_ui_state_name(pj_ui_current_state(&g_ctx));
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_dirty_x(void)
{
    return g_ctx.dirty.x;
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_dirty_y(void)
{
    return g_ctx.dirty.y;
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_dirty_width(void)
{
    return g_ctx.dirty.width;
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_dirty_height(void)
{
    return g_ctx.dirty.height;
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_dirty_partial(void)
{
    return g_ctx.dirty.partial;
}
