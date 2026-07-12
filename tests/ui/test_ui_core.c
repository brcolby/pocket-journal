#include "pj_ui.h"

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

static void assert_region_clear(const pj_framebuffer_t *fb, int x, int y, int width, int height)
{
    for (int row = y; row < y + height; row++) {
        for (int column = x; column < x + width; column++) {
            assert(pj_framebuffer_get(fb, column, row) == 0);
        }
    }
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
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIME_TEMP);

    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);

    pj_ui_wake(&ui);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIME_TEMP);

    assert(pj_ui_handle_touch(&ui, 100, 150, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);

    assert(pj_ui_handle_touch(&ui, 20, 50, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_NOTES);

    assert(pj_ui_handle_touch(&ui, 20, 120, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_LISTEN);

    assert(pj_ui_handle_touch(&ui, 20, 50, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_NOTE_DETAIL);

    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_LISTEN);

    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_NOTES);

    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);

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
    assert(pj_ui_handle_touch(&ui, 20, 50, PJ_TOUCH_TAP) == 0);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_READ);
}

static void test_note_paging_tracks_hardware_focus_and_swipes(void)
{
    pj_ui_context_t ui;
    const char labels[6][PJ_UI_NOTE_LABEL_LEN] = {
        "One", "Two", "Three", "Four", "Five", "Six",
    };
    pj_ui_init(&ui);
    pj_ui_set_notes(&ui, 6, labels);
    ui.state = PJ_UI_STATE_LISTEN;

    for (int i = 0; i < 4; i++) assert(pj_ui_handle_aux_double(&ui) == 1);
    assert(ui.focus_index == 4);
    assert(ui.note_page == 1);
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(ui.state == PJ_UI_STATE_NOTE_DETAIL);
    assert(ui.selected_note == 4);

    ui.state = PJ_UI_STATE_READ;
    ui.note_page = 0;
    ui.focus_index = 0;
    assert(pj_ui_handle_touch(&ui, 100, 100, PJ_TOUCH_SWIPE_LEFT) == 1);
    assert(ui.note_page == 1 && ui.focus_index == 4);
    assert(pj_ui_handle_touch(&ui, 100, 100, PJ_TOUCH_SWIPE_RIGHT) == 1);
    assert(ui.note_page == 0 && ui.focus_index == 0);

    ui.note_page = 1;
    ui.focus_index = 4;
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(ui.state == PJ_UI_STATE_NOTES);
    ui.focus_index = 2;
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(ui.state == PJ_UI_STATE_READ);
    assert(ui.note_page == 1 && ui.focus_index == 4);
}

