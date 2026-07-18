#include <inttypes.h>

#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pj_board.h"
#include "pj_display_worker.h"
#include "pj_loop_schedule.h"
#include "pj_sync_inventory_gate.h"
#include "pj_ui.h"
#include "pj_ui_presenter.h"

static const char *TAG = "pocket-journal";
static pj_ui_context_t g_ui;
static pj_ui_presenter_t g_presenter;
static pj_sync_inventory_gate_t g_sync_inventory_gate;
static uint32_t g_sync_active_ui_session;
static uint32_t g_sync_active_target_generation;

typedef struct {
    uint32_t display_generation;
    uint32_t interaction_generation;
    uint32_t ui_generation;
} sync_presentation_t;

static sync_presentation_t g_sync_presentation;
static uint32_t g_record_arm_display_generation;

typedef enum {
    PJ_SECONDS_CLOCK_NONE = 0,
    PJ_SECONDS_CLOCK_RECORD,
    PJ_SECONDS_CLOCK_STOPWATCH,
    PJ_SECONDS_CLOCK_TIMER,
    PJ_SECONDS_CLOCK_INTERVAL,
} seconds_clock_t;

typedef struct {
    seconds_clock_t clock;
    uint32_t sequence;
    uint32_t last_submitted_sequence;
    uint32_t last_fault_logged_sequence;
    uint64_t deadline_ms;
    int active;
} seconds_cadence_t;

static seconds_cadence_t g_seconds_cadence;

#define PJ_MAIN_LOOP_PERIOD_MS 50
#define PJ_SYNC_INVENTORY_RETRY_MS UINT64_C(200)

static uint64_t monotonic_ms(void)
{
    int64_t now_us = esp_timer_get_time();
    return now_us <= 0 ? 0 : (uint64_t)now_us / 1000u;
}

static const char *seconds_clock_name(seconds_clock_t clock)
{
    switch (clock) {
    case PJ_SECONDS_CLOCK_RECORD:
        return "record";
    case PJ_SECONDS_CLOCK_STOPWATCH:
        return "stopwatch";
    case PJ_SECONDS_CLOCK_TIMER:
        return "timer";
    case PJ_SECONDS_CLOCK_INTERVAL:
        return "interval";
    case PJ_SECONDS_CLOCK_NONE:
    default:
        return "none";
    }
}

static void clear_sync_bindings(void)
{
    if (pj_sync_inventory_gate_reset(&g_sync_inventory_gate)) {
        pj_board_sync_inventory_cancel();
    }
    g_sync_active_ui_session = 0;
    g_sync_active_target_generation = 0;
    g_sync_presentation = (sync_presentation_t) {0};
}

