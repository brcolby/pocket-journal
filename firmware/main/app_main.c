#include <inttypes.h>

#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pj_board.h"
#include "pj_display_worker.h"
#include "pj_loop_schedule.h"
#include "pj_ui.h"

static const char *TAG = "pocket-journal";
static pj_ui_context_t g_ui;
static pj_framebuffer_t g_framebuffer;
static uint32_t g_sync_active_ui_session;

typedef struct {
    uint32_t display_generation;
    uint32_t scene_epoch;
    uint32_t ui_generation;
} sync_presentation_t;

static sync_presentation_t g_sync_presentation;

#define PJ_MAIN_LOOP_PERIOD_MS 50

static uint64_t monotonic_ms(void)
{
    int64_t now_us = esp_timer_get_time();
    return now_us <= 0 ? 0 : (uint64_t)now_us / 1000u;
}

static int render_and_submit_if_dirty(pj_ui_context_t *ui)
{
    pj_ui_state_t current = pj_ui_current_state(ui);
    uint32_t scene_epoch = pj_ui_interaction_generation(ui);
    if (current != PJ_UI_STATE_SYNC) {
        g_sync_active_ui_session = 0;
        g_sync_presentation = (sync_presentation_t) {0};
    }
    if (!pj_ui_is_dirty(ui)) {
        return 1;
    }
    uint32_t sync_ui_generation = current == PJ_UI_STATE_SYNC ?
        pj_ui_sync_presentation_generation(ui) : 0;
    pj_ui_render(ui, &g_framebuffer);
    pj_ui_dirty_region_t dirty = pj_ui_dirty_region(ui);
    uint32_t generation = 0;
    if (pj_display_worker_submit(&g_framebuffer, &dirty, scene_epoch,
                                 &generation)) {
        if (current == PJ_UI_STATE_SYNC && sync_ui_generation != 0) {
            g_sync_presentation = (sync_presentation_t) {
                .display_generation = generation,
                .scene_epoch = scene_epoch,
                .ui_generation = sync_ui_generation,
            };
        }
        pj_ui_mark_displayed(ui);
        return 1;
    }
    return 0;
}

static int event_requires_presented_scene(pj_board_event_type_t type)
{
    return type == PJ_BOARD_EVENT_TOUCH_TAP ||
        type == PJ_BOARD_EVENT_AUX_SHORT ||
        type == PJ_BOARD_EVENT_AUX_DOUBLE;
}

static int state_is_playback(pj_ui_state_t state)
{
    return state == PJ_UI_STATE_LISTEN || state == PJ_UI_STATE_NOTE_DETAIL;
}

static void set_recording_active(int active)
{
    (void)pj_board_record_set_active(active);
}

static void set_playback_active(int active, int note_index)
{
    (void)pj_board_playback_set_active(active, note_index);
}

static void sync_ui_audio_from_board(pj_ui_context_t *ui)
{
    pj_board_status_t status = pj_board_status();
    pj_ui_set_audio_state(ui, status.recording, status.playback_active);
    pj_ui_set_recording_elapsed(ui, status.recording_elapsed_ms);
}

static void apply_board_state_effects(pj_ui_state_t previous, pj_ui_state_t current, const pj_board_event_t *event)
{
    if (previous != PJ_UI_STATE_RECORD && current == PJ_UI_STATE_RECORD) {
        set_recording_active(1);
    } else if (previous == PJ_UI_STATE_RECORD && current != PJ_UI_STATE_RECORD) {
        set_recording_active(0);
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
    } else if (state_is_playback(current)) {
        set_playback_active(g_ui.playback_state == PJ_PLAYBACK_ACTIVE, g_ui.selected_note);
    }
    sync_ui_audio_from_board(&g_ui);
}

