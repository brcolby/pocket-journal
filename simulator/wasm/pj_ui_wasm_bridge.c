#include "pj_ui.h"
#include "pj_ui_presenter.h"

#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

static pj_ui_context_t g_ctx;
static pj_ui_presenter_t g_presenter;
static pj_framebuffer_t g_fb;
static pj_ui_dirty_region_t g_dirty;
static pj_ui_frame_result_t g_frame_result;
static char g_note_labels[PJ_UI_MAX_NOTES][PJ_UI_NOTE_LABEL_LEN];
static int g_note_count;

static pj_ui_preferences_t current_preferences(void)
{
    return (pj_ui_preferences_t) {
        .volume = g_ctx.volume,
        .dark_mode = g_ctx.dark_mode,
        .alarm_enabled = g_ctx.alarm_on,
        .alarm_hour = g_ctx.alarm_hour,
        .alarm_minute = g_ctx.alarm_minute,
        .timer_seconds = g_ctx.timer_preset_seconds,
        .interval_seconds = g_ctx.interval_preset_seconds,
        .clock_24h = g_ctx.clock_24h,
        .temperature_fahrenheit = g_ctx.temperature_fahrenheit,
        .transcript_font_size = g_ctx.transcript_font_size,
    };
}

static void present_current_frame(void)
{
    pj_ui_presenter_revision_t revision = pj_ui_presenter_revision(&g_ctx);
    pj_ui_presenter_frame_t frame;
    pj_ui_frame_result_t result = pj_ui_presenter_prepare(
        &g_presenter, &g_ctx, &revision, &frame);
    if (result == PJ_UI_FRAME_IDLE) {
        memset(&g_dirty, 0, sizeof(g_dirty));
        g_frame_result = PJ_UI_FRAME_IDLE;
        return;
    }

    if (frame.framebuffer != NULL) {
        g_fb = *frame.framebuffer;
    }
    g_dirty = frame.dirty;
    g_frame_result = result;
    (void)pj_ui_presenter_accept(&g_presenter, frame.token);
}

static void sync_notes(void)
{
    pj_ui_set_notes(&g_ctx, g_note_count, (const char (*)[PJ_UI_NOTE_LABEL_LEN])g_note_labels);
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_init(void)
{
    pj_ui_init(&g_ctx);
    pj_ui_presenter_init(&g_presenter);
    memset(&g_fb, 0, sizeof(g_fb));
    memset(&g_dirty, 0, sizeof(g_dirty));
    g_frame_result = PJ_UI_FRAME_IDLE;
    memset(g_note_labels, 0, sizeof(g_note_labels));
    g_note_count = 0;
    pj_ui_compose_frame(&g_ctx, &g_fb);
    g_dirty = (pj_ui_dirty_region_t) {
        .x = 0,
        .y = 0,
        .width = PJ_DISPLAY_WIDTH,
        .height = PJ_DISPLAY_HEIGHT,
        .partial = 0,
    };
    g_frame_result = PJ_UI_FRAME_FULL;
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
    pj_ui_preferences_t preferences = current_preferences();
    preferences.clock_24h = clock_24h;
    preferences.temperature_fahrenheit = temperature_fahrenheit;
    preferences.transcript_font_size = transcript_font_size;
    pj_ui_apply_preferences(&g_ctx, &preferences);
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_set_full_preferences(int volume, int dark_mode, int clock_24h,
                                 int temperature_fahrenheit,
                                 int transcript_font_size)
{
    pj_ui_preferences_t preferences = current_preferences();
    preferences.volume = volume;
    preferences.dark_mode = dark_mode;
    preferences.clock_24h = clock_24h;
    preferences.temperature_fahrenheit = temperature_fahrenheit;
    preferences.transcript_font_size = transcript_font_size;
    pj_ui_apply_preferences(&g_ctx, &preferences);
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_set_time_runtime(int stopwatch_running, int stopwatch_seconds,
                             int timer_running, int timer_seconds,
                             int interval_running, int interval_seconds,
                             int interval_round)
{
    pj_ui_time_projection_t projection;
    memset(&projection, 0, sizeof(projection));
    projection.alarm_enabled = g_ctx.alarm_on;
    projection.alarm_hour = g_ctx.alarm_hour;
    projection.alarm_minute = g_ctx.alarm_minute;
    projection.stopwatch_running = stopwatch_running != 0;
    projection.stopwatch_elapsed_ms = stopwatch_seconds > 0 ?
        (uint64_t)stopwatch_seconds * 1000u : 0u;
    projection.timer_running = timer_running != 0;
    projection.timer_remaining_ms = timer_seconds > 0 ?
        (uint64_t)timer_seconds * 1000u : 0u;
    projection.interval_running = interval_running != 0;
    projection.interval_remaining_ms = interval_seconds > 0 ?
        (uint64_t)interval_seconds * 1000u : 0u;
    projection.interval_phase = interval_round > 0 ?
        (uint64_t)(interval_round - 1) : 0u;
    pj_ui_set_time_projection(&g_ctx, &projection);
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_set_sync_preview(int phase_id, int pending, int transferred,
                             int failed, int online)
{
    static const char *const phases[] = {
        "", "pending", "running", "succeeded", "failed", "offline",
    };
    const char *phase = phase_id >= 1 && phase_id <= 5 ?
        phases[phase_id] : "";
    pj_ui_set_sync_state(&g_ctx, pending, transferred, online);
    pj_ui_set_sync_detail(&g_ctx, phase, failed,
                          failed ? "preview failure" : "", 0);
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_set_recording_elapsed(int seconds)
{
    pj_ui_set_recording_elapsed(&g_ctx, seconds > 0 ?
                                (uint64_t)seconds * 1000u : 0u);
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
void pj_sim_seed_timestamp_notes(void)
{
    static const char *labels[] = {
        "REC 20260119 0919 1",
        "REC 20260715 2141 2",
        "REC 20261231 1909 3",
    };
    memset(g_note_labels, 0, sizeof(g_note_labels));
    g_note_count = (int)(sizeof(labels) / sizeof(labels[0]));
    for (int i = 0; i < g_note_count; i++) {
        strncpy(g_note_labels[i], labels[i], PJ_UI_NOTE_LABEL_LEN - 1);
    }
    sync_notes();
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_seed_punctuation_note(void)
{
    memset(g_note_labels, 0, sizeof(g_note_labels));
    g_note_count = 1;
    (void)strncpy(g_note_labels[0], "Wait: 09:19? Yes - #1 (ready)!",
                  PJ_UI_NOTE_LABEL_LEN - 1);
    sync_notes();
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_seed_long_note(void)
{
    memset(g_note_labels, 0, sizeof(g_note_labels));
    g_note_count = 1;
    (void)strncpy(g_note_labels[0],
                  "A long field note with punctuation: rain, cedar trees, river sounds, and a reminder to follow up at 09:19.",
                  PJ_UI_NOTE_LABEL_LEN - 1);
    sync_notes();
}

EMSCRIPTEN_KEEPALIVE
void pj_sim_render(void)
{
    present_current_frame();
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
    return g_dirty.x;
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_dirty_y(void)
{
    return g_dirty.y;
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_dirty_width(void)
{
    return g_dirty.width;
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_dirty_height(void)
{
    return g_dirty.height;
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_dirty_partial(void)
{
    return g_dirty.partial;
}

EMSCRIPTEN_KEEPALIVE
int pj_sim_frame_result(void)
{
    return (int)g_frame_result;
}