static void test_read_opens_transcript_detail_without_playback(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    seed_notes(&ui);
    ui.state = PJ_UI_STATE_READ;

    assert(pj_ui_handle_touch(&ui, 20, 50, PJ_TOUCH_TAP) == 1);
    assert(ui.state == PJ_UI_STATE_NOTE_DETAIL);
    assert(ui.note_detail_transcript == 1);
    assert(ui.playback_state == PJ_PLAYBACK_IDLE);
    assert(pj_ui_handle_touch(&ui, 100, 90, PJ_TOUCH_TAP) == 0);
    assert(ui.playback_state == PJ_PLAYBACK_IDLE);
    assert(pj_ui_handle_aux_short(&ui) == 1);
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
    assert(pj_ui_handle_touch(&ui, 20, 125, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_READ);
    ui.state = PJ_UI_STATE_HOME;
    assert(pj_ui_handle_touch(&ui, 20, 170, PJ_TOUCH_TAP) == 1);
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

static void test_aux_focus_and_activation(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_HOME;

    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);
    assert(ui.focus_index == 1);
    assert(pj_ui_handle_aux_double(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIME);

    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);

    pj_home_layout_t custom;
    memset(&custom, 0, sizeof(custom));
    strcpy(custom.title, "Primary");
    custom.slot_count = 1;
    custom.slots[0] = (pj_home_slot_t) {"Time", "time", "time"};
    assert(pj_ui_set_home_layout(&ui, &custom) == 1);
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIME);

    ui.state = PJ_UI_STATE_HOME;
    ui.home_layout.slot_count = 0;
    assert(pj_ui_handle_aux_short(&ui) == 0);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);
    ui.home_layout.slot_count = 1;
    strcpy(ui.home_layout.slots[0].destination, "invalid");
    assert(pj_ui_handle_aux_short(&ui) == 0);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);

    pj_ui_restore_default_home(&ui);

    ui.state = PJ_UI_STATE_SETTINGS;
    ui.focus_index = 0;
    assert(pj_ui_handle_aux_double(&ui) == 1);
    assert(ui.focus_index == 1);
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_VOLUME);
    int initial_volume = ui.volume;
    assert(ui.focus_index == 0);
    assert(pj_ui_handle_aux_double(&ui) == 1);
    assert(ui.focus_index == 1);
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(ui.volume == initial_volume + 1);
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_SETTINGS);

    ui.state = PJ_UI_STATE_NOTES;
    ui.focus_index = 0;

    assert(pj_ui_handle_aux_double(&ui) == 1);
    assert(ui.focus_index == 1);
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_LISTEN);

    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_NOTES);
    assert(pj_ui_handle_aux_double(&ui) == 1);
    assert(pj_ui_handle_aux_double(&ui) == 1);
    assert(ui.focus_index == 2);
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_READ);
    assert(ui.note_detail_transcript == 0);

    ui.state = PJ_UI_STATE_NOTES;
    ui.focus_index = 0;
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_RECORD);
    assert(ui.record_state == PJ_RECORD_ACTIVE);

    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(ui.record_state == PJ_RECORD_STOPPING);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);
    assert(ui.dirty.partial == 0);
    pj_ui_set_audio_state(&ui, 1, 0);
    assert(ui.record_state == PJ_RECORD_STOPPING);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);
    pj_ui_set_audio_state(&ui, 0, 0);
    assert(ui.record_state == PJ_RECORD_IDLE);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);

    ui.state = PJ_UI_STATE_NOTES;
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(ui.record_state == PJ_RECORD_ACTIVE);

    ui.state = PJ_UI_STATE_TIME;
    ui.focus_index = 0;
    assert(pj_ui_handle_aux_double(&ui) == 1);
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_STOPWATCH);
    assert(ui.stopwatch_running == 0);
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

        int value_top = states[i] == PJ_UI_STATE_INTERVAL ? 70 : 42;
        assert(count_black_pixels_in_region(&fb, 20, value_top, 160, 40) > 0);
        for (int y = 32; y < 150; y++) {
            assert(pj_framebuffer_get(&fb, 0, y) == 0);
            assert(pj_framebuffer_get(&fb, PJ_DISPLAY_WIDTH - 1, y) == 0);
        }

        pj_ui_mark_displayed(&ui);
        if (states[i] == PJ_UI_STATE_STOPWATCH) {
            ui.stopwatch_running = 1;
        } else if (states[i] == PJ_UI_STATE_TIMER) {
            ui.timer_running = 1;
        } else {
            ui.interval_running = 1;
        }
        assert(pj_ui_tick(&ui) == 1);
        assert(ui.dirty.partial == 1);
        assert(ui.dirty.y <= value_top);
        assert(ui.dirty.y + ui.dirty.height >= value_top + 40);
    }
}

