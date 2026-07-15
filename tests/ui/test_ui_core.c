#include "pj_ui.h"
#include "pj_default_static_art.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int count_black_pixels(const pj_framebuffer_t *fb)
{
    int count = 0;
    for (int y = 0; y < PJ_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
            count += pj_framebuffer_get(fb, x, y);
        }
    }
    return count;
}

static int count_black_pixels_in_region(const pj_framebuffer_t *fb,
                                        int x, int y, int width, int height)
{
    int count = 0;
    for (int row = y; row < y + height; row++) {
        for (int column = x; column < x + width; column++) {
            count += pj_framebuffer_get(fb, column, row);
        }
    }
    return count;
}

static int count_pixel_differences_in_region(const pj_framebuffer_t *left,
                                             const pj_framebuffer_t *right,
                                             int x, int y, int width, int height)
{
    int count = 0;
    for (int row = y; row < y + height; row++) {
        for (int column = x; column < x + width; column++) {
            count += pj_framebuffer_get(left, column, row) !=
                pj_framebuffer_get(right, column, row);
        }
    }
    return count;
}

static void seed_notes(pj_ui_context_t *ui)
{
    const char labels[][PJ_UI_NOTE_LABEL_LEN] = {
        "SAT 09:41",
        "FRI 18:12",
        "THU 07:30",
    };
    pj_ui_set_notes(ui, 3, labels);
}

static void test_state_graph(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    seed_notes(&ui);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_STATIC);

    pj_ui_wake(&ui);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);

    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIME_TEMP);
    assert(pj_ui_handle_aux_long(&ui) == 0);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIME_TEMP);

    pj_ui_wake(&ui);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);

    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_handle_touch(&ui, 100, 150, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);

    assert(pj_ui_handle_touch(&ui, 20, 50, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_NOTES);

    assert(pj_ui_handle_touch(&ui, 100, 100, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_LISTEN);

    assert(pj_ui_handle_touch(&ui, 100, 95, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_NOTE_DETAIL);

    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_LISTEN);

    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_NOTES);

    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);

    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIME_TEMP);
    assert(pj_ui_handle_aux_long(&ui) == 0);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIME_TEMP);

    ui.state = PJ_UI_STATE_NOTES;
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);

    ui.state = PJ_UI_STATE_TIMER;
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIME);
}

static void test_empty_notes_do_not_open_detail(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_READ;
    assert(pj_ui_handle_touch(&ui, 100, 95, PJ_TOUCH_TAP) == 0);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_READ);
}

static void test_note_paging_tracks_touch_selection_and_swipes(void)
{
    pj_ui_context_t ui;
    const char labels[7][PJ_UI_NOTE_LABEL_LEN] = {
        "One", "Two", "Three", "Four", "Five", "Six", "Seven",
    };
    pj_ui_init(&ui);
    pj_ui_set_notes(&ui, 7, labels);
    ui.state = PJ_UI_STATE_LISTEN;

    assert(pj_ui_handle_touch(&ui, 175, 175, PJ_TOUCH_TAP) == 1);
    assert(ui.focus_index == 3 && ui.note_page == 1);
    assert(pj_ui_handle_touch(&ui, 100, 30, PJ_TOUCH_TAP) == 1);
    assert(ui.state == PJ_UI_STATE_NOTE_DETAIL);
    assert(ui.selected_note == 3);

    ui.state = PJ_UI_STATE_READ;
    ui.note_page = 0;
    ui.focus_index = 0;
    assert(pj_ui_handle_touch(&ui, 100, 100, PJ_TOUCH_SWIPE_LEFT) == 1);
    assert(ui.note_page == 1 && ui.focus_index == 3);
    assert(pj_ui_handle_touch(&ui, 100, 100, PJ_TOUCH_SWIPE_RIGHT) == 1);
    assert(ui.note_page == 0 && ui.focus_index == 0);

    ui.note_page = 1;
    ui.focus_index = 4;
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(ui.state == PJ_UI_STATE_NOTES);
    ui.focus_index = 2;
    assert(pj_ui_handle_aux_short(&ui) == 0);
    assert(ui.state == PJ_UI_STATE_NOTES);
}

static void test_note_pager_arrows_move_exactly_three_items_and_stop_at_bounds(void)
{
    pj_ui_context_t ui;
    char labels[7][PJ_UI_NOTE_LABEL_LEN] = {
        "One", "Two", "Three", "Four", "Five", "Six", "Seven",
    };
    pj_ui_init(&ui);
    pj_ui_set_notes(&ui, 7, labels);
    ui.state = PJ_UI_STATE_LISTEN;

    assert(pj_ui_handle_touch(&ui, 25, 175, PJ_TOUCH_TAP) == 0);
    assert(ui.note_page == 0 && ui.focus_index == 0);
    assert(pj_ui_handle_touch(&ui, 175, 175, PJ_TOUCH_TAP) == 1);
    assert(ui.note_page == 1 && ui.focus_index == 3);
    assert(pj_ui_handle_touch(&ui, 175, 175, PJ_TOUCH_TAP) == 1);
    assert(ui.note_page == 2 && ui.focus_index == 6);
    assert(pj_ui_handle_touch(&ui, 175, 175, PJ_TOUCH_TAP) == 0);
    assert(ui.note_page == 2 && ui.focus_index == 6);
    assert(pj_ui_handle_touch(&ui, 25, 175, PJ_TOUCH_TAP) == 1);
    assert(ui.note_page == 1 && ui.focus_index == 3);
}

static void test_note_pager_uses_chevrons_without_arrow_bars(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t fb;
    pj_ui_init(&ui);
    seed_notes(&ui);
    ui.state = PJ_UI_STATE_LISTEN;
    ui.focus_index = -1;
    pj_ui_render(&ui, &fb);

    assert(count_black_pixels_in_region(&fb, 30, 160, 40, 30) > 20);
    assert(count_black_pixels_in_region(&fb, 130, 160, 40, 30) > 20);
    assert(count_black_pixels_in_region(&fb, 20, 174, 60, 3) < 20);
    assert(count_black_pixels_in_region(&fb, 120, 174, 60, 3) < 20);
}

static void test_read_opens_transcript_detail_without_playback(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    seed_notes(&ui);
    ui.state = PJ_UI_STATE_READ;

    assert(pj_ui_handle_touch(&ui, 100, 95, PJ_TOUCH_TAP) == 1);
    assert(ui.state == PJ_UI_STATE_NOTE_DETAIL);
    assert(ui.note_detail_transcript == 1);
    assert(ui.playback_state == PJ_PLAYBACK_IDLE);
    assert(pj_ui_handle_touch(&ui, 100, 90, PJ_TOUCH_TAP) == 0);
    assert(ui.playback_state == PJ_PLAYBACK_IDLE);
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(ui.state == PJ_UI_STATE_READ);
}

static void test_note_detail_play_control_has_touch_parity(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    seed_notes(&ui);
    ui.state = PJ_UI_STATE_NOTE_DETAIL;
    ui.note_detail_transcript = 0;

    assert(pj_ui_handle_touch(&ui, 100, 90, PJ_TOUCH_TAP) == 1);
    assert(ui.playback_state == PJ_PLAYBACK_ACTIVE);
    assert(pj_ui_handle_touch(&ui, 100, 90, PJ_TOUCH_TAP) == 1);
    assert(ui.playback_state == PJ_PLAYBACK_STOPPING);
}