static int compose_and_submit(pj_ui_context_t *ui,
                              pj_display_cadence_class_t cadence_class,
                              uint32_t cadence_sequence,
                              uint64_t cadence_deadline_ms)
{
    pj_ui_state_t current = pj_ui_current_state(ui);
    if (current != PJ_UI_STATE_SYNC) {
        clear_sync_bindings();
    }
    if (g_seconds_cadence.active && cadence_class == PJ_DISPLAY_CADENCE_NONE) {
        return 1;
    }
    pj_ui_presenter_revision_t revision = pj_ui_presenter_revision(ui);
    pj_ui_presenter_frame_t frame;
    pj_ui_frame_result_t result = pj_ui_presenter_prepare(
        &g_presenter, ui, &revision, &frame);
    if (result == PJ_UI_FRAME_IDLE) {
        if (cadence_class == PJ_DISPLAY_CADENCE_SECONDS) {
            pj_display_worker_note_cadence_fault(cadence_sequence);
            return 0;
        }
        return 1;
    }
    if (result == PJ_UI_FRAME_NOOP &&
        cadence_class == PJ_DISPLAY_CADENCE_NONE) {
        return pj_ui_presenter_accept(&g_presenter, frame.token);
    }
    if (result == PJ_UI_FRAME_NOOP &&
        cadence_class == PJ_DISPLAY_CADENCE_SECONDS) {
        pj_display_worker_note_cadence_fault(cadence_sequence);
    }
    uint32_t sync_ui_generation = current == PJ_UI_STATE_SYNC ?
        pj_ui_sync_presentation_generation(ui) : 0;
    pj_display_worker_request_t request;
    pj_display_worker_request_init(&request, &frame.dirty);
    request.layout_epoch = frame.revision.layout_epoch;
    request.interaction_generation = frame.revision.interaction_generation;
    request.visual_revision = frame.revision.visual_revision;
    request.full_refresh_revision = frame.revision.full_refresh_revision;
    request.barrier = result == PJ_UI_FRAME_BARRIER ||
        result == PJ_UI_FRAME_NOOP;
    request.cadence_class = cadence_class;
    request.cadence_sequence = cadence_sequence;
    request.cadence_deadline_ms = cadence_deadline_ms;
    request.cleanup_state = cadence_class == PJ_DISPLAY_CADENCE_SECONDS ?
        PJ_DISPLAY_CLEANUP_DEFERRED : PJ_DISPLAY_CLEANUP_NONE;
    uint32_t generation = 0;
    if (pj_display_worker_submit_request(frame.framebuffer, &request,
                                         &generation)) {
        (void)pj_ui_presenter_accept(&g_presenter, frame.token);
        if (current == PJ_UI_STATE_SYNC && sync_ui_generation != 0) {
            g_sync_presentation = (sync_presentation_t) {
                .display_generation = generation,
                .interaction_generation = frame.revision.interaction_generation,
                .ui_generation = sync_ui_generation,
            };
        }
        if (current == PJ_UI_STATE_RECORD &&
            ui->record_state == PJ_RECORD_ARMING &&
            result == PJ_UI_FRAME_FULL) {
            g_record_arm_display_generation = generation;
        }
        return 1;
    }
    (void)pj_ui_presenter_reject(&g_presenter, frame.token);
    return 0;
}

static int render_and_submit_if_changed(pj_ui_context_t *ui)
{
    return compose_and_submit(ui, PJ_DISPLAY_CADENCE_NONE, 0, 0);
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
    /*
     * The recording model intentionally retains the completed capture length
     * until the next capture starts.  That retained value is useful to the
     * storage/audio layer, but it is not the elapsed projection for a newly
     * arming Record screen.  Project zero whenever capture is inactive so a
     * fresh 00:00 frame cannot inherit the previous note's duration.
     */
    pj_ui_set_recording_elapsed(
        ui, status.recording ? status.recording_elapsed_ms : 0);
}

