#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pj_board.h"
#include "pj_ui.h"

static const char *TAG = "pocket-journal";
static pj_ui_context_t g_ui;
static pj_framebuffer_t g_framebuffer;

#define PJ_STATUS_REFRESH_TICKS 6000

static void render_and_flush_if_dirty(pj_ui_context_t *ui)
{
    if (!pj_ui_is_dirty(ui)) {
        return;
    }
    pj_ui_render(ui, &g_framebuffer);
    if (pj_board_display_framebuffer(&g_framebuffer, &ui->dirty)) {
        pj_ui_mark_displayed(ui);
    }
}

static int state_is_playback(pj_ui_state_t state)
{
    return state == PJ_UI_STATE_LISTEN || state == PJ_UI_STATE_NOTE_DETAIL;
}

static void set_recording_active(int active)
{
    if ((pj_board_status().recording != 0) != (active != 0)) {
        (void)pj_board_record_toggle();
    }
}

static void set_playback_active(int active, int note_index)
{
    if ((pj_board_status().playback_active != 0) != (active != 0)) {
        if (active) {
            (void)pj_board_playback_toggle_index(note_index);
        } else {
            (void)pj_board_playback_toggle();
        }
    }
}

static void sync_ui_audio_from_board(pj_ui_context_t *ui)
{
    pj_board_status_t status = pj_board_status();
    pj_ui_set_audio_state(ui, status.recording, status.playback_active);
}

static void apply_board_state_effects(pj_ui_state_t previous, pj_ui_state_t current, const pj_board_event_t *event)
{
    if (previous != PJ_UI_STATE_RECORD && current == PJ_UI_STATE_RECORD) {
        set_recording_active(1);
    } else if (previous == PJ_UI_STATE_RECORD && current != PJ_UI_STATE_RECORD) {
        set_recording_active(0);
        pj_board_refresh_notes(&g_ui);
    } else if (current == PJ_UI_STATE_RECORD && g_ui.record_state == PJ_RECORD_STOPPING &&
               (event->type == PJ_BOARD_EVENT_AUX_SHORT || event->type == PJ_BOARD_EVENT_AUX_LONG)) {
        set_recording_active(0);
    }

    if ((current == PJ_UI_STATE_LISTEN || current == PJ_UI_STATE_READ) &&
        current != previous) {
        pj_board_refresh_notes(&g_ui);
    }

    if (state_is_playback(previous) && !state_is_playback(current)) {
        set_playback_active(0, 0);
    } else if ((event->type == PJ_BOARD_EVENT_AUX_SHORT || event->type == PJ_BOARD_EVENT_AUX_LONG) &&
               state_is_playback(current)) {
        set_playback_active(g_ui.playback_state == PJ_PLAYBACK_ACTIVE, g_ui.selected_note);
    }
    sync_ui_audio_from_board(&g_ui);
}

static void handle_board_event(pj_ui_context_t *ui, const pj_board_event_t *event)
{
    pj_ui_state_t previous = pj_ui_current_state(ui);
    int handled = 0;
    switch (event->type) {
    case PJ_BOARD_EVENT_WAKE:
        pj_ui_wake(ui);
        handled = 1;
        break;
    case PJ_BOARD_EVENT_SLEEP:
        pj_ui_sleep(ui);
        handled = 1;
        break;
    case PJ_BOARD_EVENT_TOUCH_TAP:
        handled = pj_ui_handle_touch(ui, event->x, event->y, PJ_TOUCH_TAP);
        break;
    case PJ_BOARD_EVENT_AUX_SHORT:
        handled = pj_ui_handle_aux_short(ui);
        break;
    case PJ_BOARD_EVENT_AUX_LONG:
        handled = pj_ui_handle_aux_long(ui);
        break;
    case PJ_BOARD_EVENT_AUX_DOUBLE:
        sync_ui_audio_from_board(ui);
        handled = pj_ui_handle_aux_double(ui);
        break;
    case PJ_BOARD_EVENT_NONE:
    default:
        break;
    }
    if (!handled && event->type == PJ_BOARD_EVENT_TOUCH_TAP) {
        ESP_LOGI(TAG, "UI ignored tap state=%s x=%d y=%d",
                 pj_ui_state_name(previous), event->x, event->y);
    }
    apply_board_state_effects(previous, pj_ui_current_state(ui), event);
    (void)pj_board_store_settings_from_ui(ui);
}

void app_main(void)
{
    pj_board_profile_t profile = pj_board_default_profile();
    ESP_LOGI(TAG, "Pocket Journal booting on board profile %s", profile.name);

    pj_board_init(&profile);
    pj_board_start_services(&profile);

    pj_ui_init(&g_ui);
    pj_board_refresh_settings(&g_ui);
    pj_ui_wake(&g_ui);
    pj_board_refresh_status(&g_ui);
    pj_ui_request_full_refresh(&g_ui);
    render_and_flush_if_dirty(&g_ui);

    ESP_LOGI(TAG, "Initial UI state: %s, framebuffer bytes: %u",
             pj_ui_state_name(pj_ui_current_state(&g_ui)),
             (unsigned)PJ_FRAMEBUFFER_BYTES);

    int loop_ticks = 0;
    int second_ticks = 0;
    int clock_seconds = 0;
    while (1) {
        pj_board_event_t event;
        if (pj_board_poll_event(&event)) {
            handle_board_event(&g_ui, &event);
            render_and_flush_if_dirty(&g_ui);
            if (pj_ui_current_state(&g_ui) == PJ_UI_STATE_STATIC) {
                pj_board_enter_sleep();
            }
        }

        second_ticks++;
        if (second_ticks >= 20) {
            second_ticks = 0;
            if (pj_ui_tick(&g_ui)) {
                render_and_flush_if_dirty(&g_ui);
            }
            clock_seconds++;
            if (clock_seconds >= 60) {
                clock_seconds = 0;
                if (pj_board_tick_time(&g_ui)) {
                    render_and_flush_if_dirty(&g_ui);
                }
            }
        }

        if ((loop_ticks % PJ_STATUS_REFRESH_TICKS) == 0) {
            pj_board_refresh_status(&g_ui);
            sync_ui_audio_from_board(&g_ui);
            render_and_flush_if_dirty(&g_ui);
            ESP_LOGI(TAG, "UI=%s display=%d storage=%d audio=%d http=%d",
                     pj_ui_state_name(pj_ui_current_state(&g_ui)),
                     pj_board_status().display,
                     pj_board_status().storage,
                     pj_board_status().audio,
                     pj_board_status().http);
        }

        if (pj_board_consume_time_update(&g_ui)) {
            clock_seconds = 0;
            render_and_flush_if_dirty(&g_ui);
        }

        if (pj_board_consume_audio_update(&g_ui)) {
            render_and_flush_if_dirty(&g_ui);
        }

        if (pj_board_consume_notes_update(&g_ui)) {
            sync_ui_audio_from_board(&g_ui);
            render_and_flush_if_dirty(&g_ui);
        }

        if (pj_board_consume_settings_update(&g_ui)) {
            render_and_flush_if_dirty(&g_ui);
        }

        loop_ticks++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