static void test_custom_home_slots_render_and_route_in_order(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    pj_home_layout_t layout = {0};
    strcpy(layout.title, "Daily tools");
    layout.slot_count = 4;
    layout.slots[0] = (pj_home_slot_t) {"Capture", "microphone", "record"};
    layout.slots[1] = (pj_home_slot_t) {"Read", "read_me", "read"};
    layout.slots[2] = (pj_home_slot_t) {"Timer", "timer", "timer"};
    layout.slots[3] = (pj_home_slot_t) {"Network", "wifi", "sync"};
    assert(pj_ui_set_home_layout(&ui, &layout) == 1);

    ui.state = PJ_UI_STATE_HOME;
    pj_framebuffer_t fb;
    pj_ui_render(&ui, &fb);
    assert(count_black_pixels(&fb) > 1000);

    assert(pj_ui_handle_touch(&ui, 20, 40, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_RECORD);
    ui.state = PJ_UI_STATE_HOME;
    assert(pj_ui_handle_touch(&ui, 20, 75, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_READ);
    ui.state = PJ_UI_STATE_HOME;
    assert(pj_ui_handle_touch(&ui, 20, 125, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIMER);
    ui.state = PJ_UI_STATE_HOME;
    assert(pj_ui_handle_touch(&ui, 120, 170, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_SYNC);

    pj_ui_restore_default_home(&ui);
    assert(strcmp(ui.home_layout.title, "Pocket Journal") == 0);
    assert(ui.home_layout.slot_count == 3);
}

static void test_compiled_home_fallback_routes_every_slot(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_HOME;
    assert(pj_ui_handle_touch(&ui, 20, 40, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_NOTES);
    ui.state = PJ_UI_STATE_HOME;
    assert(pj_ui_handle_touch(&ui, 20, 125, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIME);
    ui.state = PJ_UI_STATE_HOME;
    assert(pj_ui_handle_touch(&ui, 20, 170, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_SETTINGS);
}

static void test_every_supported_home_destination_routes(void)
{
    static const struct {
        const char *destination;
        pj_ui_state_t state;
    } routes[] = {
        {"notes", PJ_UI_STATE_NOTES},
        {"record", PJ_UI_STATE_RECORD},
        {"listen", PJ_UI_STATE_LISTEN},
        {"read", PJ_UI_STATE_READ},
        {"time", PJ_UI_STATE_TIME},
        {"alarm", PJ_UI_STATE_ALARM},
        {"stopwatch", PJ_UI_STATE_STOPWATCH},
        {"timer", PJ_UI_STATE_TIMER},
        {"interval", PJ_UI_STATE_INTERVAL},
        {"settings", PJ_UI_STATE_SETTINGS},
        {"sync", PJ_UI_STATE_SYNC},
        {"volume", PJ_UI_STATE_VOLUME},
        {"calendar", PJ_UI_STATE_CALENDAR},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        pj_ui_context_t ui;
        pj_ui_init(&ui);
        pj_home_layout_t layout = {0};
        strcpy(layout.title, "Route test");
        layout.slot_count = 1;
        layout.slots[0] = (pj_home_slot_t) {"Open", "notebook", ""};
        strcpy(layout.slots[0].destination, routes[i].destination);
        assert(pj_ui_set_home_layout(&ui, &layout) == 1);
        ui.state = PJ_UI_STATE_HOME;
        assert(pj_ui_handle_touch(&ui, 20, 40, PJ_TOUCH_TAP) == 1);
        assert(pj_ui_current_state(&ui) == routes[i].state);
    }
}

static void test_home_layout_setter_canonicalizes_inactive_slots(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    pj_home_layout_t layout;
    memset(&layout, 0xcc, sizeof(layout));
    memset(layout.title, 0, sizeof(layout.title));
    strcpy(layout.title, "One tool");
    layout.slot_count = 1;
    memset(&layout.slots[0], 0, sizeof(layout.slots[0]));
    layout.slots[0] = (pj_home_slot_t) {"Notes", "notebook", "notes"};
    assert(pj_ui_set_home_layout(&ui, &layout) == 1);
    const uint8_t zero_slot[sizeof(ui.home_layout.slots[1])] = {0};
    assert(memcmp(&ui.home_layout.slots[1], zero_slot, sizeof(zero_slot)) == 0);
}

static void test_aux_primary_actions_do_not_use_hidden_focus(void)
{
    pj_ui_context_t ui;
    pj_ui_time_command_t command;
    pj_ui_init(&ui);

    ui.state = PJ_UI_STATE_HOME;
    assert(pj_ui_handle_aux_short(&ui) == 0);
    assert(ui.focus_index == 0);
    assert(pj_ui_handle_aux_double(&ui) == 1);
    assert(ui.state == PJ_UI_STATE_RECORD);
    assert(ui.record_state == PJ_RECORD_ACTIVE);

    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_SETTINGS;
    assert(pj_ui_handle_aux_short(&ui) == 0);
    assert(pj_ui_handle_aux_double(&ui) == 0);
    assert(ui.state == PJ_UI_STATE_SETTINGS && ui.focus_index == 0);

    ui.state = PJ_UI_STATE_TIMER;
    ui.focus_index = 3;
    ui.timer_seconds = 90;
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_TIMER_START);
    assert(ui.focus_index == 3);
}

static void test_aux_navigation_has_no_hidden_focus(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t before;
    pj_framebuffer_t after;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_TIME;
    pj_ui_render(&ui, &before);
    pj_ui_mark_displayed(&ui);

    assert(pj_ui_handle_aux_double(&ui) == 0);
    assert(ui.focus_index == 0);
    assert(pj_ui_is_dirty(&ui) == 0);
    pj_ui_request_full_refresh(&ui);
    pj_ui_render(&ui, &after);
    assert(memcmp(&before, &after, sizeof(before)) == 0);
}

static void test_time_value_typography_fits_refresh_regions(void)
{
    const pj_ui_state_t states[] = {
        PJ_UI_STATE_STOPWATCH,
        PJ_UI_STATE_TIMER,
        PJ_UI_STATE_INTERVAL,
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        pj_ui_context_t ui;
        pj_framebuffer_t fb;
        pj_ui_init(&ui);
        ui.state = states[i];
        ui.stopwatch_seconds = 3661;
        ui.timer_seconds = 3661;
        ui.interval_seconds = 3661;
        pj_ui_render(&ui, &fb);

        int value_top = 0;
        assert(count_black_pixels_in_region(&fb, 20, value_top, 160, 100) > 0);
        assert(count_black_pixels_in_region(&fb, 0, 100, 1, 100) +
               count_black_pixels_in_region(&fb, 199, 100, 1, 100) > 0);
        assert(pj_framebuffer_get(&fb, PJ_DISPLAY_WIDTH - 1, 130) == 1);

        pj_ui_mark_displayed(&ui);
        pj_ui_time_projection_t projection = {
            .alarm_hour = ui.alarm_hour,
            .alarm_minute = ui.alarm_minute,
            .stopwatch_running = states[i] == PJ_UI_STATE_STOPWATCH,
            .stopwatch_elapsed_ms = 3662000,
            .timer_running = states[i] == PJ_UI_STATE_TIMER,
            .timer_remaining_ms = 3662000,
            .interval_running = states[i] == PJ_UI_STATE_INTERVAL,
            .interval_remaining_ms = 3662000,
        };
        pj_ui_set_time_projection(&ui, &projection);
        assert(ui.dirty.partial == 1);
        assert(ui.dirty.y <= value_top);
        assert(ui.dirty.y + ui.dirty.height >= value_top + 100);
    }
}

static void test_interval_round_is_rendered_zero_indexed(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t zero;
    pj_framebuffer_t one;
    pj_ui_init(&ui);
    assert(ui.interval_round == 0);
    ui.state = PJ_UI_STATE_INTERVAL;
    pj_ui_render(&ui, &zero);

    ui.interval_round = 1;
    pj_ui_render(&ui, &one);
    assert(memcmp(&zero, &one, sizeof(zero)) != 0);
    assert(count_black_pixels_in_region(&zero, 0, 0, 200, 60) >
           count_black_pixels_in_region(&one, 0, 0, 200, 60));
}

static void test_aux_double_is_direct_quick_record_only(void)
{
    pj_ui_context_t ui;
    const pj_ui_state_t ignored_states[] = {
        PJ_UI_STATE_STATIC, PJ_UI_STATE_NOTES, PJ_UI_STATE_TIME,
        PJ_UI_STATE_SETTINGS, PJ_UI_STATE_VOLUME, PJ_UI_STATE_ALARM,
        PJ_UI_STATE_STOPWATCH, PJ_UI_STATE_TIMER, PJ_UI_STATE_INTERVAL,
    };
    for (size_t i = 0; i < sizeof(ignored_states) / sizeof(ignored_states[0]); i++) {
        pj_ui_init(&ui);
        ui.state = ignored_states[i];
        assert(pj_ui_handle_aux_double(&ui) == 0);
        assert(ui.state == ignored_states[i]);
        assert(ui.focus_index == 0);
    }

    const pj_ui_state_t quick_record_states[] = {
        PJ_UI_STATE_TIME_TEMP, PJ_UI_STATE_HOME,
    };
    for (size_t i = 0; i < sizeof(quick_record_states) / sizeof(quick_record_states[0]); i++) {
        pj_ui_init(&ui);
        ui.state = quick_record_states[i];
        assert(pj_ui_handle_aux_double(&ui) == 1);
        assert(ui.state == PJ_UI_STATE_RECORD);
        assert(ui.record_state == PJ_RECORD_ACTIVE);
    }

    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_HOME;
    ui.record_state = PJ_RECORD_STOPPING;
    assert(pj_ui_handle_aux_double(&ui) == 0);
    ui.record_state = PJ_RECORD_IDLE;
    ui.playback_state = PJ_PLAYBACK_ACTIVE;
    assert(pj_ui_handle_aux_double(&ui) == 0);
}

static void test_audio_lifecycle_reconciliation(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);

    ui.state = PJ_UI_STATE_RECORD;
    ui.record_state = PJ_RECORD_ACTIVE;
    pj_ui_set_audio_state(&ui, 0, 0);
    assert(ui.record_state == PJ_RECORD_IDLE);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);

    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_NOTE_DETAIL;
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(ui.playback_state == PJ_PLAYBACK_ACTIVE);
    pj_ui_set_audio_state(&ui, 0, 1);
    assert(ui.playback_state == PJ_PLAYBACK_ACTIVE);
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(ui.playback_state == PJ_PLAYBACK_STOPPING);
    pj_ui_set_audio_state(&ui, 0, 1);
    assert(ui.playback_state == PJ_PLAYBACK_STOPPING);
    pj_ui_set_audio_state(&ui, 0, 0);
    assert(ui.playback_state == PJ_PLAYBACK_IDLE);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_NOTE_DETAIL);

    pj_ui_set_audio_state(&ui, 0, 1);
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(ui.playback_state == PJ_PLAYBACK_STOPPING);
    assert(ui.playback_exit_pending == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_NOTE_DETAIL);
    pj_ui_set_audio_state(&ui, 0, 0);
    assert(ui.playback_state == PJ_PLAYBACK_IDLE);
    assert(ui.playback_exit_pending == 0);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_LISTEN);

    ui.state = PJ_UI_STATE_LISTEN;
    ui.playback_state = PJ_PLAYBACK_ACTIVE;
    pj_ui_set_audio_state(&ui, 0, 0);
    assert(ui.playback_state == PJ_PLAYBACK_IDLE);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_LISTEN);

    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_RECORD;
    ui.record_state = PJ_RECORD_ACTIVE;
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);
    assert(ui.record_state == PJ_RECORD_STOPPING);
    pj_ui_set_audio_state(&ui, 1, 0);
    assert(ui.record_state == PJ_RECORD_STOPPING);
    assert(pj_ui_handle_aux_short(&ui) == 0);
    assert(pj_ui_handle_aux_double(&ui) == 0);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);
    assert(ui.record_state == PJ_RECORD_STOPPING);
    pj_ui_set_audio_state(&ui, 0, 0);
    assert(ui.record_state == PJ_RECORD_IDLE);

    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_HOME;
    ui.record_state = PJ_RECORD_STOPPING;
    assert(pj_ui_handle_aux_double(&ui) == 0);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);
    assert(pj_ui_handle_touch(&ui, 20, 50, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_NOTES);
    assert(pj_ui_handle_touch(&ui, 100, 30, PJ_TOUCH_TAP) == 0);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_NOTES);
}