static void apply_board_state_effects(pj_ui_state_t previous, pj_ui_state_t current, const pj_board_event_t *event)
{
    int record_exit = previous == PJ_UI_STATE_RECORD &&
        current != PJ_UI_STATE_RECORD;
    if (record_exit) {
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
    /* Record exit is a logical/UI transition, not a save-completion wait.
     * The recording worker publishes AUDIO when durable raw publication has
     * finished; polling its status here can only delay cadence cancellation. */
    if (!record_exit) {
        sync_ui_audio_from_board(&g_ui);
    }
}

static seconds_clock_t desired_seconds_clock(const pj_ui_context_t *ui)
{
    switch (pj_ui_current_state(ui)) {
    case PJ_UI_STATE_RECORD:
        return ui->record_state == PJ_RECORD_ACTIVE ?
            PJ_SECONDS_CLOCK_RECORD : PJ_SECONDS_CLOCK_NONE;
    case PJ_UI_STATE_STOPWATCH:
        return ui->stopwatch_running ?
            PJ_SECONDS_CLOCK_STOPWATCH : PJ_SECONDS_CLOCK_NONE;
    case PJ_UI_STATE_TIMER:
        return ui->timer_running ?
            PJ_SECONDS_CLOCK_TIMER : PJ_SECONDS_CLOCK_NONE;
    case PJ_UI_STATE_INTERVAL:
        return ui->interval_running ?
            PJ_SECONDS_CLOCK_INTERVAL : PJ_SECONDS_CLOCK_NONE;
    default:
        return PJ_SECONDS_CLOCK_NONE;
    }
}

static void end_seconds_cadence(pj_ui_context_t *ui)
{
    if (!g_seconds_cadence.active) return;
    const seconds_cadence_t ended = g_seconds_cadence;
    pj_display_worker_status_t before = pj_display_worker_status();
    pj_display_worker_cadence_end();
    g_seconds_cadence = (seconds_cadence_t) {0};
    /*
     * An intentional pause, reset, or navigation cancels any queued cadence
     * snapshot immediately.  The worker retains its exact pixel delta and
     * merges it into this ordinary handoff while an in-flight physical frame
     * is allowed to finish.  Cleanup stays deferred across a same-layout
     * pause and is satisfied by the next successful navigation/full frame.
     */
    (void)render_and_submit_if_changed(ui);
    ESP_LOGI(TAG,
             "Seconds cadence end clock=%s submitted=%" PRIu32
             " last_committed=%" PRIu32 " starts=%" PRIu32
             " commits=%" PRIu32 " late_max_ms=%" PRIu32
             " overruns=%" PRIu32 " misses=%" PRIu32
             " cleanup_deferred=%" PRIu32 " cleanup_pending=%d",
             seconds_clock_name(ended.clock), ended.last_submitted_sequence,
             before.cadence_last_committed_sequence,
             before.cadence_starts, before.cadence_commits,
             before.cadence_max_start_lateness_ms,
             before.cadence_overruns, before.cadence_misses,
             before.cleanup_deferred_frames, before.cleanup_pending);
}

static void reconcile_seconds_cadence(pj_ui_context_t *ui, uint64_t now_ms)
{
    seconds_clock_t desired = desired_seconds_clock(ui);
    if (g_seconds_cadence.active &&
        g_seconds_cadence.clock != desired) {
        end_seconds_cadence(ui);
        return;
    }
    if (desired == PJ_SECONDS_CLOCK_NONE || g_seconds_cadence.active) return;

    const uint64_t first_deadline = now_ms + PJ_DISPLAY_WORKER_CADENCE_PERIOD_MS;
    if (!pj_display_worker_cadence_start(1, first_deadline)) {
        ESP_LOGE(TAG, "Failed to reserve seconds cadence clock=%d", (int)desired);
        return;
    }
    g_seconds_cadence = (seconds_cadence_t) {
        .clock = desired,
        .sequence = 1,
        .deadline_ms = first_deadline,
        .active = 1,
    };
    ESP_LOGI(TAG,
             "Seconds cadence start clock=%s first_sequence=1"
             " first_deadline_ms=%" PRIu64,
             seconds_clock_name(desired), first_deadline);
}

static int service_record_arming(pj_ui_context_t *ui)
{
    if (g_record_arm_display_generation == 0 ||
        pj_ui_current_state(ui) != PJ_UI_STATE_RECORD ||
        ui->record_state != PJ_RECORD_ARMING) {
        if (pj_ui_current_state(ui) != PJ_UI_STATE_RECORD ||
            ui->record_state == PJ_RECORD_IDLE) {
            g_record_arm_display_generation = 0;
        }
        return 0;
    }
    pj_display_worker_status_t status = pj_display_worker_status();
    if (status.committed_generation != g_record_arm_display_generation ||
        status.committed_layout_epoch != pj_ui_layout_epoch(ui)) {
        return 0;
    }
    /* A prior capture can still be finalizing after an asynchronous exit.
     * Do not interpret pj_board_record_set_active()'s idempotent success for
     * that worker as a successful new arm.  record_task_exit clears the
     * lifecycle before it clears this status bit, so false is the safe point
     * at which a new recording can be started. */
    if (pj_board_status().recording) {
        return 0;
    }
    g_record_arm_display_generation = 0;
    set_recording_active(1);
    sync_ui_audio_from_board(ui);
    if (ui->record_state != PJ_RECORD_ACTIVE) {
        ESP_LOGE(TAG, "Record capture failed to arm after 00:00 presentation");
        return 0;
    }
    /* Capture startup is synchronous and can consume a meaningful fraction
     * of a second.  Anchor the first Record deadline only after PCM capture is
     * active so sequence 1 represents the first complete playable second. */
    reconcile_seconds_cadence(ui, monotonic_ms());
    return 1;
}

static int service_seconds_cadence(pj_ui_context_t *ui, uint64_t now_ms)
{
    if (!g_seconds_cadence.active ||
        now_ms < g_seconds_cadence.deadline_ms) {
        return 0;
    }
    (void)pj_ui_tick(ui);
    sync_ui_audio_from_board(ui);
    (void)pj_board_update_time_state(ui);
    (void)pj_board_tick_time(ui);

    /*
     * Board projection can end Record asynchronously (capture failure or
     * completion) or resolve a time command while servicing this deadline.
     * Release the old reservation and compose the resulting ordinary scene;
     * never mislabel that navigation/state transition as a cadence frame.
     */
    if (desired_seconds_clock(ui) != g_seconds_cadence.clock) {
        reconcile_seconds_cadence(ui, monotonic_ms());
        return 1;
    }

    uint32_t sequence = g_seconds_cadence.sequence;
    uint64_t deadline = g_seconds_cadence.deadline_ms;
    if (!compose_and_submit(ui, PJ_DISPLAY_CADENCE_SECONDS,
                            sequence, deadline)) {
        if (g_seconds_cadence.last_fault_logged_sequence != sequence) {
            g_seconds_cadence.last_fault_logged_sequence = sequence;
            ESP_LOGE(TAG,
                     "Seconds cadence submission delayed clock=%d"
                     " sequence=%" PRIu32 " deadline_ms=%" PRIu64
                     " now_ms=%" PRIu64,
                     (int)g_seconds_cadence.clock, sequence,
                     deadline, now_ms);
        }
        return 0;
    }
    g_seconds_cadence.last_submitted_sequence = sequence;
    g_seconds_cadence.sequence++;
    if (g_seconds_cadence.sequence == 0) g_seconds_cadence.sequence = 1;
    g_seconds_cadence.deadline_ms += PJ_DISPLAY_WORKER_CADENCE_PERIOD_MS;
    return 1;
}

static int prepare_sync_inventory(pj_ui_context_t *ui, uint64_t now_ms)
{
    if (pj_ui_current_state(ui) != PJ_UI_STATE_SYNC) {
        clear_sync_bindings();
        return 0;
    }

    uint32_t current_session = pj_ui_sync_session_generation(ui);
    if (pj_sync_inventory_gate_reconcile(
            &g_sync_inventory_gate, current_session)) {
        pj_board_sync_inventory_cancel();
    }
    if (g_sync_inventory_gate.session == 0) {
        uint32_t requested_session = 0;
        if (!pj_ui_consume_sync_preflight_request(ui, &requested_session)) {
            return 0;
        }
        if (!pj_sync_inventory_gate_bind(
                &g_sync_inventory_gate, requested_session, now_ms)) {
            return 0;
        }
    }
    if (!pj_sync_inventory_gate_poll_due(
            &g_sync_inventory_gate, current_session, now_ms)) {
        return 0;
    }

    pj_board_sync_inventory_t inventory;
    if (!pj_board_sync_inventory_snapshot(&inventory)) {
        inventory = (pj_board_sync_inventory_t) {
            .state = PJ_BOARD_SYNC_INVENTORY_ERROR,
        };
    }
    if (inventory.state == PJ_BOARD_SYNC_INVENTORY_BUSY) {
        pj_sync_inventory_gate_defer(
            &g_sync_inventory_gate, now_ms, PJ_SYNC_INVENTORY_RETRY_MS);
        return 0;
    }

    uint32_t session = pj_sync_inventory_gate_take(&g_sync_inventory_gate);
    pj_ui_sync_inventory_state_t state = PJ_UI_SYNC_INVENTORY_UNKNOWN;
    if (inventory.state == PJ_BOARD_SYNC_INVENTORY_READY) {
        state = inventory.online ? PJ_UI_SYNC_INVENTORY_READY :
                                   PJ_UI_SYNC_INVENTORY_OFFLINE;
    }
    pj_ui_set_sync_inventory(
        ui, session, state,
        inventory.state == PJ_BOARD_SYNC_INVENTORY_READY ? inventory.pending : 0,
        inventory.state == PJ_BOARD_SYNC_INVENTORY_READY ?
            inventory.transferred : 0,
        inventory.online);
    return 1;
}

static int consume_active_sync_update(pj_ui_context_t *ui)
{
    uint32_t current_session = pj_ui_sync_session_generation(ui);
    if (pj_ui_current_state(ui) != PJ_UI_STATE_SYNC ||
        g_sync_active_ui_session != current_session) {
        g_sync_active_ui_session = 0;
        g_sync_active_target_generation = 0;
        return 0;
    }
    if (g_sync_active_ui_session == 0 ||
        g_sync_active_target_generation == 0) {
        return 0;
    }

    pj_companion_sync_state_t snapshot;
    if (!pj_board_consume_companion_sync_update_snapshot(&snapshot)) {
        return 0;
    }
    g_sync_active_target_generation =
        pj_board_companion_sync_snapshot_reconcile_target(
            &snapshot, g_sync_active_target_generation);
    if (!pj_board_companion_sync_snapshot_matches_target(
            &snapshot, g_sync_active_target_generation)) {
        return 0;
    }
    int succeeded = pj_board_companion_sync_snapshot_target_succeeded(
        &snapshot, g_sync_active_target_generation);
    if (snapshot.phase == PJ_COMPANION_SYNC_SUCCEEDED && !succeeded) {
        return 0;
    }
    pj_ui_set_sync_state(ui, snapshot.pending, snapshot.transferred,
                         snapshot.online);
    pj_ui_set_sync_detail_for_generation(
        ui, g_sync_active_ui_session,
        succeeded ? "succeeded" :
            pj_companion_sync_phase_name(snapshot.phase),
        snapshot.failed, snapshot.error,
        pj_companion_sync_state_pending(&snapshot));
    return 1;
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
        status.committed_interaction_generation !=
            g_sync_presentation.interaction_generation) {
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
        pj_companion_sync_state_t started;
        if (!pj_board_companion_sync_start_snapshot(&started) ||
            started.requested_generation == 0) {
            g_sync_active_ui_session = 0;
            g_sync_active_target_generation = 0;
            pj_ui_set_sync_detail_for_generation(
                ui, session, "failed", 1, "START FAILED", 0);
            changed = 1;
        } else {
            g_sync_active_ui_session = session;
            g_sync_active_target_generation = started.requested_generation;
        }
    }
    if (pj_ui_consume_sync_success_return(ui)) {
        clear_sync_bindings();
        changed = 1;
    }
    return changed;
}