static void test_aux_double_cycles_visible_actions_and_falls_back_to_record(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);

    const struct { pj_ui_state_t state; int count; } menus[] = {
        {PJ_UI_STATE_NOTES, 3}, {PJ_UI_STATE_TIME, 4}, {PJ_UI_STATE_SETTINGS, 4},
        {PJ_UI_STATE_VOLUME, 2}, {PJ_UI_STATE_ALARM, 5},
        {PJ_UI_STATE_STOPWATCH, 2}, {PJ_UI_STATE_TIMER, 4}, {PJ_UI_STATE_INTERVAL, 4},
    };
    for (size_t i = 0; i < sizeof(menus) / sizeof(menus[0]); i++) {
        ui.state = menus[i].state;
        ui.focus_index = 0;
        assert(pj_ui_handle_aux_double(&ui) == 1);
        assert(ui.state == menus[i].state);
        assert(ui.focus_index == 1);
        for (int step = 1; step < menus[i].count; step++) {
            assert(pj_ui_handle_aux_double(&ui) == 1);
        }
        assert(ui.focus_index == 0);
    }

    ui.state = PJ_UI_STATE_STATIC;
    assert(pj_ui_handle_aux_double(&ui) == 1);
    assert(ui.state == PJ_UI_STATE_RECORD);
    assert(ui.record_state == PJ_RECORD_ACTIVE);

    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_RECORD;
    ui.record_state = PJ_RECORD_ACTIVE;
    assert(pj_ui_handle_aux_double(&ui) == 0);

    ui.state = PJ_UI_STATE_HOME;
    assert(pj_ui_handle_aux_double(&ui) == 0);
    ui.record_state = PJ_RECORD_IDLE;
    ui.playback_state = PJ_PLAYBACK_ACTIVE;
    assert(pj_ui_handle_aux_double(&ui) == 0);
    ui.playback_state = PJ_PLAYBACK_STOPPING;
    assert(pj_ui_handle_aux_double(&ui) == 0);

    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_STOPWATCH;
    ui.stopwatch_running = 1;
    assert(pj_ui_handle_aux_double(&ui) == 1);
    assert(ui.focus_index == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_STOPWATCH);

    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_TIMER;
    ui.timer_running = 1;
    assert(pj_ui_handle_aux_double(&ui) == 1);
    assert(ui.focus_index == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIMER);

    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_INTERVAL;
    ui.interval_running = 1;
    assert(pj_ui_handle_aux_double(&ui) == 1);
    assert(ui.focus_index == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_INTERVAL);
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
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);
    assert(ui.record_state == PJ_RECORD_STOPPING);
    pj_ui_set_audio_state(&ui, 1, 0);
    assert(ui.record_state == PJ_RECORD_STOPPING);
    assert(pj_ui_handle_aux_short(&ui) == 0);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);
    pj_ui_set_audio_state(&ui, 0, 0);
    assert(ui.record_state == PJ_RECORD_IDLE);
}

static void test_settings_dark_mode_toggle(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_SETTINGS;
    int initial_dark_mode = ui.dark_mode;

    assert(pj_ui_handle_touch(&ui, 20, 130, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_SETTINGS);
    assert(ui.dark_mode != initial_dark_mode);
    assert(ui.dirty.partial == 0);
}

static void test_settings_volume_opens_dedicated_screen(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_SETTINGS;
    ui.volume = 4;

    assert(pj_ui_handle_touch(&ui, 100, 90, PJ_TOUCH_TAP) == 1);
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
    assert(pj_ui_handle_touch(&ui, 100, 116, PJ_TOUCH_TAP) == 0);
    assert(pj_ui_handle_touch(&ui, 100, 166, PJ_TOUCH_TAP) == 0);
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

static void test_timer_presets_are_not_runtime_counters(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_TIMER;

    assert(pj_ui_handle_touch(&ui, 100, 160, PJ_TOUCH_TAP) == 1);
    assert(ui.timer_seconds == 330);
    assert(ui.timer_preset_seconds == 330);
    ui.timer_running = 1;
    assert(pj_ui_tick(&ui) == 1);
    assert(ui.timer_seconds == 329);
    assert(ui.timer_preset_seconds == 330);

    ui.timer_running = 0;
    ui.timer_seconds = 0;
    ui.focus_index = 0;
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(ui.timer_running == 1);
    assert(ui.timer_seconds == 330);

    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_INTERVAL;
    assert(pj_ui_handle_touch(&ui, 100, 160, PJ_TOUCH_TAP) == 1);
    assert(ui.interval_seconds == 1560);
    assert(ui.interval_preset_seconds == 1560);
    ui.interval_running = 1;
    ui.interval_seconds = 1;
    assert(pj_ui_tick(&ui) == 1);
    assert(ui.interval_seconds == 300);
    assert(ui.interval_preset_seconds == 1560);
    ui.interval_seconds = 1;
    assert(pj_ui_tick(&ui) == 1);
    assert(ui.interval_seconds == 1560);
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

static void test_time_projection_and_alert_repaint(void)
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
    assert(pj_ui_is_dirty(&ui) == 1 && ui.dirty.partial == 0);

    pj_ui_mark_displayed(&ui);
    pj_ui_set_time_projection(&ui, &projection);
    assert(pj_ui_is_dirty(&ui) == 0);

    projection.active_alert.skipped_occurrences = 2;
    pj_ui_set_time_projection(&ui, &projection);
    assert(pj_ui_is_dirty(&ui) == 1 && ui.dirty.partial == 0);

    pj_ui_mark_displayed(&ui);
    projection.alert_audio_deferred = 1;
    pj_ui_set_time_projection(&ui, &projection);
    assert(ui.alert_audio_deferred == 1);
    assert(pj_ui_is_dirty(&ui) == 1 && ui.dirty.partial == 0);
}

static void test_active_alert_is_controllable_from_aux(void)
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
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);
    assert(ui.stopwatch_running == 1 && ui.timer_running == 1 && ui.interval_running == 1);
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);
    pj_ui_time_command_t command;
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_ALERT_DISMISS && command.alert_id == 51);

    projection = time_projection_with_alert(52, PJ_TIME_ALERT_ALARM);
    pj_ui_set_time_projection(&ui, &projection);
    assert(pj_ui_handle_aux_double(&ui) == 1);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_ALARM_SNOOZE && command.alert_id == 52);

    projection = time_projection_with_alert(53, PJ_TIME_ALERT_TIMER);
    pj_ui_set_time_projection(&ui, &projection);
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_ALERT_DISMISS && command.alert_id == 53);
    assert(pj_ui_handle_touch(&ui, 20, 40, PJ_TOUCH_TAP) == 0);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);
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