static void test_settings_quadrants_control_volume_appearance_clock_and_sync(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_SETTINGS;

    assert(pj_ui_handle_touch(&ui, 50, 50, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_VOLUME);
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_SETTINGS);

    assert(ui.dark_mode == 0);
    assert(pj_ui_handle_touch(&ui, 150, 50, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_SETTINGS);
    assert(ui.dark_mode == 1);

    assert(ui.clock_24h == 1);
    assert(pj_ui_handle_touch(&ui, 50, 150, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_SETTINGS);
    assert(ui.clock_24h == 0);
    assert(ui.dirty.partial == 0);

    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_SETTINGS;
    assert(ui.focus_index == 0);
    assert(pj_ui_handle_aux_double(&ui) == 0);
    assert(pj_ui_handle_aux_short(&ui) == 0);
    assert(ui.focus_index == 0 && ui.state == PJ_UI_STATE_SETTINGS);

    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_SETTINGS;
    assert(pj_ui_handle_touch(&ui, 99, 99, PJ_TOUCH_TAP) == 1);
    assert(ui.state == PJ_UI_STATE_VOLUME);
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_handle_touch(&ui, 100, 99, PJ_TOUCH_TAP) == 1);
    assert(ui.state == PJ_UI_STATE_SETTINGS && ui.dark_mode == 1);
    assert(pj_ui_handle_touch(&ui, 99, 100, PJ_TOUCH_TAP) == 1);
    assert(ui.clock_24h == 0);
    assert(pj_ui_handle_touch(&ui, 100, 100, PJ_TOUCH_TAP) == 1);
    assert(ui.state == PJ_UI_STATE_SYNC);
}

static void test_alarm_touch_regions_match_enable_and_adjustment_controls(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_ALARM;

    assert(ui.alarm_on == 0);
    assert(pj_ui_handle_touch(&ui, 100, 105, PJ_TOUCH_TAP) == 1);
    assert(ui.alarm_on == 1 && ui.focus_index == 0);

    assert(pj_ui_handle_touch(&ui, 50, 140, PJ_TOUCH_TAP) == 1);
    assert(ui.alarm_hour == 6 && ui.focus_index == 1);
    assert(pj_ui_handle_touch(&ui, 150, 140, PJ_TOUCH_TAP) == 1);
    assert(ui.alarm_hour == 7 && ui.focus_index == 2);
    assert(pj_ui_handle_touch(&ui, 50, 180, PJ_TOUCH_TAP) == 1);
    assert(ui.alarm_minute == 15 && ui.focus_index == 3);
    assert(pj_ui_handle_touch(&ui, 150, 180, PJ_TOUCH_TAP) == 1);
    assert(ui.alarm_minute == 30 && ui.focus_index == 4);
}

static void test_api_preferences_remain_available_after_settings_flattening(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    pj_ui_mark_displayed(&ui);

    pj_ui_set_preferences(&ui, 0, 1, 2);
    assert(ui.clock_24h == 0);
    assert(ui.temperature_fahrenheit == 1);
    assert(ui.transcript_font_size == 2);
    assert(ui.dirty.partial == 0);
}

static void test_settings_volume_opens_dedicated_screen(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_SETTINGS;
    ui.volume = 4;

    assert(pj_ui_handle_touch(&ui, 50, 50, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_VOLUME);
    assert(ui.volume == 4);
}

static void test_volume_only_bottom_controls_adjust_and_clamp(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_VOLUME;
    ui.volume = 5;

    assert(pj_ui_handle_touch(&ui, 100, 24, PJ_TOUCH_TAP) == 0);
    assert(pj_ui_handle_touch(&ui, 100, 76, PJ_TOUCH_TAP) == 0);
    assert(pj_ui_handle_touch(&ui, 100, 99, PJ_TOUCH_TAP) == 0);
    assert(ui.volume == 5);

    assert(pj_ui_handle_touch(&ui, 52, 166, PJ_TOUCH_TAP) == 1);
    assert(ui.volume == 4);
    assert(pj_ui_handle_touch(&ui, 148, 166, PJ_TOUCH_TAP) == 1);
    assert(ui.volume == 5);

    ui.volume = 0;
    assert(pj_ui_handle_touch(&ui, 52, 166, PJ_TOUCH_TAP) == 1);
    assert(ui.volume == 0);
    ui.volume = 10;
    assert(pj_ui_handle_touch(&ui, 148, 166, PJ_TOUCH_TAP) == 1);
    assert(ui.volume == 10);
}

static void test_volume_fill_uses_full_top_half_and_controls_have_thick_borders(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t volume;
    pj_framebuffer_t timer;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_VOLUME;
    ui.volume = 5;
    ui.focus_index = -1;
    pj_ui_render(&ui, &volume);

    assert(count_black_pixels_in_region(&volume, 25, 0, 1, 100) >= 98);
    assert(count_black_pixels_in_region(&volume, 150, 3, 1, 94) == 0);

    ui.state = PJ_UI_STATE_TIMER;
    ui.focus_index = -1;
    pj_ui_render(&ui, &timer);
    assert(count_black_pixels_in_region(&timer, 99, 153, 3, 3) == 9);
}

static void test_timer_presets_are_not_runtime_counters(void)
{
    pj_ui_context_t ui;
    pj_ui_time_command_t command;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_TIMER;

    assert(pj_ui_handle_touch(&ui, 150, 177, PJ_TOUCH_TAP) == 1);
    assert(ui.timer_seconds == 330);
    assert(ui.timer_preset_seconds == 330);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    ui.timer_running = 1;
    assert(pj_ui_tick(&ui) == 0);
    assert(ui.timer_seconds == 330);
    pj_ui_time_projection_t projection = {
        .timer_running = 1,
        .timer_remaining_ms = 329000,
    };
    pj_ui_set_time_projection(&ui, &projection);
    assert(ui.timer_seconds == 329);
    assert(ui.timer_preset_seconds == 330);

    ui.timer_running = 0;
    ui.timer_seconds = 0;
    ui.focus_index = 0;
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(ui.timer_running == 0);
    assert(ui.timer_seconds == 0);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_TIMER_START);
    assert(command.duration_ms == 330000);

    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_INTERVAL;
    assert(pj_ui_handle_touch(&ui, 150, 177, PJ_TOUCH_TAP) == 1);
    assert(ui.interval_seconds == 150);
    assert(ui.interval_preset_seconds == 150);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
}

static void test_interval_repeats_one_stable_duration_across_rounds(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_INTERVAL;
    ui.interval_running = 1;
    ui.interval_seconds = 2;
    ui.interval_preset_seconds = 2;
    pj_ui_mark_displayed(&ui);

    for (int round = 0; round < 4; round++) {
        assert(ui.interval_round == round);
        assert(ui.interval_seconds == 2);
        pj_ui_time_projection_t projection = {
            .interval_running = 1,
            .interval_remaining_ms = 1000,
            .interval_phase = (uint64_t)round,
        };
        pj_ui_set_time_projection(&ui, &projection);
        assert(ui.interval_seconds == 1 && ui.interval_round == round);
        assert(ui.dirty.partial == 1);
        pj_ui_mark_displayed(&ui);
        projection.interval_remaining_ms = 2000;
        projection.interval_phase = (uint64_t)round + 1u;
        pj_ui_set_time_projection(&ui, &projection);
        assert(ui.interval_seconds == 2 && ui.interval_round == round + 1);
        assert(ui.state == PJ_UI_STATE_INTERVAL);
        assert(ui.dirty.partial == 1);
        pj_ui_mark_displayed(&ui);
    }
}

static void test_timer_geometry_and_adjustment_focus_timeout(void)
{
    pj_ui_context_t ui;
    pj_ui_time_command_t command;
    pj_framebuffer_t fb;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_TIMER;
    ui.timer_seconds = 123;
    ui.timer_preset_seconds = 300;
    ui.timer_running = 1;
    ui.focus_index = -1;
    pj_ui_render(&ui, &fb);

    assert(count_black_pixels_in_region(&fb, 0, 149, PJ_DISPLAY_WIDTH, 2) > 380);
    assert(pj_ui_handle_touch(&ui, 150, 126, PJ_TOUCH_TAP) == 1);
    assert(ui.focus_index == 1 && ui.timer_running == 1 && ui.timer_seconds == 123);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_TIMER_RESET);

    pj_ui_time_projection_t projection = {
        .timer_remaining_ms = 300000,
    };
    pj_ui_set_time_projection(&ui, &projection);
    assert(ui.timer_running == 0 && ui.timer_seconds == 300);

    assert(pj_ui_handle_touch(&ui, 50, 170, PJ_TOUCH_TAP) == 1);
    assert(ui.focus_index == 2 && ui.timer_seconds == 270);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(pj_ui_handle_touch(&ui, 150, 170, PJ_TOUCH_TAP) == 1);
    assert(ui.focus_index == 3 && ui.timer_seconds == 300);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);

    pj_ui_mark_displayed(&ui);
    for (int tick = 1; tick < PJ_UI_FOCUS_TIMEOUT_TICKS; tick++) {
        assert(pj_ui_tick(&ui) == 0);
        assert(ui.focus_index == 3);
    }
    assert(pj_ui_tick(&ui) == 0);
    assert(ui.focus_index == 0);
    assert(ui.timer_running == 0);
    assert(pj_ui_is_dirty(&ui) == 0);
}