static int prepare_sync_inventory(pj_ui_context_t *ui)
{
    uint32_t session = 0;
    if (pj_ui_current_state(ui) != PJ_UI_STATE_SYNC ||
        !pj_ui_consume_sync_preflight_request(ui, &session)) {
        return 0;
    }

    pj_board_refresh_status(ui);
    pj_board_status_t status = pj_board_status();
    int online = status.wifi == PJ_BOARD_SERVICE_READY;
    pj_ui_sync_inventory_state_t state = PJ_UI_SYNC_INVENTORY_UNKNOWN;
    if (status.storage == PJ_BOARD_SERVICE_READY) {
        state = online ? PJ_UI_SYNC_INVENTORY_READY :
                         PJ_UI_SYNC_INVENTORY_OFFLINE;
    }
    pj_ui_set_sync_inventory(ui, session, state, ui->sync_pending,
                             ui->sync_transferred, online);
    return 1;
}

static int consume_active_sync_update(pj_ui_context_t *ui)
{
    if (pj_ui_current_state(ui) != PJ_UI_STATE_SYNC ||
        g_sync_active_ui_session == 0 ||
        g_sync_active_ui_session != pj_ui_sync_session_generation(ui)) {
        return 0;
    }
    return pj_board_consume_companion_sync_update(ui);
}

static int service_sync_presentation(pj_ui_context_t *ui)
{
    if (g_sync_presentation.display_generation == 0 ||
        pj_ui_current_state(ui) != PJ_UI_STATE_SYNC) {
        return 0;
    }
    pj_display_worker_status_t status = pj_display_worker_status();
    if (status.committed_generation !=
            g_sync_presentation.display_generation ||
        status.committed_scene_epoch != g_sync_presentation.scene_epoch) {
        return 0;
    }

    uint32_t ui_generation = g_sync_presentation.ui_generation;
    g_sync_presentation = (sync_presentation_t) {0};
    if (!pj_ui_sync_presentation_committed(ui, ui_generation)) {
        return 0;
    }

    int changed = 0;
    uint32_t session = 0;
    if (pj_ui_consume_sync_transfer_request(ui, &session)) {
        g_sync_active_ui_session = session;
        if (!pj_board_companion_sync_start()) {
            g_sync_active_ui_session = 0;
            pj_ui_set_sync_detail_for_generation(
                ui, session, "failed", 1, "START FAILED", 0);
            changed = 1;
        }
    }
    if (pj_ui_consume_sync_success_return(ui)) {
        g_sync_active_ui_session = 0;
        changed = 1;
    }
    return changed;
}

static void handle_board_event(pj_ui_context_t *ui, const pj_board_event_t *event)
{
    uint32_t scene_epoch = pj_ui_interaction_generation(ui);
    if (event_requires_presented_scene(event->type) &&
        !pj_display_worker_scene_presented(scene_epoch)) {
        pj_display_worker_note_input_deferred();
        pj_display_worker_status_t status = pj_display_worker_status();
        ESP_LOGI(TAG,
                 "UI deferred input type=%d logical_scene=%" PRIu32
                 " presented_scene=%" PRIu32 " accepted=%" PRIu32
                 " committed=%" PRIu32,
                 (int)event->type, scene_epoch,
                 status.committed_scene_epoch, status.accepted_generation,
                 status.committed_generation);
        return;
    }
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
    case PJ_BOARD_EVENT_POWER:
        if (pj_ui_current_state(ui) == PJ_UI_STATE_STATIC) {
            pj_ui_wake(ui);
        } else {
            pj_ui_sleep(ui);
        }
        handled = 1;
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
    (void)pj_board_apply_time_actions(ui);
}