static void test_recovery_notice_emits_acknowledgement(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_TIMER;
    pj_ui_time_projection_t projection = {0};
    projection.timer_remaining_ms = 30000;
    projection.recovery_time_uncertain = 1;
    pj_ui_set_time_projection(&ui, &projection);

    assert(pj_ui_handle_touch(&ui, 170, 16, PJ_TOUCH_TAP) == 1);
    pj_ui_time_command_t command;
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_RECOVERY_ACKNOWLEDGE);
}

static void test_time_controls_emit_explicit_commands(void)
{
    pj_ui_context_t ui;
    pj_ui_time_command_t command;
    pj_ui_init(&ui);

    ui.state = PJ_UI_STATE_STOPWATCH;
    ui.stopwatch_running = 1;
    ui.stopwatch_seconds = 42;
    assert(pj_ui_handle_touch(&ui, 170, 160, PJ_TOUCH_TAP) == 1);
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
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_TIMER_PAUSE);
    assert(pj_ui_handle_touch(&ui, 170, 160, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_TIMER_RESET);

    ui.state = PJ_UI_STATE_INTERVAL;
    ui.focus_index = 0;
    ui.interval_running = 0;
    ui.interval_seconds = 120;
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_INTERVAL_START);
    assert(command.duration_ms == 120000);
    assert(command.secondary_duration_ms == 300000);
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_INTERVAL_PAUSE);
    assert(pj_ui_handle_touch(&ui, 170, 160, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_INTERVAL_RESET);

    ui.state = PJ_UI_STATE_TIMER;
    ui.timer_seconds = 86400;
    assert(pj_ui_handle_touch(&ui, 100, 160, PJ_TOUCH_TAP) == 1);
    assert(ui.timer_seconds == 86400);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.duration_ms == 86400000);

    ui.state = PJ_UI_STATE_INTERVAL;
    ui.interval_seconds = 86400;
    assert(pj_ui_handle_touch(&ui, 100, 160, PJ_TOUCH_TAP) == 1);
    assert(ui.interval_seconds == 86400);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.duration_ms == 86400000);
}

static void test_alert_overlay_commands_keep_model_authoritative(void)
{
    pj_ui_context_t ui;
    pj_ui_time_command_t command;
    pj_framebuffer_t fb;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_SETTINGS;

    pj_ui_time_projection_t projection = time_projection_with_alert(61, PJ_TIME_ALERT_ALARM);
    pj_ui_set_time_projection(&ui, &projection);
    pj_ui_render(&ui, &fb);
    assert(count_black_pixels(&fb) > 0);

    assert(pj_ui_handle_touch(&ui, 40, 166, PJ_TOUCH_TAP) == 1);
    assert(ui.active_alert.id == 61);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_ALERT_DISMISS && command.alert_id == 61);
    assert(pj_ui_consume_time_command(&ui, &command) == 0);

    assert(pj_ui_handle_touch(&ui, 150, 166, PJ_TOUCH_TAP) == 1);
    assert(ui.active_alert.id == 61);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_ALARM_SNOOZE && command.alert_id == 61);

    projection = time_projection_with_alert(62, PJ_TIME_ALERT_INTERVAL);
    pj_ui_set_time_projection(&ui, &projection);
    assert(pj_ui_handle_touch(&ui, 150, 166, PJ_TOUCH_TAP) == 0);
    assert(pj_ui_consume_time_command(&ui, &command) == 0);
    assert(pj_ui_handle_touch(&ui, 40, 166, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_consume_time_command(&ui, &command) == 1);
    assert(command.type == PJ_UI_TIME_COMMAND_ALERT_DISMISS && command.alert_id == 62);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_SETTINGS);
}