static pj_ui_time_projection_t time_projection_with_alert(uint64_t id,
                                                          pj_time_alert_source_t source)
{
    pj_ui_time_projection_t projection = {0};
    projection.alarm_enabled = 1;
    projection.alarm_hour = 6;
    projection.alarm_minute = 45;
    projection.stopwatch_running = 1;
    projection.stopwatch_elapsed_ms = 1250;
    projection.timer_running = 1;
    projection.timer_remaining_ms = 2501;
    projection.interval_running = 1;
    projection.interval_remaining_ms = 60000;
    projection.interval_phase = 3;
    projection.active_alert = (pj_time_alert_t) {
        .id = id,
        .occurrence = 7,
        .source = (uint8_t)source,
        .reason = source == PJ_TIME_ALERT_ALARM ? PJ_TIME_ALERT_SCHEDULED : PJ_TIME_ALERT_EXPIRED,
    };
    return projection;
}

static void test_time_projection_updates_values_without_alert_subscreen(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_TIMER;
    pj_ui_mark_displayed(&ui);

    pj_ui_time_projection_t projection = time_projection_with_alert(41, PJ_TIME_ALERT_TIMER);
    pj_ui_set_time_projection(&ui, &projection);
    assert(ui.alarm_on == 1 && ui.alarm_hour == 6 && ui.alarm_minute == 45);
    assert(ui.stopwatch_running == 1 && ui.stopwatch_seconds == 1);
    assert(ui.timer_running == 1 && ui.timer_seconds == 3);
    assert(ui.interval_running == 1 && ui.interval_seconds == 60 && ui.interval_round == 3);
    assert(ui.active_alert.id == 41);
    assert(pj_ui_is_dirty(&ui) == 1 && ui.dirty.partial == 1);

    pj_ui_mark_displayed(&ui);
    pj_ui_set_time_projection(&ui, &projection);
    assert(pj_ui_is_dirty(&ui) == 0);

    projection.active_alert.skipped_occurrences = 2;
    pj_ui_set_time_projection(&ui, &projection);
    assert(pj_ui_is_dirty(&ui) == 0);

    projection.alert_audio_deferred = 1;
    pj_ui_set_time_projection(&ui, &projection);
    assert(ui.alert_audio_deferred == 1);
    assert(pj_ui_is_dirty(&ui) == 0);
}

static void test_interval_alert_stays_inline_and_aux_long_resets(void)
{
    pj_ui_context_t with_alert;
    pj_ui_context_t without_alert;
    pj_framebuffer_t with_alert_fb;
    pj_framebuffer_t without_alert_fb;
    pj_ui_time_command_t command;
    pj_ui_init(&with_alert);
    pj_ui_init(&without_alert);
    with_alert.state = PJ_UI_STATE_INTERVAL;
    without_alert.state = PJ_UI_STATE_INTERVAL;
    pj_ui_mark_displayed(&with_alert);
    pj_ui_mark_displayed(&without_alert);

    pj_ui_time_projection_t projection =
        time_projection_with_alert(71, PJ_TIME_ALERT_INTERVAL);
    projection.active_alert.recovered = 1;
    pj_ui_time_projection_t quiet_projection = projection;
    memset(&quiet_projection.active_alert, 0, sizeof(quiet_projection.active_alert));
    pj_ui_set_time_projection(&with_alert, &projection);
    pj_ui_set_time_projection(&without_alert, &quiet_projection);
    assert(with_alert.state == PJ_UI_STATE_INTERVAL);
    assert(with_alert.active_alert.id == 71);
    assert(with_alert.interval_running == 1 && with_alert.interval_round == 3);
    assert(with_alert.dirty.partial == 1);
    assert(with_alert.dirty.width * with_alert.dirty.height <
           PJ_DISPLAY_WIDTH * PJ_DISPLAY_HEIGHT);

    memset(&with_alert_fb, 0, sizeof(with_alert_fb));
    memset(&without_alert_fb, 0, sizeof(without_alert_fb));
    pj_ui_render(&with_alert, &with_alert_fb);
    pj_ui_render(&without_alert, &without_alert_fb);
    assert(memcmp(&with_alert_fb, &without_alert_fb, sizeof(with_alert_fb)) == 0);

    pj_ui_mark_displayed(&with_alert);
    projection.active_alert.id = 72;
    projection.active_alert.occurrence = 8;
    projection.interval_phase = 4;
    projection.interval_remaining_ms = 45000;
    pj_ui_set_time_projection(&with_alert, &projection);
    assert(with_alert.active_alert.id == 72 && with_alert.interval_round == 4);
    assert(with_alert.interval_seconds == 45);
    assert(with_alert.dirty.partial == 1);

    assert(pj_ui_handle_aux_long(&with_alert) == 1);
    assert(with_alert.state == PJ_UI_STATE_TIME);
    assert(pj_ui_consume_time_command(&with_alert, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_INTERVAL_RESET);
}

static void test_recovered_interval_alert_is_nonmodal_on_every_screen(void)
{
    const pj_ui_state_t states[] = {
        PJ_UI_STATE_STATIC,
        PJ_UI_STATE_TIME_TEMP,
        PJ_UI_STATE_HOME,
        PJ_UI_STATE_SETTINGS,
        PJ_UI_STATE_TIMER,
    };

    for (size_t index = 0; index < sizeof(states) / sizeof(states[0]); ++index) {
        pj_ui_context_t with_alert;
        pj_ui_context_t without_alert;
        pj_framebuffer_t with_alert_fb;
        pj_framebuffer_t without_alert_fb;
        pj_ui_init(&with_alert);
        pj_ui_init(&without_alert);
        with_alert.state = states[index];
        without_alert.state = states[index];

        pj_ui_time_projection_t projection =
            time_projection_with_alert(81, PJ_TIME_ALERT_INTERVAL);
        projection.active_alert.recovered = 1;
        pj_ui_time_projection_t quiet_projection = projection;
        memset(&quiet_projection.active_alert, 0, sizeof(quiet_projection.active_alert));
        pj_ui_set_time_projection(&with_alert, &projection);
        pj_ui_set_time_projection(&without_alert, &quiet_projection);
        pj_ui_mark_displayed(&with_alert);
        pj_ui_mark_displayed(&without_alert);

        pj_ui_render(&with_alert, &with_alert_fb);
        pj_ui_render(&without_alert, &without_alert_fb);
        assert(memcmp(&with_alert_fb, &without_alert_fb,
                      sizeof(with_alert_fb)) == 0);

        projection.active_alert.id = 82;
        projection.active_alert.occurrence++;
        pj_ui_set_time_projection(&with_alert, &projection);
        assert(with_alert.active_alert.id == 82);
        assert(pj_ui_is_dirty(&with_alert) == 0);

        int alert_handled = pj_ui_handle_aux_short(&with_alert);
        int quiet_handled = pj_ui_handle_aux_short(&without_alert);
        assert(alert_handled == quiet_handled);
        assert(with_alert.state == without_alert.state);
        assert(with_alert.focus_index == without_alert.focus_index);
    }

    pj_ui_context_t sleeping;
    pj_ui_init(&sleeping);
    sleeping.state = PJ_UI_STATE_HOME;
    pj_ui_time_projection_t recovered =
        time_projection_with_alert(83, PJ_TIME_ALERT_INTERVAL);
    recovered.active_alert.recovered = 1;
    pj_ui_set_time_projection(&sleeping, &recovered);
    pj_ui_sleep(&sleeping);
    assert(sleeping.state == PJ_UI_STATE_STATIC);
    assert(sleeping.interval_running == 1);
}

static pj_ui_time_projection_t projection_from_ui(const pj_ui_context_t *ui)
{
    return (pj_ui_time_projection_t) {
        .alarm_enabled = ui->alarm_on,
        .alarm_hour = ui->alarm_hour,
        .alarm_minute = ui->alarm_minute,
        .stopwatch_running = ui->stopwatch_running,
        .stopwatch_elapsed_ms = (uint64_t)ui->stopwatch_seconds * 1000u,
        .timer_running = ui->timer_running,
        .timer_remaining_ms = (uint64_t)ui->timer_seconds * 1000u,
        .interval_running = ui->interval_running,
        .interval_remaining_ms = (uint64_t)ui->interval_seconds * 1000u,
        .interval_phase = (uint32_t)ui->interval_round,
    };
}

static void test_time_command_buttons_wait_for_authoritative_projection(void)
{
    const struct {
        pj_ui_state_t state;
        pj_ui_time_command_type_t command;
    } cases[] = {
        {PJ_UI_STATE_STOPWATCH, PJ_UI_TIME_COMMAND_STOPWATCH_RESET},
        {PJ_UI_STATE_TIMER, PJ_UI_TIME_COMMAND_TIMER_RESET},
        {PJ_UI_STATE_INTERVAL, PJ_UI_TIME_COMMAND_INTERVAL_RESET},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        pj_ui_context_t ui;
        pj_framebuffer_t before;
        pj_framebuffer_t after_command;
        pj_ui_time_command_t command;
        pj_ui_init(&ui);
        ui.state = cases[i].state;
        ui.focus_index = 1;
        ui.stopwatch_running = 1;
        ui.stopwatch_seconds = 42;
        ui.timer_running = 1;
        ui.timer_seconds = 17;
        ui.timer_preset_seconds = 90;
        ui.interval_running = 1;
        ui.interval_seconds = 11;
        ui.interval_preset_seconds = 120;
        ui.interval_round = 4;
        pj_ui_render(&ui, &before);
        pj_ui_mark_displayed(&ui);

        assert(pj_ui_handle_touch(&ui, 150, 110, PJ_TOUCH_TAP) == 1);
        assert(pj_ui_consume_time_command(&ui, &command) == 1);
        assert(command.type == cases[i].command);
        assert(pj_ui_is_dirty(&ui) == 0);
        assert(ui.stopwatch_running == 1 && ui.stopwatch_seconds == 42);
        assert(ui.timer_running == 1 && ui.timer_seconds == 17);
        assert(ui.interval_running == 1 && ui.interval_seconds == 11 &&
               ui.interval_round == 4);

        pj_ui_context_t full = ui;
        pj_ui_request_full_refresh(&full);
        pj_ui_render(&full, &after_command);
        assert(memcmp(&before, &after_command, sizeof(before)) == 0);

        pj_ui_time_projection_t projection = projection_from_ui(&ui);
        if (cases[i].state == PJ_UI_STATE_STOPWATCH) {
            projection.stopwatch_running = 0;
            projection.stopwatch_elapsed_ms = 0;
        } else if (cases[i].state == PJ_UI_STATE_TIMER) {
            projection.timer_running = 0;
            projection.timer_remaining_ms = 90000;
        } else {
            projection.interval_running = 0;
            projection.interval_remaining_ms = 120000;
            projection.interval_phase = 0;
        }
        pj_ui_set_time_projection(&ui, &projection);
        assert(pj_ui_is_dirty(&ui) == 1 && ui.dirty.partial == 1);
    }
}

static void test_time_projection_refreshes_changed_controls(void)
{
    const pj_ui_state_t states[] = {
        PJ_UI_STATE_STOPWATCH, PJ_UI_STATE_TIMER, PJ_UI_STATE_INTERVAL,
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        pj_ui_context_t ui;
        pj_ui_init(&ui);
        ui.state = states[i];
        pj_ui_time_projection_t projection = projection_from_ui(&ui);
        if (states[i] == PJ_UI_STATE_STOPWATCH) projection.stopwatch_running = 1;
        if (states[i] == PJ_UI_STATE_TIMER) projection.timer_running = 1;
        if (states[i] == PJ_UI_STATE_INTERVAL) projection.interval_running = 1;
        pj_ui_mark_displayed(&ui);
        pj_ui_set_time_projection(&ui, &projection);
        assert(pj_ui_is_dirty(&ui) == 1);
        assert(ui.dirty.partial == 1);
        assert(ui.dirty.width * ui.dirty.height < PJ_DISPLAY_WIDTH * PJ_DISPLAY_HEIGHT);
    }

    pj_ui_context_t timer;
    pj_ui_init(&timer);
    timer.state = PJ_UI_STATE_TIMER;
    timer.timer_running = 1;
    timer.timer_seconds = 1;
    pj_ui_mark_displayed(&timer);
    pj_ui_time_projection_t timer_projection = projection_from_ui(&timer);
    timer_projection.timer_running = 0;
    timer_projection.timer_remaining_ms = 0;
    pj_ui_set_time_projection(&timer, &timer_projection);
    assert(timer.timer_running == 0 && timer.timer_seconds == 0);
    assert(timer.dirty.partial == 1);

    pj_ui_context_t alarm;
    pj_ui_init(&alarm);
    alarm.state = PJ_UI_STATE_ALARM;
    pj_ui_time_projection_t alarm_projection = projection_from_ui(&alarm);
    alarm_projection.alarm_enabled = 1;
    pj_ui_mark_displayed(&alarm);
    pj_ui_set_time_projection(&alarm, &alarm_projection);
    assert(alarm.dirty.partial == 1);
    assert(alarm.dirty.y == 0);
    assert(alarm.dirty.y + alarm.dirty.height >= 112);
}

static void test_active_alert_does_not_create_modal_navigation(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_HOME;
    ui.stopwatch_running = 1;
    ui.timer_running = 1;
    ui.interval_running = 1;
    pj_ui_time_projection_t projection = time_projection_with_alert(51, PJ_TIME_ALERT_TIMER);
    pj_ui_set_time_projection(&ui, &projection);

    pj_ui_sleep(&ui);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_STATIC);
    assert(ui.stopwatch_running == 1 && ui.timer_running == 1 && ui.interval_running == 1);
    pj_ui_wake(&ui);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIME_TEMP);
    pj_ui_time_command_t command;
    assert(pj_ui_consume_time_command(&ui, &command) == 0);

    projection = time_projection_with_alert(52, PJ_TIME_ALERT_ALARM);
    pj_ui_set_time_projection(&ui, &projection);
    assert(pj_ui_handle_aux_double(&ui) == 1);
    assert(ui.state == PJ_UI_STATE_RECORD);
    assert(ui.record_state == PJ_RECORD_ACTIVE);
    assert(pj_ui_consume_time_command(&ui, &command) == 0);

    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_HOME;
    projection = time_projection_with_alert(53, PJ_TIME_ALERT_TIMER);
    pj_ui_set_time_projection(&ui, &projection);
    assert(pj_ui_handle_touch(&ui, 20, 40, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_NOTES);
}

