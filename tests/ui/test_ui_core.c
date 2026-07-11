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

    assert(pj_ui_handle_touch(&ui, 20, 20, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_NOTES);

    assert(pj_ui_handle_touch(&ui, 20, 120, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_LISTEN);

    assert(pj_ui_handle_touch(&ui, 20, 20, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_NOTE_DETAIL);

    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);

    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIME_TEMP);

    ui.state = PJ_UI_STATE_NOTES;
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);

    ui.state = PJ_UI_STATE_TIMER;
    assert(pj_ui_handle_aux_long(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);
}

static void test_empty_notes_do_not_open_detail(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_READ;
    assert(pj_ui_handle_touch(&ui, 20, 20, PJ_TOUCH_TAP) == 0);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_READ);
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
    assert(pj_ui_handle_touch(&ui, 120, 40, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_READ);
    ui.state = PJ_UI_STATE_HOME;
    assert(pj_ui_handle_touch(&ui, 20, 130, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIMER);
    ui.state = PJ_UI_STATE_HOME;
    assert(pj_ui_handle_touch(&ui, 120, 130, PJ_TOUCH_TAP) == 1);
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
    assert(pj_ui_handle_touch(&ui, 120, 40, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIME);
    ui.state = PJ_UI_STATE_HOME;
    assert(pj_ui_handle_touch(&ui, 20, 130, PJ_TOUCH_TAP) == 1);
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

static void test_aux_primary_actions(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_NOTES;

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
    assert(pj_ui_handle_aux_short(&ui) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_STOPWATCH);
    assert(ui.stopwatch_running == 1);
}

static void test_aux_double_click_routing(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);

    for (int state = PJ_UI_STATE_STATIC; state < PJ_UI_STATE_COUNT; state++) {
        if (state == PJ_UI_STATE_RECORD) {
            continue;
        }
        ui.state = (pj_ui_state_t)state;
        ui.record_state = PJ_RECORD_IDLE;
        ui.playback_state = PJ_PLAYBACK_IDLE;
        assert(pj_ui_handle_aux_double(&ui) == 1);
        assert(pj_ui_current_state(&ui) == PJ_UI_STATE_RECORD);
        assert(ui.record_state == PJ_RECORD_ACTIVE);
    }

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
    assert(pj_ui_handle_aux_double(&ui) == 0);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_STOPWATCH);

    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_TIMER;
    ui.timer_running = 1;
    assert(pj_ui_handle_aux_double(&ui) == 0);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIMER);

    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_INTERVAL;
    ui.interval_running = 1;
    assert(pj_ui_handle_aux_double(&ui) == 0);
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
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);

    ui.state = PJ_UI_STATE_LISTEN;
    ui.playback_state = PJ_PLAYBACK_ACTIVE;
    pj_ui_set_audio_state(&ui, 0, 0);
    assert(ui.playback_state == PJ_PLAYBACK_IDLE);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_LISTEN);
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

static void test_sync_state_is_board_driven(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    ui.state = PJ_UI_STATE_SYNC;
    pj_ui_mark_displayed(&ui);

    assert(pj_ui_handle_aux_short(&ui) == 0);
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

static void test_no_back_button_pixels(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t fb;
    pj_ui_init(&ui);

    for (int state = 0; state < PJ_UI_STATE_COUNT; state++) {
        ui.state = (pj_ui_state_t)state;
        pj_ui_render(&ui, &fb);
        assert(pj_framebuffer_get(&fb, 4, 4) == 0);
    }
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
    test_custom_home_slots_render_and_route_in_order();
    test_compiled_home_fallback_routes_every_slot();
    test_every_supported_home_destination_routes();
    test_home_layout_setter_canonicalizes_inactive_slots();
    test_aux_primary_actions();
    test_aux_double_click_routing();
    test_audio_lifecycle_reconciliation();
    test_settings_dark_mode_toggle();
    test_timer_presets_are_not_runtime_counters();
    test_sync_state_is_board_driven();
    test_dirty_lifecycle();
    test_partial_render_preserves_outside_region();
    test_no_back_button_pixels();
    test_static_art_render_and_fallback();
    test_render_all_states();
    puts("ui tests passed");
    return 0;
}