static void test_sync_state_is_board_driven(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_SYNC;
    pj_ui_mark_displayed(&ui);

    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(ui.state == PJ_UI_STATE_SETTINGS);
    ui.state = PJ_UI_STATE_SYNC;
    pj_ui_mark_displayed(&ui);
    pj_ui_set_sync_state(&ui, 4, 2, 1);
    assert(ui.sync_pending == 4);
    assert(ui.sync_transferred == 2);
    assert(ui.sync_online == 1);
    assert(ui.dirty.partial == 1);

    pj_ui_mark_displayed(&ui);
    pj_ui_set_sync_state(&ui, -1, -2, 0);
    assert(ui.sync_pending == 0);
    assert(ui.sync_transferred == 0);
    assert(ui.sync_online == 0);
}

static void test_dirty_lifecycle(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);

    assert(pj_ui_is_dirty(&ui) == 1);
    assert(ui.dirty.partial == 0);

    pj_ui_set_status(&ui, 42, 21);
    assert(pj_ui_is_dirty(&ui) == 1);
    assert(ui.dirty.partial == 0);

    pj_ui_mark_displayed(&ui);
    assert(pj_ui_is_dirty(&ui) == 0);

    pj_ui_wake(&ui);
    pj_ui_mark_displayed(&ui);
    pj_ui_set_status(&ui, 43, 22);
    assert(pj_ui_is_dirty(&ui) == 1);
    assert(ui.dirty.partial == 1);
    assert(ui.dirty.y == 146);

    pj_ui_mark_displayed(&ui);
    pj_ui_set_time(&ui, 10, 42, 2026, 6, 6);
    assert(pj_ui_is_dirty(&ui) == 1);
    assert(ui.dirty.partial == 1);
    assert(ui.dirty.x == 0);
    assert(ui.dirty.y == 0);
    assert(ui.dirty.width == PJ_DISPLAY_WIDTH);
    assert(ui.dirty.height == 132);

    pj_ui_mark_displayed(&ui);
    pj_ui_set_time(&ui, 10, 43, 2026, 6, 7);
    assert(pj_ui_is_dirty(&ui) == 1);
    assert(ui.dirty.partial == 0);

    pj_ui_request_full_refresh(&ui);
    assert(pj_ui_is_dirty(&ui) == 1);
    assert(ui.dirty.partial == 0);
}

static void test_partial_render_preserves_outside_region(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t before;
    pj_framebuffer_t after;
    pj_ui_init(&ui);
    pj_ui_wake(&ui);
    pj_ui_render(&ui, &before);
    after = before;
    pj_ui_mark_displayed(&ui);

    pj_ui_set_status(&ui, 41, 23);
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
    assert(ui.dirty.x == 0 && ui.dirty.y == 32);
    assert(ui.dirty.width == PJ_DISPLAY_WIDTH);
    assert(ui.dirty.y + ui.dirty.height == PJ_DISPLAY_HEIGHT);
    pj_ui_render(&ui, &partial);

    expected_ui = ui;
    pj_ui_request_full_refresh(&expected_ui);
    pj_ui_render(&expected_ui, &expected);
    assert(memcmp(&partial, &expected, sizeof(expected)) == 0);
    assert(count_pixel_differences_in_region(
               &before, &partial, 0, 150, PJ_DISPLAY_WIDTH, 50) > 0);
}

static void test_back_affordance_is_visible_on_child_screens(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t fb;
    pj_ui_init(&ui);

    ui.state = PJ_UI_STATE_TIMER;
    pj_ui_render(&ui, &fb);
    assert(count_black_pixels_in_region(&fb, 8, 6, 24, 24) > 0);
    assert(pj_ui_handle_touch(&ui, 16, 16, PJ_TOUCH_TAP) == 1);
    assert(ui.state == PJ_UI_STATE_TIME);
}