static void test_sleep_preserves_durable_time_activity(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_HOME;
    ui.stopwatch_running = 1;
    ui.timer_running = 1;
    ui.interval_running = 1;

    pj_ui_sleep(&ui);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_STATIC);
    assert(ui.stopwatch_running == 1);
    assert(ui.timer_running == 1);
    assert(ui.interval_running == 1);
}

static void test_recovery_metadata_does_not_add_subtext_or_touch_target(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t before;
    pj_framebuffer_t after;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_TIMER;
    ui.timer_seconds = 30;
    pj_ui_render(&ui, &before);
    pj_ui_mark_displayed(&ui);
    pj_ui_time_projection_t projection = {0};
    projection.timer_remaining_ms = 30000;
    projection.recovery_time_uncertain = 1;
    pj_ui_set_time_projection(&ui, &projection);

    assert(pj_ui_is_dirty(&ui) == 0);
    pj_ui_request_full_refresh(&ui);
    pj_ui_render(&ui, &after);
    assert(memcmp(&before, &after, sizeof(before)) == 0);
    assert(pj_ui_handle_touch(&ui, 170, 16, PJ_TOUCH_TAP) == 0);
    pj_ui_time_command_t command;
    assert(pj_ui_consume_time_command(&ui, &command) == 0);
}

static void test_time_controls_emit_explicit_commands(void)
{
    pj_ui_context_t ui;
    pj_ui_time_command_t command;
    pj_ui_init(&ui);

    ui.state = PJ_UI_STATE_STOPWATCH;
    ui.stopwatch_running = 1;
    ui.stopwatch_seconds = 42;
    assert(pj_ui_handle_touch(&ui, 170, 126, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_STOPWATCH_RESET);

    ui.state = PJ_UI_STATE_TIMER;
    ui.focus_index = 0;
    ui.timer_running = 0;
    ui.timer_seconds = 90;
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_TIMER_START);
    assert(command.duration_ms == 90000);
    pj_ui_time_projection_t projection = projection_from_ui(&ui);
    projection.timer_running = 1;
    projection.timer_remaining_ms = 90000;
    pj_ui_set_time_projection(&ui, &projection);
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_TIMER_PAUSE);
    assert(pj_ui_handle_touch(&ui, 170, 126, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_TIMER_RESET);

    ui.state = PJ_UI_STATE_INTERVAL;
    ui.focus_index = 0;
    ui.interval_running = 0;
    ui.interval_seconds = 120;
    ui.interval_preset_seconds = 120;
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_INTERVAL_START);
    assert(command.duration_ms == 120000);
    assert(command.secondary_duration_ms == 120000);
    projection = projection_from_ui(&ui);
    projection.interval_running = 1;
    projection.interval_remaining_ms = 120000;
    pj_ui_set_time_projection(&ui, &projection);
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_INTERVAL_PAUSE);
    assert(pj_ui_handle_touch(&ui, 170, 126, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_INTERVAL_RESET);

    ui.state = PJ_UI_STATE_TIMER;
    ui.timer_seconds = 86400;
    assert(pj_ui_handle_touch(&ui, 150, 177, PJ_TOUCH_TAP) == 1);
    assert(ui.timer_seconds == 86400);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.duration_ms == 86400000);

    ui.state = PJ_UI_STATE_INTERVAL;
    ui.interval_seconds = 86400;
    assert(pj_ui_handle_touch(&ui, 150, 177, PJ_TOUCH_TAP) == 1);
    assert(ui.interval_seconds == 86400);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.duration_ms == 86400000);
}

static void test_aux_back_resets_time_pages_before_returning(void)
{
    const struct {
        pj_ui_state_t state;
        pj_ui_time_command_type_t command;
    } cases[] = {
        {PJ_UI_STATE_STOPWATCH, PJ_UI_TIME_COMMAND_STOPWATCH_RESET},
        {PJ_UI_STATE_TIMER, PJ_UI_TIME_COMMAND_TIMER_RESET},
        {PJ_UI_STATE_INTERVAL, PJ_UI_TIME_COMMAND_INTERVAL_RESET},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        pj_ui_context_t ui;
        pj_ui_time_command_t command;
        pj_ui_init(&ui);
        ui.state = cases[i].state;
        ui.stopwatch_running = 1;
        ui.stopwatch_seconds = 42;
        ui.timer_running = 1;
        ui.timer_seconds = 17;
        ui.timer_preset_seconds = 90;
        ui.interval_running = 1;
        ui.interval_seconds = 11;
        ui.interval_preset_seconds = 120;
        ui.interval_round = 4;

        assert(pj_ui_handle_aux_long(&ui) == 1);
        assert(ui.state == PJ_UI_STATE_TIME);
        assert(pj_ui_consume_time_command(&ui, &command) == 1);
        assert(command.type == cases[i].command);
        if (cases[i].state == PJ_UI_STATE_STOPWATCH) {
            assert(ui.stopwatch_running == 0 && ui.stopwatch_seconds == 0);
        } else if (cases[i].state == PJ_UI_STATE_TIMER) {
            assert(ui.timer_running == 0 && ui.timer_seconds == 90);
        } else {
            assert(ui.interval_running == 0 && ui.interval_seconds == 120);
            assert(ui.interval_round == 0);
        }
    }

    pj_ui_context_t pending;
    pj_ui_init(&pending);
    pending.state = PJ_UI_STATE_TIMER;
    pending.time_command.type = PJ_UI_TIME_COMMAND_TIMER_START;
    pending.timer_running = 1;
    pending.timer_seconds = 30;
    assert(pj_ui_handle_aux_long(&pending) == 0);
    assert(pending.state == PJ_UI_STATE_TIMER);
    assert(pending.timer_running == 1 && pending.timer_seconds == 30);
}