void app_main(void)
{
    pj_board_profile_t profile = pj_board_default_profile();
    ESP_LOGI(TAG, "Pocket Journal booting on board profile %s", profile.name);

    pj_board_init(&profile);
    int services_ready = pj_board_start_services(&profile);

    pj_ui_init(&g_ui);
    pj_board_refresh_settings(&g_ui);
    pj_ui_wake(&g_ui);
    pj_board_refresh_status(&g_ui);
    pj_board_refresh_time_state(&g_ui);
    (void)pj_board_companion_sync_resume();
    pj_ui_request_full_refresh(&g_ui);
    int display_worker_ready = pj_display_worker_start();
    int initial_render_ready = display_worker_ready &&
        render_and_submit_if_dirty(&g_ui);

    ESP_LOGI(TAG, "Initial UI state: %s, framebuffer bytes: %u",
             pj_ui_state_name(pj_ui_current_state(&g_ui)),
             (unsigned)PJ_FRAMEBUFFER_BYTES);
    int boot_health_pending = services_ready && initial_render_ready;
    if (!boot_health_pending) {
        pj_board_confirm_boot_health(0);
    }

    pj_loop_schedule_t schedule;
    pj_loop_schedule_init(&schedule, monotonic_ms());
    int sleep_pending = 0;
    while (1) {
        pj_board_event_t event;
        if (pj_board_poll_event(&event)) {
            handle_board_event(&g_ui, &event);
            render_and_submit_if_dirty(&g_ui);
            if (pj_ui_current_state(&g_ui) == PJ_UI_STATE_STATIC) {
                sleep_pending = 1;
            } else {
                sleep_pending = 0;
            }
        }

        if (sleep_pending && pj_board_aux_released() && pj_board_power_released() &&
            pj_display_worker_is_idle()) {
            sleep_pending = 0;
            int sleep_result = pj_board_enter_sleep();
            if (sleep_result > 0) {
                pj_ui_wake(&g_ui);
                pj_board_refresh_status(&g_ui);
                (void)pj_board_update_time_state(&g_ui);
                render_and_submit_if_dirty(&g_ui);
            } else if (sleep_result == 0) {
                sleep_pending = 1;
            } else {
                pj_ui_wake(&g_ui);
                render_and_submit_if_dirty(&g_ui);
            }
        }

        pj_loop_schedule_events_t due = pj_loop_schedule_poll(
            &schedule, monotonic_ms());
        int dynamic_changed = 0;
        if (due.second_due) {
            dynamic_changed = pj_ui_tick(&g_ui);
            sync_ui_audio_from_board(&g_ui);
            dynamic_changed |= pj_board_update_time_state(&g_ui);
            dynamic_changed |= pj_board_tick_time(&g_ui);
        }
        if (due.second_due && (dynamic_changed || pj_ui_is_dirty(&g_ui))) {
            render_and_submit_if_dirty(&g_ui);
        }

        if (due.status_due) {
            pj_board_refresh_status(&g_ui);
            sync_ui_audio_from_board(&g_ui);
            render_and_submit_if_dirty(&g_ui);
            ESP_LOGI(TAG, "UI=%s display=%d storage=%d audio=%d http=%d",
                     pj_ui_state_name(pj_ui_current_state(&g_ui)),
                     pj_board_status().display,
                     pj_board_status().storage,
                     pj_board_status().audio,
                     pj_board_status().http);
            pj_display_worker_status_t display = pj_display_worker_status();
            ESP_LOGI(TAG,
                     "Display generations accepted=%" PRIu32
                     " started=%" PRIu32 " committed=%" PRIu32
                     " scene=%" PRIu32 " superseded=%" PRIu32
                     " rate_deferred=%" PRIu32 " input_deferred=%" PRIu32,
                     display.accepted_generation, display.started_generation,
                     display.committed_generation,
                     display.committed_scene_epoch, display.superseded_frames,
                     display.rate_deferred_frames,
                     display.input_deferred_events);
        }

        if (pj_board_consume_time_update(&g_ui)) {
            render_and_submit_if_dirty(&g_ui);
        }

        if (pj_board_consume_audio_update(&g_ui)) {
            render_and_submit_if_dirty(&g_ui);
        }

        if (pj_board_consume_notes_update(&g_ui)) {
            sync_ui_audio_from_board(&g_ui);
            render_and_submit_if_dirty(&g_ui);
        }

        if (pj_board_consume_settings_update(&g_ui)) {
            render_and_submit_if_dirty(&g_ui);
        }

        if (prepare_sync_inventory(&g_ui)) {
            render_and_submit_if_dirty(&g_ui);
        }

        if (consume_active_sync_update(&g_ui)) {
            render_and_submit_if_dirty(&g_ui);
        }

        if (service_sync_presentation(&g_ui)) {
            render_and_submit_if_dirty(&g_ui);
        }

        if (boot_health_pending && pj_display_worker_committed_frames() > 0) {
            pj_board_confirm_boot_health(1);
            boot_health_pending = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(PJ_MAIN_LOOP_PERIOD_MS));
    }
}