static void test_static_art_render_and_fallback(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t fallback;
    pj_framebuffer_t custom;
    uint8_t pixels[PJ_FRAMEBUFFER_BYTES] = {0};
    pj_ui_init(&ui);

    pj_ui_render(&ui, &fallback);
    assert(count_black_pixels(&fallback) > 10);

    size_t first = (size_t)4 * PJ_DISPLAY_WIDTH + 3;
    size_t second = (size_t)199 * PJ_DISPLAY_WIDTH + 199;
    pixels[first >> 3u] |= (uint8_t)(1u << (first & 7u));
    pixels[second >> 3u] |= (uint8_t)(1u << (second & 7u));
    pj_ui_mark_displayed(&ui);
    pj_ui_set_static_art(&ui, pixels, sizeof(pixels));
    assert(ui.static_art_valid == 1);
    assert(pj_ui_is_dirty(&ui) == 1);
    assert(ui.dirty.partial == 0);

    pj_ui_render(&ui, &custom);
    assert(count_black_pixels(&custom) == 2);
    assert(pj_framebuffer_get(&custom, 3, 4) == 1);
    assert(pj_framebuffer_get(&custom, 199, 199) == 1);
    assert(pj_framebuffer_get(&custom, 100, 100) == 0);

    ui.dark_mode = 1;
    pj_ui_request_full_refresh(&ui);
    pj_ui_render(&ui, &custom);
    assert(count_black_pixels(&custom) == 2);
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
    assert_region_clear(&fb, 190, 39, 10, 32);
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
    assert(count_black_pixels_in_region(&fb, 8, 12, 184, 36) > 0);
    assert_region_clear(&fb, 0, 12, 8, 36);
    assert_region_clear(&fb, 192, 12, 8, 36);
}

static void test_long_home_title_is_ellipsized_inside_header(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t fb;
    pj_ui_init(&ui);
    strcpy(ui.home_layout.title, "My long personal journal");
    ui.state = PJ_UI_STATE_HOME;
    pj_ui_render(&ui, &fb);
    assert(count_black_pixels_in_region(&fb, 10, 6, 180, 24) > 0);
    for (int y = 6; y < 30; y++) {
        assert(pj_framebuffer_get(&fb, 0, y) == 0);
        assert(pj_framebuffer_get(&fb, 199, y) == 0);
    }
}

static void test_calendar_divider_stays_above_empty_state(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t fb;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_CALENDAR;
    pj_ui_render(&ui, &fb);

    for (int x = 12; x <= 188; x++) {
        assert(pj_framebuffer_get(&fb, x, 91) == 1);
    }
    assert_region_clear(&fb, 12, 92, 177, 14);
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
    test_note_paging_tracks_hardware_focus_and_swipes();
    test_read_opens_transcript_detail_without_playback();
    test_note_detail_play_control_has_touch_parity();
    test_custom_home_slots_render_and_route_in_order();
    test_compiled_home_fallback_routes_every_slot();
    test_every_supported_home_destination_routes();
    test_home_layout_setter_canonicalizes_inactive_slots();
    test_aux_focus_and_activation();
    test_time_value_typography_fits_refresh_regions();
    test_aux_double_cycles_visible_actions_and_falls_back_to_record();
    test_audio_lifecycle_reconciliation();
    test_settings_dark_mode_toggle();
    test_settings_volume_opens_dedicated_screen();
    test_volume_only_bottom_controls_adjust_and_clamp();
    test_timer_presets_are_not_runtime_counters();
    test_time_projection_and_alert_repaint();
    test_active_alert_is_controllable_from_aux();
    test_sleep_preserves_durable_time_activity();
    test_recovery_notice_emits_acknowledgement();
    test_time_controls_emit_explicit_commands();
    test_alert_overlay_commands_keep_model_authoritative();
    test_sync_state_is_board_driven();
    test_dirty_lifecycle();
    test_partial_render_preserves_outside_region();
    test_record_partial_render_replaces_status_through_bottom_edge();
    test_back_affordance_is_visible_on_child_screens();
    test_static_art_render_and_fallback();
    test_long_note_label_stays_inside_editorial_row();
    test_note_detail_title_respects_side_margins();
    test_long_home_title_is_ellipsized_inside_header();
    test_calendar_divider_stays_above_empty_state();
    test_polished_scenes_render_stably();
    test_render_all_states();
    puts("ui tests passed");
    return 0;
}