static void test_persistent_interval_reset_discards_stale_ui_command(void)
{
    static const pj_ui_time_command_type_t interval_commands[] = {
        PJ_UI_TIME_COMMAND_INTERVAL_START,
        PJ_UI_TIME_COMMAND_INTERVAL_PAUSE,
        PJ_UI_TIME_COMMAND_INTERVAL_RESET,
    };
    for (size_t i = 0; i < sizeof(interval_commands) / sizeof(interval_commands[0]); i++) {
        pj_ui_context_t ui;
        pj_ui_time_command_t consumed;
        pj_ui_init(&ui);
        ui.time_command.type = interval_commands[i];
        ui.time_command.duration_ms = 90000;
        assert(pj_ui_discard_pending_interval_command(&ui) == 1);
        assert(pj_ui_consume_time_command(&ui, &consumed) == 0);
    }

    pj_ui_context_t unrelated;
    pj_ui_init(&unrelated);
    unrelated.time_command.type = PJ_UI_TIME_COMMAND_TIMER_START;
    assert(pj_ui_discard_pending_interval_command(&unrelated) == 0);
    pj_ui_time_command_t consumed;
    assert(pj_ui_consume_time_command(&unrelated, &consumed) == 1);
    assert(consumed.type == PJ_UI_TIME_COMMAND_TIMER_START);
}

static void test_alerts_never_replace_or_intercept_the_active_page(void)
{
    pj_ui_context_t ui;
    pj_ui_time_command_t command;
    pj_framebuffer_t before;
    pj_framebuffer_t after;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_SETTINGS;
    pj_ui_render(&ui, &before);

    pj_ui_time_projection_t projection = time_projection_with_alert(61, PJ_TIME_ALERT_ALARM);
    pj_ui_set_time_projection(&ui, &projection);
    pj_ui_render(&ui, &after);
    assert(memcmp(&before, &after, sizeof(before)) == 0);

    assert(pj_ui_handle_touch(&ui, 40, 166, PJ_TOUCH_TAP) == 1);
    assert(ui.active_alert.id == 61);
    assert(pj_ui_consume_time_command(&ui, &command) == 0);
    assert(ui.clock_24h == 0);

    assert(pj_ui_handle_touch(&ui, 150, 166, PJ_TOUCH_TAP) == 1);
    assert(ui.active_alert.id == 61);
    assert(ui.state == PJ_UI_STATE_SYNC);
    assert(pj_ui_consume_time_command(&ui, &command) == 0);
}

static void test_sync_state_is_board_driven(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_SYNC;
    pj_ui_mark_displayed(&ui);

    assert(pj_ui_handle_aux_short(&ui) == 0);
    assert(ui.state == PJ_UI_STATE_SYNC);
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(ui.state == PJ_UI_STATE_SETTINGS);
    ui.state = PJ_UI_STATE_SYNC;
    pj_ui_mark_displayed(&ui);
    pj_ui_set_sync_state(&ui, 4, 2, 1);
    assert(ui.sync_pending == 4);
    assert(ui.sync_transferred == 2);
    assert(ui.sync_online == 1);
    assert(ui.dirty.partial == 1);
    assert(ui.dirty.y <= 50);
    assert(ui.dirty.y + ui.dirty.height == PJ_DISPLAY_HEIGHT);

    pj_ui_mark_displayed(&ui);
    pj_ui_set_sync_state(&ui, -1, -2, 0);
    assert(ui.sync_pending == 0);
    assert(ui.sync_transferred == 0);
    assert(ui.sync_online == 0);
}

static void test_sync_render_distinguishes_transport_and_failure_states(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t pending;
    pj_framebuffer_t active;
    pj_framebuffer_t offline;
    pj_framebuffer_t auth_failed;
    pj_framebuffer_t protocol_failed;
    pj_framebuffer_t terminal_failed;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_SYNC;
    pj_ui_set_sync_state(&ui, 3, 1, 1);

    pj_ui_set_sync_detail(&ui, "pending", 0, "", 1);
    pj_ui_render(&ui, &pending);
    pj_ui_set_sync_detail(&ui, "running", 0, "", 1);
    pj_ui_render(&ui, &active);
    pj_ui_set_sync_state(&ui, 3, 1, 0);
    pj_ui_set_sync_detail(&ui, "offline", 0, "", 1);
    pj_ui_render(&ui, &offline);
    pj_ui_set_sync_detail(&ui, "auth_failed", 0, "", 1);
    pj_ui_render(&ui, &auth_failed);
    pj_ui_set_sync_detail(&ui, "protocol_failed", 0, "", 1);
    pj_ui_render(&ui, &protocol_failed);
    pj_ui_set_sync_detail(&ui, "failed", 1, "", 0);
    pj_ui_render(&ui, &terminal_failed);

    assert(count_pixel_differences_in_region(
               &pending, &active, 0, 0, PJ_DISPLAY_WIDTH, 70) > 20);
    assert(count_pixel_differences_in_region(
               &offline, &auth_failed, 0, 0, PJ_DISPLAY_WIDTH, 70) > 20);
    assert(count_pixel_differences_in_region(
               &auth_failed, &protocol_failed,
               0, 0, PJ_DISPLAY_WIDTH, 70) == 0);
    assert(count_pixel_differences_in_region(
               &auth_failed, &protocol_failed,
               0, 145, PJ_DISPLAY_WIDTH, 55) > 20);
    assert(count_pixel_differences_in_region(
               &protocol_failed, &terminal_failed,
               0, 145, PJ_DISPLAY_WIDTH, 55) > 20);
}

static void test_dirty_lifecycle(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);

    assert(pj_ui_is_dirty(&ui) == 1);
    assert(ui.dirty.partial == 0);

    pj_ui_set_status(&ui, 42, 21, 45);
    assert(pj_ui_is_dirty(&ui) == 1);
    assert(ui.dirty.partial == 0);

    pj_ui_mark_displayed(&ui);
    assert(pj_ui_is_dirty(&ui) == 0);

    pj_ui_wake(&ui);
    pj_ui_mark_displayed(&ui);
    pj_ui_set_status(&ui, 43, 22, 46);
    assert(pj_ui_is_dirty(&ui) == 0);
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(ui.state == PJ_UI_STATE_TIME_TEMP);
    pj_ui_mark_displayed(&ui);
    pj_ui_set_status(&ui, 44, 23, 47);
    assert(pj_ui_is_dirty(&ui) == 1);
    assert(ui.dirty.partial == 1);
    assert(ui.dirty.y == 118);

    pj_ui_mark_displayed(&ui);
    pj_ui_set_time(&ui, 10, 42, 2026, 6, 6);
    assert(pj_ui_is_dirty(&ui) == 1);
    assert(ui.dirty.partial == 1);
    assert(ui.dirty.x == 0);
    assert(ui.dirty.y == 0);
    assert(ui.dirty.width == PJ_DISPLAY_WIDTH);
    assert(ui.dirty.height == 84);

    pj_ui_mark_displayed(&ui);
    pj_ui_set_time(&ui, 10, 43, 2026, 6, 7);
    assert(pj_ui_is_dirty(&ui) == 1);
    assert(ui.dirty.partial == 0);

    pj_ui_request_full_refresh(&ui);
    assert(pj_ui_is_dirty(&ui) == 1);
    assert(ui.dirty.partial == 0);
}

static void test_dynamic_updates_force_periodic_full_refresh(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_STOPWATCH;
    ui.stopwatch_running = 1;
    pj_ui_mark_displayed(&ui);

    for (int update = 1; update <= 120; update++) {
        pj_ui_time_projection_t projection = projection_from_ui(&ui);
        projection.stopwatch_elapsed_ms = (uint64_t)update * 1000u;
        pj_ui_set_time_projection(&ui, &projection);
        assert(ui.dirty.partial == 1);
        assert(ui.dirty.x == 0 && ui.dirty.y == 0);
        assert(ui.dirty.width == PJ_DISPLAY_WIDTH && ui.dirty.height == 100);
        pj_ui_mark_displayed(&ui);
    }
    assert(ui.partial_refresh_count == 120);
    assert(pj_ui_is_dirty(&ui) == 0);
}

static void test_recording_elapsed_projection_is_bounded_and_monotonic_by_seconds(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_RECORD;
    ui.record_state = PJ_RECORD_ACTIVE;
    pj_ui_mark_displayed(&ui);

    pj_ui_set_recording_elapsed(&ui, 1999);
    assert(ui.recording_seconds == 1);
    assert(ui.dirty.partial == 1);
    assert(ui.dirty.y == 55 && ui.dirty.height == 90);
    pj_ui_mark_displayed(&ui);
    assert(pj_ui_tick(&ui) == 0);
    assert(ui.recording_seconds == 1);
    assert(pj_ui_is_dirty(&ui) == 0);
    pj_ui_set_recording_elapsed(&ui, 1999);
    assert(pj_ui_is_dirty(&ui) == 0);
    pj_ui_set_recording_elapsed(&ui, 2000);
    assert(ui.recording_seconds == 2);
    assert(ui.dirty.partial == 1);
}