static void handle_board_event(pj_ui_context_t *ui, const pj_board_event_t *event)
{
    uint32_t interaction_generation = pj_ui_interaction_generation(ui);
    if (event_requires_presented_scene(event->type)) {
        pj_display_worker_status_t status = pj_display_worker_status();
        if (!pj_display_worker_status_accepts_interaction(
                &status, interaction_generation, event->captured_at_ms)) {
            pj_display_worker_note_input_deferred();
            ESP_LOGI(TAG,
                     "UI deferred input type=%d logical_interaction=%" PRIu32
                     " presented_interaction=%" PRIu32 " accepted=%" PRIu32
                     " committed=%" PRIu32 " captured_ms=%" PRIu64
                     " interaction_started_ms=%" PRIu64,
                     (int)event->type, interaction_generation,
                     status.committed_interaction_generation,
                     status.accepted_generation,
                     status.committed_generation, event->captured_at_ms,
                     status.committed_interaction_started_ms);
            return;
        }
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
    /* Navigation out of Record changes the desired cadence immediately and
     * must not queue behind unrelated settings/time transactions.  Time-page
     * controls are reconciled again after their authoritative command below. */
    reconcile_seconds_cadence(ui, monotonic_ms());
    (void)pj_board_store_settings_from_ui(ui);
    (void)pj_board_apply_time_actions(ui);
    reconcile_seconds_cadence(ui, monotonic_ms());
}

void app_main(void)
{
    pj_board_profile_t profile = pj_board_default_profile();
    ESP_LOGI(TAG, "Pocket Journal booting on board profile %s", profile.name);

    pj_board_init(&profile);
    int services_ready = pj_board_start_services(&profile);

    pj_ui_presenter_init(&g_presenter);
    pj_ui_init(&g_ui);
    pj_board_refresh_settings(&g_ui);
    pj_ui_wake(&g_ui);
    pj_board_refresh_status(&g_ui);
    pj_board_refresh_time_state(&g_ui);
    (void)pj_board_companion_sync_resume();
    pj_ui_request_full_presentation(&g_ui);
    int display_worker_ready = pj_display_worker_start();
    int initial_render_ready = display_worker_ready &&
        render_and_submit_if_changed(&g_ui);

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
            render_and_submit_if_changed(&g_ui);
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
                render_and_submit_if_changed(&g_ui);
            } else if (sleep_result == 0) {
                sleep_pending = 1;
            } else {
                pj_ui_wake(&g_ui);
                render_and_submit_if_changed(&g_ui);
            }
        }

        pj_loop_schedule_events_t due = pj_loop_schedule_poll(
            &schedule, monotonic_ms());
        int dynamic_changed = 0;
        if (due.second_due && !g_seconds_cadence.active) {
            dynamic_changed = pj_ui_tick(&g_ui);
            sync_ui_audio_from_board(&g_ui);
            dynamic_changed |= pj_board_update_time_state(&g_ui);
            dynamic_changed |= pj_board_tick_time(&g_ui);
        }
        if (due.second_due && !g_seconds_cadence.active && dynamic_changed) {
            render_and_submit_if_changed(&g_ui);
        }

        uint64_t now_ms = monotonic_ms();
        (void)service_record_arming(&g_ui);
        (void)service_seconds_cadence(&g_ui, now_ms);
        reconcile_seconds_cadence(&g_ui, monotonic_ms());

        if (due.status_due) {
            pj_board_refresh_status(&g_ui);
            sync_ui_audio_from_board(&g_ui);
            render_and_submit_if_changed(&g_ui);
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
                     " layout=%" PRIu32 " interaction=%" PRIu32
                     " superseded=%" PRIu32 " rate_deferred=%" PRIu32
                     " input_deferred=%" PRIu32 " cadence_starts=%" PRIu32
                     " cadence_commits=%" PRIu32 " cadence_late_max_ms=%" PRIu32
                     " cadence_overruns=%" PRIu32 " cadence_misses=%" PRIu32
                     " cadence_active=%d cleanup_deferred=%" PRIu32
                     " cleanup_pending=%d",
                     display.accepted_generation, display.started_generation,
                     display.committed_generation,
                     display.committed_layout_epoch,
                     display.committed_interaction_generation,
                     display.superseded_frames,
                     display.rate_deferred_frames,
                     display.input_deferred_events,
                     display.cadence_starts, display.cadence_commits,
                     display.cadence_max_start_lateness_ms,
                     display.cadence_overruns, display.cadence_misses,
                     display.cadence_active,
                     display.cleanup_deferred_frames,
                     display.cleanup_pending);
        }

        if (pj_board_consume_time_update(&g_ui)) {
            render_and_submit_if_changed(&g_ui);
        }

        if (pj_board_consume_audio_update(&g_ui)) {
            render_and_submit_if_changed(&g_ui);
        }

        if (pj_board_consume_notes_update(&g_ui)) {
            sync_ui_audio_from_board(&g_ui);
            render_and_submit_if_changed(&g_ui);
        }

        if (pj_board_consume_settings_update(&g_ui)) {
            render_and_submit_if_changed(&g_ui);
        }

        if (prepare_sync_inventory(&g_ui, monotonic_ms())) {
            render_and_submit_if_changed(&g_ui);
        }

        if (consume_active_sync_update(&g_ui)) {
            render_and_submit_if_changed(&g_ui);
        }

        if (service_sync_presentation(&g_ui)) {
            render_and_submit_if_changed(&g_ui);
        }

        if (boot_health_pending && pj_display_worker_committed_frames() > 0) {
            pj_board_confirm_boot_health(1);
            boot_health_pending = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(PJ_MAIN_LOOP_PERIOD_MS));
    }
}