static void test_partial_render_preserves_outside_region(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t before;
    pj_framebuffer_t after;
    pj_ui_init(&ui);
    pj_ui_wake(&ui);
    assert(pj_ui_handle_aux_long(&ui) == 1);
    pj_ui_render(&ui, &before);
    after = before;
    pj_ui_mark_displayed(&ui);

    pj_ui_set_status(&ui, 41, 23, 47);
    assert(ui.dirty.partial == 1);
    pj_ui_render(&ui, &after);

    for (int y = 0; y < ui.dirty.y; y++) {
        for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
            assert(pj_framebuffer_get(&after, x, y) == pj_framebuffer_get(&before, x, y));
        }
    }
}

static void test_record_partial_render_replaces_status_through_bottom_edge(void)
{
    pj_ui_context_t ui;
    pj_ui_context_t expected_ui;
    pj_framebuffer_t before;
    pj_framebuffer_t partial;
    pj_framebuffer_t expected;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_RECORD;
    ui.record_state = PJ_RECORD_IDLE;
    pj_ui_render(&ui, &partial);
    before = partial;
    pj_ui_mark_displayed(&ui);

    pj_ui_set_audio_state(&ui, 1, 0);
    assert(ui.state == PJ_UI_STATE_RECORD);
    assert(ui.record_state == PJ_RECORD_ACTIVE);
    assert(ui.dirty.partial == 1);
    assert(ui.dirty.x == 0 && ui.dirty.y == 0);
    assert(ui.dirty.width == PJ_DISPLAY_WIDTH);
    assert(ui.dirty.y + ui.dirty.height == PJ_DISPLAY_HEIGHT);
    assert(pj_ui_tick(&ui) == 0);
    pj_ui_set_recording_elapsed(&ui, 1000);
    pj_ui_render(&ui, &partial);

    expected_ui = ui;
    pj_ui_request_full_refresh(&expected_ui);
    pj_ui_render(&expected_ui, &expected);
    assert(memcmp(&partial, &expected, sizeof(expected)) == 0);
    assert(count_pixel_differences_in_region(
               &before, &partial, 0, 0, PJ_DISPLAY_WIDTH, PJ_DISPLAY_HEIGHT) > 0);
}

static void test_child_screens_have_no_back_affordance_or_hidden_touch_target(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);

    ui.state = PJ_UI_STATE_TIMER;
    (void)pj_ui_handle_touch(&ui, 16, 16, PJ_TOUCH_TAP);
    assert(ui.state == PJ_UI_STATE_TIMER);
    assert(pj_ui_handle_touch(&ui, 100, 100, PJ_TOUCH_LONG_PRESS) == 0);
    assert(ui.state == PJ_UI_STATE_TIMER);
    assert(pj_ui_handle_touch(&ui, 100, 100, PJ_TOUCH_SWIPE_RIGHT) == 0);
    assert(ui.state == PJ_UI_STATE_TIMER);

    ui.state = PJ_UI_STATE_LISTEN;
    (void)pj_ui_handle_touch(&ui, 16, 16, PJ_TOUCH_TAP);
    assert(ui.state == PJ_UI_STATE_LISTEN);

    ui.state = PJ_UI_STATE_READ;
    (void)pj_ui_handle_touch(&ui, 16, 16, PJ_TOUCH_TAP);
    assert(ui.state == PJ_UI_STATE_READ);

    ui.state = PJ_UI_STATE_NOTE_DETAIL;
    ui.note_detail_transcript = 1;
    (void)pj_ui_handle_touch(&ui, 16, 16, PJ_TOUCH_TAP);
    assert(ui.state == PJ_UI_STATE_NOTE_DETAIL);
}

static void test_voice_note_back_paths_are_consistent(void)
{
    const pj_ui_state_t states[] = {
        PJ_UI_STATE_LISTEN, PJ_UI_STATE_READ, PJ_UI_STATE_NOTE_DETAIL,
    };
    const pj_ui_state_t parents[] = {
        PJ_UI_STATE_NOTES, PJ_UI_STATE_NOTES, PJ_UI_STATE_LISTEN,
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        pj_ui_context_t ui;
        pj_ui_init(&ui);
        ui.state = states[i];
        assert(pj_ui_handle_aux_long(&ui) == 1);
        assert(ui.state == parents[i]);
    }

    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_NOTE_DETAIL;
    ui.note_detail_transcript = 1;
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(ui.state == PJ_UI_STATE_READ);

    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_NOTE_DETAIL;
    ui.playback_state = PJ_PLAYBACK_ACTIVE;
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(ui.state == PJ_UI_STATE_NOTE_DETAIL);
    assert(ui.playback_state == PJ_PLAYBACK_STOPPING);
    assert(ui.playback_exit_pending == 1);
    pj_ui_set_audio_state(&ui, 0, 0);
    assert(ui.state == PJ_UI_STATE_LISTEN);

    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_RECORD;
    ui.record_state = PJ_RECORD_ACTIVE;
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(ui.state == PJ_UI_STATE_HOME);
    assert(ui.record_state == PJ_RECORD_STOPPING);
}

static void test_note_detail_back_restores_selected_page(void)
{
    const char labels[6][PJ_UI_NOTE_LABEL_LEN] = {
        "One", "Two", "Three", "Four", "Five", "Six",
    };

    for (int transcript = 0; transcript < 2; transcript++) {
        pj_ui_context_t ui;
        pj_ui_init(&ui);
        pj_ui_set_notes(&ui, 6, labels);
        ui.state = transcript ? PJ_UI_STATE_READ : PJ_UI_STATE_LISTEN;
        ui.note_page = 1;
        ui.focus_index = 4;
        assert(pj_ui_handle_touch(&ui, 100, 75, PJ_TOUCH_TAP) == 1);
        assert(ui.state == PJ_UI_STATE_NOTE_DETAIL && ui.selected_note == 4);
        assert(pj_ui_handle_aux_long(&ui) == 1);
        assert(ui.state == (transcript ? PJ_UI_STATE_READ : PJ_UI_STATE_LISTEN));
        assert(ui.note_page == 1 && ui.focus_index == 4);
    }

    pj_ui_context_t playback;
    pj_ui_init(&playback);
    pj_ui_set_notes(&playback, 6, labels);
    playback.state = PJ_UI_STATE_LISTEN;
    playback.note_page = 1;
    playback.focus_index = 4;
    assert(pj_ui_handle_touch(&playback, 100, 75, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_handle_aux_short(&playback) == 1);
    assert(playback.playback_state == PJ_PLAYBACK_ACTIVE);
    assert(pj_ui_handle_aux_long(&playback) == 1);
    pj_ui_set_audio_state(&playback, 0, 0);
    assert(playback.state == PJ_UI_STATE_LISTEN);
    assert(playback.note_page == 1 && playback.focus_index == 4);
}

static void test_twelve_hour_alarm_distinguishes_am_and_pm(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t morning;
    pj_framebuffer_t evening;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_ALARM;
    ui.clock_24h = 0;
    ui.alarm_hour = 7;
    pj_ui_render(&ui, &morning);
    ui.alarm_hour = 19;
    pj_ui_render(&ui, &evening);
    assert(memcmp(&morning, &evening, sizeof(morning)) != 0);
}

static void test_twelve_hour_time_temp_omits_am_pm_suffix(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t morning;
    pj_framebuffer_t evening;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_TIME_TEMP;
    ui.clock_24h = 0;
    ui.hour = 7;
    ui.minute = 41;
    pj_ui_render(&ui, &morning);
    ui.hour = 19;
    pj_ui_render(&ui, &evening);
    assert(memcmp(&morning, &evening, sizeof(morning)) == 0);
}

static void test_clock_always_shows_celsius_and_fahrenheit(void)
{
    pj_ui_context_t celsius_first;
    pj_ui_context_t fahrenheit_first;
    pj_framebuffer_t celsius_first_fb;
    pj_framebuffer_t fahrenheit_first_fb;
    pj_ui_init(&celsius_first);
    pj_ui_init(&fahrenheit_first);
    celsius_first.state = PJ_UI_STATE_TIME_TEMP;
    fahrenheit_first.state = PJ_UI_STATE_TIME_TEMP;
    fahrenheit_first.temperature_fahrenheit = 1;
    pj_ui_render(&celsius_first, &celsius_first_fb);
    pj_ui_render(&fahrenheit_first, &fahrenheit_first_fb);

    assert(memcmp(&celsius_first_fb, &fahrenheit_first_fb,
                  sizeof(celsius_first_fb)) != 0);
    assert(count_black_pixels_in_region(&celsius_first_fb, 0, 118, 200, 44) > 500);
    assert(count_black_pixels_in_region(&fahrenheit_first_fb, 0, 118, 200, 44) > 500);
}

static void test_settings_labels_stay_inside_quadrant_borders(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t fb;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_SETTINGS;
    pj_ui_render(&ui, &fb);

    for (int row = 0; row < 2; row++) {
        for (int column = 0; column < 2; column++) {
            int x = column * 100;
            int y = row * 100;
            assert(count_black_pixels_in_region(&fb, x + 3, y + 12, 1, 76) == 0);
            assert(count_black_pixels_in_region(&fb, x + 96, y + 12, 1, 76) == 0);
            assert(count_black_pixels_in_region(&fb, x + 8, y + 8, 84, 84) > 50);
        }
    }
}

static void test_text_rendering_is_uppercase_at_the_boundary(void)
{
    pj_ui_context_t mixed;
    pj_ui_context_t upper;
    pj_framebuffer_t mixed_fb;
    pj_framebuffer_t upper_fb;
    const char mixed_labels[][PJ_UI_NOTE_LABEL_LEN] = {"Tall Letters Qgj"};
    const char upper_labels[][PJ_UI_NOTE_LABEL_LEN] = {"TALL LETTERS QGJ"};

    assert(strcmp(pj_ui_default_font_name(), "IBM Plex Mono Bold") == 0);

    pj_ui_init(&mixed);
    pj_ui_init(&upper);
    pj_ui_set_notes(&mixed, 1, mixed_labels);
    pj_ui_set_notes(&upper, 1, upper_labels);
    mixed.state = PJ_UI_STATE_READ;
    upper.state = PJ_UI_STATE_READ;
    pj_ui_render(&mixed, &mixed_fb);
    pj_ui_render(&upper, &upper_fb);
    assert(memcmp(&mixed_fb, &upper_fb, sizeof(mixed_fb)) == 0);
}

static void test_compiled_static_art_is_immutable(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t fallback;
    pj_framebuffer_t fallback_after_clock_change;
    pj_framebuffer_t fallback_dark;
    pj_ui_init(&ui);

    pj_ui_render(&ui, &fallback);
    assert(PJ_DEFAULT_STATIC_ART_WIDTH == PJ_DISPLAY_WIDTH);
    assert(PJ_DEFAULT_STATIC_ART_HEIGHT == PJ_DISPLAY_HEIGHT);
    assert(PJ_DEFAULT_STATIC_ART_BYTES == PJ_FRAMEBUFFER_BYTES);
    assert(count_black_pixels(&fallback) == PJ_DEFAULT_STATIC_ART_BLACK_PIXELS);
    assert(memcmp(fallback.pixels, pj_default_static_art, sizeof(fallback.pixels)) == 0);
    pj_ui_set_time(&ui, 23, 59, 2030, 12, 31);
    pj_ui_set_status(&ui, 7, -4, 20);
    pj_ui_render(&ui, &fallback_after_clock_change);
    assert(memcmp(&fallback, &fallback_after_clock_change, sizeof(fallback)) == 0);

    ui.dark_mode = 1;
    pj_ui_request_full_refresh(&ui);
    pj_ui_render(&ui, &fallback_dark);
    assert(memcmp(&fallback, &fallback_dark, sizeof(fallback)) == 0);
    ui.dark_mode = 0;

    assert(sizeof(ui) < PJ_FRAMEBUFFER_BYTES);
}

static void test_long_note_label_stays_inside_editorial_row(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t fb;
    const char labels[][PJ_UI_NOTE_LABEL_LEN] = {
        "MAXIMUM-LABEL-TXT",
    };
    pj_ui_init(&ui);
    pj_ui_set_notes(&ui, 1, labels);
    ui.state = PJ_UI_STATE_LISTEN;

    pj_ui_render(&ui, &fb);
    assert(count_black_pixels(&fb) > 0);
    assert(count_black_pixels_in_region(&fb, 5, 5, 190, 40) > 100);
}

static void test_note_detail_title_respects_side_margins(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t fb;
    const char labels[][PJ_UI_NOTE_LABEL_LEN] = {
        "WWWWWWWWWWWWWWWWW",
    };
    pj_ui_init(&ui);
    pj_ui_set_notes(&ui, 1, labels);
    ui.state = PJ_UI_STATE_NOTE_DETAIL;
    ui.selected_note = 0;

    pj_ui_render(&ui, &fb);
    assert(count_black_pixels_in_region(&fb, 18, 18, 164, 164) > 500);
}

static void test_long_home_title_is_ellipsized_inside_header(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t first;
    pj_framebuffer_t second;
    pj_ui_init(&ui);
    strcpy(ui.home_layout.title, "My long personal journal");
    ui.state = PJ_UI_STATE_HOME;
    pj_ui_render(&ui, &first);
    strcpy(ui.home_layout.title, "Another hidden title");
    pj_ui_render(&ui, &second);
    assert(memcmp(&first, &second, sizeof(first)) == 0);
}

static void test_calendar_divider_stays_above_empty_state(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t fb;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_CALENDAR;
    pj_ui_render(&ui, &fb);

    assert(count_black_pixels_in_region(&fb, 10, 50, 180, 100) > 500);
    assert(count_black_pixels_in_region(&fb, 60, 91, 130, 1) < 100);
}

static void test_polished_scenes_render_stably(void)
{
    static const pj_ui_state_t stable_states[] = {
        PJ_UI_STATE_STATIC,
        PJ_UI_STATE_CALENDAR,
        PJ_UI_STATE_NOTE_DETAIL,
    };
    pj_ui_context_t ui;
    pj_framebuffer_t first;
    pj_framebuffer_t second;
    pj_framebuffer_t record_frames[3];
    pj_ui_init(&ui);
    pj_ui_set_time(&ui, 9, 41, 2026, 7, 11);

    for (size_t i = 0; i < sizeof(stable_states) / sizeof(stable_states[0]); i++) {
        ui.state = stable_states[i];
        pj_ui_render(&ui, &first);
        pj_ui_render(&ui, &second);
        assert(count_black_pixels(&first) > 0);
        assert(memcmp(&first, &second, sizeof(first)) == 0);
    }

    ui.state = PJ_UI_STATE_RECORD;
    for (int state = PJ_RECORD_IDLE; state <= PJ_RECORD_STOPPING; state++) {
        ui.record_state = (pj_record_state_t)state;
        ui.recording_seconds = state * 61;
        pj_ui_render(&ui, &record_frames[state]);
        pj_ui_render(&ui, &second);
        assert(count_black_pixels(&record_frames[state]) > 0);
        assert(memcmp(&record_frames[state], &second, sizeof(second)) == 0);
    }
    assert(memcmp(&record_frames[PJ_RECORD_IDLE],
                  &record_frames[PJ_RECORD_ACTIVE], sizeof(first)) != 0);
    assert(memcmp(&record_frames[PJ_RECORD_ACTIVE],
                  &record_frames[PJ_RECORD_STOPPING], sizeof(first)) != 0);
}

static void test_render_all_states(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t fb;
    pj_ui_init(&ui);

    for (int state = 0; state < PJ_UI_STATE_COUNT; state++) {
        ui.state = (pj_ui_state_t)state;
        pj_ui_render(&ui, &fb);
        int black_pixels = count_black_pixels(&fb);
        if (black_pixels <= 0) {
            fprintf(stderr, "empty render for state %d (%s)\n", state, pj_ui_state_name((pj_ui_state_t)state));
        }
        assert(black_pixels > 0);
        assert(pj_ui_state_name((pj_ui_state_t)state) != 0);
    }
}

int main(void)
{
    test_state_graph();
    test_empty_notes_do_not_open_detail();
    test_note_paging_tracks_touch_selection_and_swipes();
    test_note_pager_arrows_move_exactly_three_items_and_stop_at_bounds();
    test_note_pager_uses_chevrons_without_arrow_bars();
    test_read_opens_transcript_detail_without_playback();
    test_note_detail_play_control_has_touch_parity();
    test_custom_home_slots_render_and_route_in_order();
    test_compiled_home_fallback_routes_every_slot();
    test_every_supported_home_destination_routes();
    test_home_layout_setter_canonicalizes_inactive_slots();
    test_aux_primary_actions_do_not_use_hidden_focus();
    test_aux_navigation_has_no_hidden_focus();
    test_time_value_typography_fits_refresh_regions();
    test_interval_round_is_rendered_zero_indexed();
    test_aux_double_is_direct_quick_record_only();
    test_audio_lifecycle_reconciliation();
    test_settings_quadrants_control_volume_appearance_clock_and_sync();
    test_alarm_touch_regions_match_enable_and_adjustment_controls();
    test_api_preferences_remain_available_after_settings_flattening();
    test_settings_volume_opens_dedicated_screen();
    test_volume_only_bottom_controls_adjust_and_clamp();
    test_volume_fill_uses_full_top_half_and_controls_have_thick_borders();
    test_timer_presets_are_not_runtime_counters();
    test_interval_repeats_one_stable_duration_across_rounds();
    test_timer_geometry_and_adjustment_focus_timeout();
    test_time_projection_updates_values_without_alert_subscreen();
    test_interval_alert_stays_inline_and_aux_long_resets();
    test_recovered_interval_alert_is_nonmodal_on_every_screen();
    test_time_command_buttons_wait_for_authoritative_projection();
    test_time_projection_refreshes_changed_controls();
    test_active_alert_does_not_create_modal_navigation();
    test_sleep_preserves_durable_time_activity();
    test_recovery_metadata_does_not_add_subtext_or_touch_target();
    test_time_controls_emit_explicit_commands();
    test_aux_back_resets_time_pages_before_returning();
    test_persistent_interval_reset_discards_stale_ui_command();
    test_alerts_never_replace_or_intercept_the_active_page();
    test_sync_state_is_board_driven();
    test_sync_render_distinguishes_transport_and_failure_states();
    test_dirty_lifecycle();
    test_dynamic_updates_force_periodic_full_refresh();
    test_recording_elapsed_projection_is_bounded_and_monotonic_by_seconds();
    test_partial_render_preserves_outside_region();
    test_record_partial_render_replaces_status_through_bottom_edge();
    test_child_screens_have_no_back_affordance_or_hidden_touch_target();
    test_voice_note_back_paths_are_consistent();
    test_note_detail_back_restores_selected_page();
    test_twelve_hour_alarm_distinguishes_am_and_pm();
    test_twelve_hour_time_temp_omits_am_pm_suffix();
    test_clock_always_shows_celsius_and_fahrenheit();
    test_settings_labels_stay_inside_quadrant_borders();
    test_text_rendering_is_uppercase_at_the_boundary();
    test_compiled_static_art_is_immutable();
    test_long_note_label_stays_inside_editorial_row();
    test_note_detail_title_respects_side_margins();
    test_long_home_title_is_ellipsized_inside_header();
    test_calendar_divider_stays_above_empty_state();
    test_polished_scenes_render_stably();
    test_render_all_states();
    puts("ui tests passed");
    return 0;
}
