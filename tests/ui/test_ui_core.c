#include "pj_ui.h"
#include "pj_ui_presenter.h"
#include "pj_layout_geometry.h"
#include "pj_default_static_art.h"
#include "pj_glyph_carbon.h"
#include "pj_icon_carbon.h"
#include "pj_font_ibm_plex_mono_bold.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_LEN(values) (sizeof(values) / sizeof((values)[0]))

typedef struct {
    pj_ui_presenter_t presenter;
    pj_framebuffer_t accepted;
} presenter_fixture_t;

static void framebuffer_put(pj_framebuffer_t *framebuffer, int x, int y, int black)
{
    size_t index = (size_t)y * PJ_DISPLAY_WIDTH + (size_t)x;
    uint8_t mask = (uint8_t)(1u << (index & 7u));
    if (black) {
        framebuffer->pixels[index >> 3u] |= mask;
    } else {
        framebuffer->pixels[index >> 3u] &= (uint8_t)~mask;
    }
}

static int count_black_pixels(const pj_framebuffer_t *framebuffer)
{
    int count = 0;
    for (int y = 0; y < PJ_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
            count += pj_framebuffer_get(framebuffer, x, y);
        }
    }
    return count;
}

static int count_black_pixels_in_region(const pj_framebuffer_t *framebuffer,
                                        int x, int y, int width, int height)
{
    int count = 0;
    for (int row = y; row < y + height; row++) {
        for (int column = x; column < x + width; column++) {
            count += pj_framebuffer_get(framebuffer, column, row);
        }
    }
    return count;
}

static int framebuffer_differences(const pj_framebuffer_t *left,
                                   const pj_framebuffer_t *right)
{
    int count = 0;
    for (int y = 0; y < PJ_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
            count += pj_framebuffer_get(left, x, y) !=
                pj_framebuffer_get(right, x, y);
        }
    }
    return count;
}

static void assert_framebuffer_region_equal(
    const pj_framebuffer_t *left, const pj_framebuffer_t *right,
    int x, int y, int width, int height)
{
    for (int row = y; row < y + height; row++) {
        for (int column = x; column < x + width; column++) {
            assert(pj_framebuffer_get(left, column, row) ==
                   pj_framebuffer_get(right, column, row));
        }
    }
}

static pj_ui_dirty_region_t independent_changed_bounds(
    const pj_framebuffer_t *before, const pj_framebuffer_t *after)
{
    int min_x = PJ_DISPLAY_WIDTH;
    int min_y = PJ_DISPLAY_HEIGHT;
    int max_x = -1;
    int max_y = -1;
    for (int y = 0; y < PJ_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
            if (pj_framebuffer_get(before, x, y) ==
                pj_framebuffer_get(after, x, y)) {
                continue;
            }
            if (x < min_x) min_x = x;
            if (y < min_y) min_y = y;
            if (x > max_x) max_x = x;
            if (y > max_y) max_y = y;
        }
    }
    if (max_x < min_x) {
        return (pj_ui_dirty_region_t) {0};
    }
    return (pj_ui_dirty_region_t) {
        .x = min_x,
        .y = min_y,
        .width = max_x - min_x + 1,
        .height = max_y - min_y + 1,
        .partial = 1,
    };
}

static void assert_regions_equal(pj_ui_dirty_region_t actual,
                                 pj_ui_dirty_region_t expected)
{
    assert(actual.x == expected.x);
    assert(actual.y == expected.y);
    assert(actual.width == expected.width);
    assert(actual.height == expected.height);
    assert(actual.partial == expected.partial);
}

static void apply_region(pj_framebuffer_t *destination,
                         const pj_framebuffer_t *source,
                         pj_ui_dirty_region_t dirty)
{
    for (int y = dirty.y; y < dirty.y + dirty.height; y++) {
        for (int x = dirty.x; x < dirty.x + dirty.width; x++) {
            framebuffer_put(destination, x, y,
                            pj_framebuffer_get(source, x, y));
        }
    }
}

static void presenter_start(presenter_fixture_t *fixture,
                            const pj_ui_context_t *context)
{
    pj_ui_presenter_frame_t frame;
    pj_ui_presenter_init(&fixture->presenter);
    pj_ui_presenter_revision_t revision = pj_ui_presenter_revision(context);
    assert(pj_ui_presenter_prepare(&fixture->presenter, context, &revision,
                                   &frame) == PJ_UI_FRAME_FULL);
    assert(frame.result == PJ_UI_FRAME_FULL);
    assert(frame.dirty.x == 0 && frame.dirty.y == 0);
    assert(frame.dirty.width == PJ_DISPLAY_WIDTH);
    assert(frame.dirty.height == PJ_DISPLAY_HEIGHT);
    assert(frame.dirty.partial == 0);
    fixture->accepted = *frame.framebuffer;
    assert(pj_ui_presenter_accept(&fixture->presenter, frame.token));
}

static pj_ui_dirty_region_t presenter_accept_partial(
    presenter_fixture_t *fixture, const pj_ui_context_t *context)
{
    pj_ui_presenter_frame_t frame;
    pj_framebuffer_t fresh;
    pj_framebuffer_t reconstructed = fixture->accepted;
    pj_ui_presenter_revision_t revision = pj_ui_presenter_revision(context);
    assert(pj_ui_presenter_prepare(&fixture->presenter, context, &revision,
                                   &frame) == PJ_UI_FRAME_PARTIAL);
    pj_ui_compose_frame(context, &fresh);
    assert(memcmp(frame.framebuffer, &fresh, sizeof(fresh)) == 0);
    pj_ui_dirty_region_t expected = independent_changed_bounds(
        &fixture->accepted, &fresh);
    assert(expected.width > 0 && expected.height > 0);
    assert_regions_equal(frame.dirty, expected);
    apply_region(&reconstructed, frame.framebuffer, frame.dirty);
    assert(memcmp(&reconstructed, &fresh, sizeof(fresh)) == 0);
    pj_ui_dirty_region_t dirty = frame.dirty;
    assert(pj_ui_presenter_accept(&fixture->presenter, frame.token));
    fixture->accepted = fresh;
    return dirty;
}

static void assert_presenter_full(presenter_fixture_t *fixture,
                                  const pj_ui_context_t *context)
{
    pj_ui_presenter_frame_t frame;
    pj_framebuffer_t fresh;
    pj_ui_presenter_revision_t revision = pj_ui_presenter_revision(context);
    assert(pj_ui_presenter_prepare(&fixture->presenter, context, &revision,
                                   &frame) == PJ_UI_FRAME_FULL);
    assert(frame.dirty.x == 0 && frame.dirty.y == 0);
    assert(frame.dirty.width == PJ_DISPLAY_WIDTH);
    assert(frame.dirty.height == PJ_DISPLAY_HEIGHT);
    assert(frame.dirty.partial == 0);
    pj_ui_compose_frame(context, &fresh);
    assert(memcmp(frame.framebuffer, &fresh, sizeof(fresh)) == 0);
    assert(pj_ui_presenter_accept(&fixture->presenter, frame.token));
    fixture->accepted = fresh;
}

static pj_ui_preferences_t current_preferences(const pj_ui_context_t *context)
{
    return (pj_ui_preferences_t) {
        .volume = context->volume,
        .dark_mode = context->dark_mode,
        .alarm_enabled = context->alarm_on,
        .alarm_hour = context->alarm_hour,
        .alarm_minute = context->alarm_minute,
        .timer_seconds = context->timer_preset_seconds,
        .interval_seconds = context->interval_preset_seconds,
        .clock_24h = context->clock_24h,
        .temperature_fahrenheit = context->temperature_fahrenheit,
        .transcript_font_size = context->transcript_font_size,
    };
}

static const pj_layout_slot_t *find_slot(pj_layout_id_t layout_id,
                                         pj_layout_slot_id_t slot_id)
{
    const pj_layout_geometry_t *geometry = pj_layout_geometry(layout_id);
    assert(geometry != NULL);
    for (uint8_t index = 0; index < geometry->slot_count; index++) {
        if (geometry->slots[index].id == slot_id) {
            return &geometry->slots[index];
        }
    }
    assert(!"missing layout slot");
    return NULL;
}

static int slot_x(pj_layout_id_t layout_id, pj_layout_slot_id_t slot_id)
{
    const pj_layout_slot_t *slot = find_slot(layout_id, slot_id);
    return (slot->icon_center.x + PJ_LAYOUT_COORD_SCALE / 2) /
        PJ_LAYOUT_COORD_SCALE;
}

static int slot_y(pj_layout_id_t layout_id, pj_layout_slot_id_t slot_id)
{
    const pj_layout_slot_t *slot = find_slot(layout_id, slot_id);
    return (slot->icon_center.y + PJ_LAYOUT_COORD_SCALE / 2) /
        PJ_LAYOUT_COORD_SCALE;
}

static int tap_slot(pj_ui_context_t *context, pj_layout_id_t layout_id,
                    pj_layout_slot_id_t slot_id)
{
    return pj_ui_handle_touch(context, slot_x(layout_id, slot_id),
                              slot_y(layout_id, slot_id), PJ_TOUCH_TAP);
}

static void seed_notes(pj_ui_context_t *context, int count)
{
    const char labels[PJ_UI_MAX_NOTES][PJ_UI_NOTE_LABEL_LEN] = {
        "REC 20260717 0909 1", "Mixed Case: Alpha!", "Third / note?",
        "Fourth note", "Fifth note", "Sixth note", "Seventh note",
        "Eighth note", "Ninth note", "Tenth note", "Eleventh note",
        "Twelfth note",
    };
    pj_ui_set_notes(context, count, labels);
}

static void navigate_home_to(pj_ui_context_t *context,
                             pj_layout_slot_id_t slot)
{
    pj_ui_init(context);
    pj_ui_wake(context);
    assert(tap_slot(context, PJ_LAYOUT_HOME_3_1, slot));
}

static int asset_pixel(const pj_asset_bitmap_t *asset, int x, int y)
{
    uint8_t byte = asset->data[(size_t)y * asset->stride + (size_t)x / 8u];
    return (byte & (uint8_t)(0x80u >> (x % 8))) != 0;
}

static void assert_asset_region(const pj_framebuffer_t *framebuffer,
                                const pj_asset_bitmap_t *asset, int center_x,
                                int center_y)
{
    assert(asset != NULL);
    int left = center_x - asset->width / 2;
    int top = center_y - asset->height / 2;
    for (int y = 0; y < asset->height; y++) {
        for (int x = 0; x < asset->width; x++) {
            assert(pj_framebuffer_get(framebuffer, left + x, top + y) ==
                   asset_pixel(asset, x, y));
        }
    }
}

static void assert_frame_is_asset(const pj_framebuffer_t *framebuffer,
                                  const pj_asset_bitmap_t *asset, int center_x,
                                  int center_y)
{
    int left = center_x - asset->width / 2;
    int top = center_y - asset->height / 2;
    for (int y = 0; y < PJ_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
            int expected = x >= left && x < left + asset->width &&
                y >= top && y < top + asset->height ?
                asset_pixel(asset, x - left, y - top) : 0;
            assert(pj_framebuffer_get(framebuffer, x, y) == expected);
        }
    }
}

static int glyph_pixel(const pj_asset_glyph_t *glyph, int x, int y)
{
    uint8_t byte = glyph->data[(size_t)y * glyph->stride + (size_t)x / 8u];
    return (byte & (uint8_t)(0x80u >> (x % 8))) != 0;
}

static void assert_glyph_region(const pj_framebuffer_t *framebuffer,
                                const pj_asset_glyph_t *glyph, int center_x,
                                int center_y)
{
    assert(glyph != NULL);
    int left = center_x - glyph->width / 2 + glyph->x_offset;
    int top = center_y - glyph->height / 2 + glyph->y_offset;
    for (int y = 0; y < glyph->height; y++) {
        for (int x = 0; x < glyph->width; x++) {
            assert(pj_framebuffer_get(framebuffer, left + x, top + y) ==
                   glyph_pixel(glyph, x, y));
        }
    }
}

static void consume_expected_command(pj_ui_context_t *context,
                                     pj_ui_time_command_type_t type,
                                     uint64_t duration_ms,
                                     uint64_t secondary_duration_ms)
{
    pj_ui_time_command_t command;
    assert(pj_ui_consume_time_command(context, &command));
    assert(command.type == type);
    assert(command.duration_ms == duration_ms);
    assert(command.secondary_duration_ms == secondary_duration_ms);
    assert(!pj_ui_consume_time_command(context, &command));
}

static pj_ui_time_projection_t time_projection(int stopwatch_seconds,
                                               int timer_seconds,
                                               int interval_seconds)
{
    pj_ui_time_projection_t projection;
    memset(&projection, 0, sizeof(projection));
    projection.alarm_hour = 7;
    projection.alarm_minute = 30;
    projection.stopwatch_elapsed_ms = (uint64_t)stopwatch_seconds * 1000u;
    projection.timer_remaining_ms = (uint64_t)timer_seconds * 1000u;
    projection.interval_remaining_ms = (uint64_t)interval_seconds * 1000u;
    projection.interval_phase = 1;
    return projection;
}

static void test_geometry_is_exhaustive_and_navigation_is_fixed(void)
{
    static const struct {
        pj_layout_id_t layout;
        pj_layout_slot_id_t first;
        pj_layout_slot_id_t last;
    } layouts[] = {
        {PJ_LAYOUT_HOME_3_1, PJ_LAYOUT_SLOT_HOME_TIME,
         PJ_LAYOUT_SLOT_HOME_SETTINGS},
        {PJ_LAYOUT_NOTES_3_1M, PJ_LAYOUT_SLOT_NOTES_RECORD,
         PJ_LAYOUT_SLOT_NOTES_READ},
        {PJ_LAYOUT_TIME_4_1, PJ_LAYOUT_SLOT_TIME_ALARM,
         PJ_LAYOUT_SLOT_TIME_INTERVAL},
        {PJ_LAYOUT_SETTINGS_4_0M, PJ_LAYOUT_SLOT_SETTINGS_VOLUME,
         PJ_LAYOUT_SLOT_SETTINGS_SYNC},
    };

    for (size_t layout_index = 0; layout_index < ARRAY_LEN(layouts);
         layout_index++) {
        const pj_layout_geometry_t *geometry =
            pj_layout_geometry(layouts[layout_index].layout);
        assert(geometry != NULL);
        assert(geometry->rule_count ==
               (layouts[layout_index].layout < PJ_LAYOUT_TIME_4_1 ? 3 : 5));
        assert(!pj_layout_pixel_is_rule(layouts[layout_index].layout, 0, 0));
        assert(!pj_layout_pixel_is_rule(layouts[layout_index].layout, 199, 0));
        assert(!pj_layout_pixel_is_rule(layouts[layout_index].layout, 0, 199));
        assert(!pj_layout_pixel_is_rule(layouts[layout_index].layout, 199, 199));
        assert(pj_layout_pixel_is_rule(layouts[layout_index].layout, 100, 100));

        for (int y = 0; y < PJ_DISPLAY_HEIGHT; y++) {
            for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
                pj_layout_slot_id_t slot = pj_layout_hit_test(
                    layouts[layout_index].layout, (uint16_t)x, (uint16_t)y);
                assert(slot >= layouts[layout_index].first);
                assert(slot <= layouts[layout_index].last);
            }
        }
        for (uint8_t slot_index = 0; slot_index < geometry->slot_count;
             slot_index++) {
            const pj_layout_slot_t *slot = &geometry->slots[slot_index];
            assert(pj_layout_hit_test(
                       layouts[layout_index].layout,
                       (uint16_t)slot_x(layouts[layout_index].layout, slot->id),
                       (uint16_t)slot_y(layouts[layout_index].layout, slot->id)) ==
                   slot->id);
        }
    }
    assert(pj_layout_geometry(PJ_LAYOUT_COUNT) == NULL);
    assert(pj_layout_hit_test(PJ_LAYOUT_HOME_3_1, 200, 0) ==
           PJ_LAYOUT_SLOT_NONE);

    static const struct {
        pj_layout_slot_id_t slot;
        pj_ui_state_t state;
    } home_targets[] = {
        {PJ_LAYOUT_SLOT_HOME_TIME, PJ_UI_STATE_TIME},
        {PJ_LAYOUT_SLOT_HOME_NOTES, PJ_UI_STATE_NOTES},
        {PJ_LAYOUT_SLOT_HOME_SETTINGS, PJ_UI_STATE_SETTINGS},
    };
    for (size_t i = 0; i < ARRAY_LEN(home_targets); i++) {
        pj_ui_context_t context;
        pj_ui_init(&context);
        pj_ui_wake(&context);
        assert(tap_slot(&context, PJ_LAYOUT_HOME_3_1, home_targets[i].slot));
        assert(context.state == home_targets[i].state);
    }

    static const struct {
        pj_layout_slot_id_t slot;
        pj_ui_state_t state;
    } note_targets[] = {
        {PJ_LAYOUT_SLOT_NOTES_RECORD, PJ_UI_STATE_RECORD},
        {PJ_LAYOUT_SLOT_NOTES_LISTEN, PJ_UI_STATE_LISTEN},
        {PJ_LAYOUT_SLOT_NOTES_READ, PJ_UI_STATE_READ},
    };
    for (size_t i = 0; i < ARRAY_LEN(note_targets); i++) {
        pj_ui_context_t context;
        navigate_home_to(&context, PJ_LAYOUT_SLOT_HOME_NOTES);
        assert(tap_slot(&context, PJ_LAYOUT_NOTES_3_1M,
                        note_targets[i].slot));
        assert(context.state == note_targets[i].state);
        if (context.state == PJ_UI_STATE_RECORD) {
            assert(context.record_state == PJ_RECORD_ARMING);
        }
    }

    static const struct {
        pj_layout_slot_id_t slot;
        pj_ui_state_t state;
    } time_targets[] = {
        {PJ_LAYOUT_SLOT_TIME_ALARM, PJ_UI_STATE_ALARM},
        {PJ_LAYOUT_SLOT_TIME_STOPWATCH, PJ_UI_STATE_STOPWATCH},
        {PJ_LAYOUT_SLOT_TIME_TIMER, PJ_UI_STATE_TIMER},
        {PJ_LAYOUT_SLOT_TIME_INTERVAL, PJ_UI_STATE_INTERVAL},
    };
    for (size_t i = 0; i < ARRAY_LEN(time_targets); i++) {
        pj_ui_context_t context;
        navigate_home_to(&context, PJ_LAYOUT_SLOT_HOME_TIME);
        assert(tap_slot(&context, PJ_LAYOUT_TIME_4_1,
                        time_targets[i].slot));
        assert(context.state == time_targets[i].state);
    }
}

static void test_state_metadata_and_deterministic_composition(void)
{
    static const pj_ui_state_t parents[PJ_UI_STATE_COUNT] = {
        [PJ_UI_STATE_STATIC] = PJ_UI_STATE_STATIC,
        [PJ_UI_STATE_TIME_TEMP] = PJ_UI_STATE_STATIC,
        [PJ_UI_STATE_HOME] = PJ_UI_STATE_TIME_TEMP,
        [PJ_UI_STATE_NOTES] = PJ_UI_STATE_HOME,
        [PJ_UI_STATE_RECORD] = PJ_UI_STATE_NOTES,
        [PJ_UI_STATE_LISTEN] = PJ_UI_STATE_NOTES,
        [PJ_UI_STATE_READ] = PJ_UI_STATE_NOTES,
        [PJ_UI_STATE_TIME] = PJ_UI_STATE_HOME,
        [PJ_UI_STATE_ALARM] = PJ_UI_STATE_TIME,
        [PJ_UI_STATE_STOPWATCH] = PJ_UI_STATE_TIME,
        [PJ_UI_STATE_TIMER] = PJ_UI_STATE_TIME,
        [PJ_UI_STATE_INTERVAL] = PJ_UI_STATE_TIME,
        [PJ_UI_STATE_SETTINGS] = PJ_UI_STATE_HOME,
        [PJ_UI_STATE_SYNC] = PJ_UI_STATE_SETTINGS,
        [PJ_UI_STATE_VOLUME] = PJ_UI_STATE_SETTINGS,
        [PJ_UI_STATE_NOTE_DETAIL] = PJ_UI_STATE_LISTEN,
    };
    pj_ui_context_t context;
    pj_ui_init(&context);
    seed_notes(&context, PJ_UI_MAX_NOTES);
    context.selected_note = 1;
    context.record_state = PJ_RECORD_ACTIVE;
    context.recording_seconds = 59;

    for (int state = 0; state < PJ_UI_STATE_COUNT; state++) {
        pj_framebuffer_t first;
        pj_framebuffer_t second;
        memset(&first, 0xa5, sizeof(first));
        memset(&second, 0x5a, sizeof(second));
        context.state = (pj_ui_state_t)state;
        context.note_detail_transcript = state == PJ_UI_STATE_NOTE_DETAIL;
        pj_ui_compose_frame(&context, &first);
        pj_ui_compose_frame(&context, &second);
        assert(memcmp(&first, &second, sizeof(first)) == 0);
        assert(count_black_pixels(&first) > 0);
        assert(strcmp(pj_ui_state_name((pj_ui_state_t)state), "unknown") != 0);
        assert(pj_ui_parent_state((pj_ui_state_t)state) == parents[state]);
    }
    assert(strcmp(pj_ui_state_name(PJ_UI_STATE_COUNT), "unknown") == 0);
    assert(pj_ui_parent_state(PJ_UI_STATE_COUNT) == PJ_UI_STATE_STATIC);
    assert(strstr(pj_ui_default_font_name(), "Carbon") != NULL);

    context.state = PJ_UI_STATE_STATIC;
    pj_framebuffer_t splash;
    pj_ui_compose_frame(&context, &splash);
    assert(memcmp(splash.pixels, pj_default_static_art,
                  sizeof(splash.pixels)) == 0);
}

static void test_record_arming_and_back_contract(void)
{
    pj_ui_context_t context;
    navigate_home_to(&context, PJ_LAYOUT_SLOT_HOME_NOTES);
    assert(tap_slot(&context, PJ_LAYOUT_NOTES_3_1M,
                    PJ_LAYOUT_SLOT_NOTES_RECORD));
    assert(context.state == PJ_UI_STATE_RECORD);
    assert(context.record_state == PJ_RECORD_ARMING);
    assert(context.recording_seconds == 0);
    pj_ui_set_recording_elapsed(&context, 5000);
    assert(context.recording_seconds == 0);

    pj_framebuffer_t arming;
    pj_ui_compose_frame(&context, &arming);
    assert(count_black_pixels(&arming) > 0);
    pj_ui_set_audio_state(&context, 0, 0);
    assert(context.record_state == PJ_RECORD_ARMING);
    assert(context.state == PJ_UI_STATE_RECORD);
    assert(pj_ui_handle_aux_long(&context));
    assert(context.record_state == PJ_RECORD_IDLE);
    assert(context.state == PJ_UI_STATE_HOME);

    assert(pj_ui_handle_aux_double(&context));
    assert(context.state == PJ_UI_STATE_RECORD);
    assert(context.record_state == PJ_RECORD_ARMING);
    pj_ui_set_audio_state(&context, 1, 0);
    assert(context.record_state == PJ_RECORD_ACTIVE);
    pj_ui_set_recording_elapsed(&context, 59999);
    assert(context.recording_seconds == 59);
    pj_ui_set_recording_elapsed(&context, 60000);
    assert(context.recording_seconds == 60);
    pj_ui_set_recording_elapsed(&context, 59000);
    assert(context.recording_seconds == 60);
    assert(pj_ui_handle_aux_short(&context));
    assert(context.state == PJ_UI_STATE_HOME);
    assert(context.record_state == PJ_RECORD_STOPPING);
    pj_ui_set_audio_state(&context, 0, 0);
    assert(context.record_state == PJ_RECORD_IDLE);
    assert(context.recording_seconds == 0);
    pj_ui_set_recording_elapsed(&context, 60000);
    assert(context.recording_seconds == 0);
}

static void test_record_sleep_waits_for_audio_ack_before_reentry(void)
{
    pj_ui_context_t context;
    navigate_home_to(&context, PJ_LAYOUT_SLOT_HOME_NOTES);
    assert(tap_slot(&context, PJ_LAYOUT_NOTES_3_1M,
                    PJ_LAYOUT_SLOT_NOTES_RECORD));
    pj_ui_set_audio_state(&context, 1, 0);
    pj_ui_set_recording_elapsed(&context, 6000);
    assert(context.record_state == PJ_RECORD_ACTIVE);
    assert(context.recording_seconds == 6);

    pj_ui_sleep(&context);
    assert(context.state == PJ_UI_STATE_STATIC);
    assert(context.record_state == PJ_RECORD_STOPPING);
    pj_ui_wake(&context);
    assert(context.state == PJ_UI_STATE_HOME);
    assert(context.record_state == PJ_RECORD_STOPPING);
    assert(!pj_ui_handle_aux_double(&context));

    assert(tap_slot(&context, PJ_LAYOUT_HOME_3_1,
                    PJ_LAYOUT_SLOT_HOME_NOTES));
    assert(!tap_slot(&context, PJ_LAYOUT_NOTES_3_1M,
                     PJ_LAYOUT_SLOT_NOTES_RECORD));
    assert(context.state == PJ_UI_STATE_NOTES);

    uint32_t stopping_interaction = context.interaction_generation;
    pj_ui_set_audio_state(&context, 0, 0);
    assert(context.record_state == PJ_RECORD_IDLE);
    assert(context.recording_seconds == 0);
    assert(context.interaction_generation != stopping_interaction);
    assert(tap_slot(&context, PJ_LAYOUT_NOTES_3_1M,
                    PJ_LAYOUT_SLOT_NOTES_RECORD));
    assert(context.record_state == PJ_RECORD_ARMING);
    assert(context.recording_seconds == 0);
}

static void test_playback_uses_only_compact_play_and_pause(void)
{
    pj_ui_context_t context;
    navigate_home_to(&context, PJ_LAYOUT_SLOT_HOME_NOTES);
    seed_notes(&context, 3);
    assert(tap_slot(&context, PJ_LAYOUT_NOTES_3_1M,
                    PJ_LAYOUT_SLOT_NOTES_LISTEN));
    assert(pj_ui_handle_touch(&context, 80, 25, PJ_TOUCH_TAP));
    assert(context.state == PJ_UI_STATE_NOTE_DETAIL);
    assert(!context.note_detail_transcript);

    const pj_asset_bitmap_t *play = pj_carbon_icon_lookup(
        PJ_CARBON_ICON_PLAY_FILLED, 96);
    const pj_asset_bitmap_t *pause = pj_carbon_icon_lookup(
        PJ_CARBON_ICON_PAUSE_FILLED, 96);
    assert(play != NULL && pause != NULL);
    assert(pj_carbon_icon_lookup(PJ_CARBON_ICON_PLAY_FILLED, 64) == NULL);
    assert(pj_carbon_icon_lookup(PJ_CARBON_ICON_PLAY_FILLED, 144) == NULL);

    pj_framebuffer_t play_frame;
    pj_framebuffer_t pause_frame;
    pj_ui_compose_frame(&context, &play_frame);
    assert_frame_is_asset(&play_frame, play, 100, 100);

    pj_ui_set_audio_state(&context, 0, 1);
    assert(context.playback_state == PJ_PLAYBACK_ACTIVE);
    pj_ui_compose_frame(&context, &pause_frame);
    assert_frame_is_asset(&pause_frame, pause, 100, 100);
    assert(framebuffer_differences(&play_frame, &pause_frame) > 0);

    assert(pj_ui_handle_aux_long(&context));
    assert(context.state == PJ_UI_STATE_LISTEN);
    assert(context.playback_state == PJ_PLAYBACK_STOPPING);
    assert(context.playback_exit_pending);
    pj_ui_set_audio_state(&context, 0, 0);
    assert(context.state == PJ_UI_STATE_LISTEN);
    assert(context.playback_state == PJ_PLAYBACK_IDLE);
    assert(!context.playback_exit_pending);

    assert(pj_ui_handle_touch(&context, 80, 25, PJ_TOUCH_TAP));
    assert(context.state == PJ_UI_STATE_NOTE_DETAIL);
    assert(pj_ui_handle_touch(&context, 100, 100, PJ_TOUCH_TAP));
    assert(context.playback_state == PJ_PLAYBACK_ACTIVE);
    assert(pj_ui_handle_touch(&context, 100, 100, PJ_TOUCH_TAP));
    assert(context.playback_state == PJ_PLAYBACK_STOPPING);
    assert(!pj_ui_handle_touch(&context, 100, 100, PJ_TOUCH_TAP));
}

static void test_case_punctuation_and_long_transcript(void)
{
    static const char punctuation[] =
        " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
    const int sizes[] = {16, 24, 32, 64};
    for (size_t size_index = 0; size_index < ARRAY_LEN(sizes); size_index++) {
        for (size_t index = 0; punctuation[index] != '\0'; index++) {
            assert(pj_ibm_plex_punctuation_lookup(
                       (uint8_t)punctuation[index],
                       (uint16_t)sizes[size_index]) != NULL);
        }
    }
    const pj_asset_glyph_t *upper =
        pj_carbon_glyph_lookup_codepoint('A', 32);
    const pj_asset_glyph_t *lower =
        pj_carbon_glyph_lookup_codepoint('a', 32);
    assert(upper != NULL && lower != NULL);
    assert(upper->width != lower->width || upper->height != lower->height ||
           memcmp(upper->data, lower->data,
                  (size_t)upper->stride * upper->height) != 0);
    assert(pj_carbon_glyph_lookup_codepoint('?', 32) == NULL);

    const char mixed[][PJ_UI_NOTE_LABEL_LEN] = {
        "Aa Ii Ll Tt Rr Ff Mm Ww Jj ygj pq!?.,:-/% Mixed Case",
    };
    const char upper_only[][PJ_UI_NOTE_LABEL_LEN] = {
        "AA II LL TT RR FF MM WW JJ YGJ PQ!?.,:-/% MIXED CASE",
    };
    pj_ui_context_t context;
    pj_ui_init(&context);
    context.state = PJ_UI_STATE_NOTE_DETAIL;
    context.note_detail_transcript = 1;
    context.selected_note = 0;
    pj_ui_set_notes(&context, 1, mixed);
    pj_framebuffer_t first;
    pj_framebuffer_t repeat;
    pj_framebuffer_t uppercase;
    pj_ui_compose_frame(&context, &first);
    pj_ui_compose_frame(&context, &repeat);
    assert(memcmp(&first, &repeat, sizeof(first)) == 0);
    assert(count_black_pixels(&first) > 100);
    pj_ui_set_notes(&context, 1, upper_only);
    pj_ui_compose_frame(&context, &uppercase);
    assert(framebuffer_differences(&first, &uppercase) > 0);
}

static void test_note_pages_selection_and_content_partials(void)
{
    pj_ui_context_t context;
    navigate_home_to(&context, PJ_LAYOUT_SLOT_HOME_NOTES);
    seed_notes(&context, 7);
    assert(tap_slot(&context, PJ_LAYOUT_NOTES_3_1M,
                    PJ_LAYOUT_SLOT_NOTES_READ));
    assert(context.state == PJ_UI_STATE_READ);
    assert(context.note_page == 0);
    presenter_fixture_t fixture;
    presenter_start(&fixture, &context);

    assert(pj_ui_handle_touch(&context, 100, 100, PJ_TOUCH_SWIPE_LEFT));
    assert(context.note_page == 1);
    presenter_accept_partial(&fixture, &context);
    assert(pj_ui_handle_touch(&context, 175, 175, PJ_TOUCH_TAP));
    assert(context.note_page == 2);
    presenter_accept_partial(&fixture, &context);
    assert(!pj_ui_handle_touch(&context, 100, 100, PJ_TOUCH_SWIPE_LEFT));
    assert(context.note_page == 2);
    assert(pj_ui_handle_touch(&context, 50, 25, PJ_TOUCH_TAP));
    assert(context.state == PJ_UI_STATE_NOTE_DETAIL);
    assert(context.selected_note == 6);
    assert(context.note_detail_transcript);
    pj_framebuffer_t detail;
    pj_ui_compose_frame(&context, &detail);
    assert(count_black_pixels(&detail) > 0);
    assert(!pj_ui_handle_touch(&context, 100, 100, PJ_TOUCH_TAP));
    assert(pj_ui_handle_aux_long(&context));
    assert(context.state == PJ_UI_STATE_READ);
    assert(context.note_page == 2);

    const char changed[PJ_UI_MAX_NOTES][PJ_UI_NOTE_LABEL_LEN] = {
        "REC 20260717 0909 1", "Mixed Case: Alpha!", "Third / note?",
        "Fourth note", "Fifth note", "Sixth note",
        "A deliberately long transcript label with punctuation: 09:10!",
    };
    presenter_start(&fixture, &context);
    pj_ui_set_notes(&context, 7, changed);
    presenter_accept_partial(&fixture, &context);
    assert(pj_ui_handle_touch(&context, 100, 100, PJ_TOUCH_SWIPE_RIGHT));
    assert(context.note_page == 1);
    presenter_accept_partial(&fixture, &context);
}

static void test_audio_note_timestamps_are_compact(void)
{
    const char raw[][PJ_UI_NOTE_LABEL_LEN] = {
        "REC 20260717 0909 1",
    };
    const char compact[][PJ_UI_NOTE_LABEL_LEN] = {
        "JUL1709:09",
    };
    const char spaced[][PJ_UI_NOTE_LABEL_LEN] = {
        "JUL 17 09:09",
    };
    pj_ui_context_t context;
    pj_ui_context_t expected;
    pj_ui_context_t old_layout;
    pj_framebuffer_t actual_frame;
    pj_framebuffer_t expected_frame;
    pj_framebuffer_t old_frame;

    pj_ui_init(&context);
    context.state = PJ_UI_STATE_LISTEN;
    pj_ui_set_notes(&context, 1, raw);
    pj_ui_compose_frame(&context, &actual_frame);

    pj_ui_init(&expected);
    expected.state = PJ_UI_STATE_LISTEN;
    pj_ui_set_notes(&expected, 1, compact);
    pj_ui_compose_frame(&expected, &expected_frame);
    assert(memcmp(&actual_frame, &expected_frame, sizeof(actual_frame)) == 0);

    pj_ui_init(&old_layout);
    old_layout.state = PJ_UI_STATE_LISTEN;
    pj_ui_set_notes(&old_layout, 1, spaced);
    pj_ui_compose_frame(&old_layout, &old_frame);
    assert(framebuffer_differences(&actual_frame, &old_frame) > 0);
}

static void test_typed_preferences_revisions_and_theme_inversion(void)
{
    pj_ui_context_t context;
    pj_ui_init(&context);
    context.state = PJ_UI_STATE_SETTINGS;
    pj_ui_preferences_t preferences = current_preferences(&context);
    uint32_t visual = context.visual_revision;
    uint32_t full = context.full_refresh_revision;
    uint32_t interaction = context.interaction_generation;
    pj_ui_apply_preferences(&context, &preferences);
    assert(context.visual_revision == visual);
    assert(context.full_refresh_revision == full);
    assert(context.interaction_generation == interaction);

    presenter_fixture_t fixture;
    presenter_start(&fixture, &context);
    assert_asset_region(
        &fixture.accepted,
        pj_carbon_icon_lookup(PJ_CARBON_ICON_ASLEEP_FILLED, 64),
        slot_x(PJ_LAYOUT_SETTINGS_4_0M, PJ_LAYOUT_SLOT_SETTINGS_THEME),
        slot_y(PJ_LAYOUT_SETTINGS_4_0M, PJ_LAYOUT_SLOT_SETTINGS_THEME));
    assert_glyph_region(
        &fixture.accepted,
        pj_carbon_glyph_lookup(PJ_CARBON_GLYPH_SETTINGS_24H, 64),
        slot_x(PJ_LAYOUT_SETTINGS_4_0M,
               PJ_LAYOUT_SLOT_SETTINGS_HOUR_FORMAT),
        slot_y(PJ_LAYOUT_SETTINGS_4_0M,
               PJ_LAYOUT_SLOT_SETTINGS_HOUR_FORMAT));
    preferences.clock_24h = 0;
    pj_ui_apply_preferences(&context, &preferences);
    assert(context.visual_revision != visual);
    assert(context.full_refresh_revision == full);
    assert(context.interaction_generation != interaction);
    pj_ui_dirty_region_t hour_dirty =
        presenter_accept_partial(&fixture, &context);
    assert(hour_dirty.x < 100);
    assert(hour_dirty.x + hour_dirty.width <= 100);
    assert(hour_dirty.y >= 100);
    assert_glyph_region(
        &fixture.accepted,
        pj_carbon_glyph_lookup(PJ_CARBON_GLYPH_SETTINGS_12H, 64),
        slot_x(PJ_LAYOUT_SETTINGS_4_0M,
               PJ_LAYOUT_SLOT_SETTINGS_HOUR_FORMAT),
        slot_y(PJ_LAYOUT_SETTINGS_4_0M,
               PJ_LAYOUT_SLOT_SETTINGS_HOUR_FORMAT));

    pj_framebuffer_t light = fixture.accepted;
    full = context.full_refresh_revision;
    preferences.dark_mode = 1;
    pj_ui_apply_preferences(&context, &preferences);
    assert(context.full_refresh_revision != full);
    assert_presenter_full(&fixture, &context);
    for (size_t index = 0; index < sizeof(light.pixels); index++) {
        assert(fixture.accepted.pixels[index] ==
               (uint8_t)~light.pixels[index]);
    }

    context.state = PJ_UI_STATE_TIME_TEMP;
    preferences = current_preferences(&context);
    preferences.volume = -20;
    preferences.alarm_hour = 99;
    preferences.alarm_minute = -1;
    preferences.timer_seconds = 1;
    preferences.interval_seconds = 1;
    preferences.transcript_font_size = 99;
    pj_ui_apply_preferences(&context, &preferences);
    assert(context.volume == 0);
    assert(context.alarm_hour == 23);
    assert(context.alarm_minute == 0);
    assert(context.timer_preset_seconds == 30);
    assert(context.interval_preset_seconds == 30);
    assert(context.transcript_font_size == 3);
}

static void test_settings_mapping_and_volume_extrema(void)
{
    pj_ui_context_t context;
    navigate_home_to(&context, PJ_LAYOUT_SLOT_HOME_SETTINGS);
    assert(context.state == PJ_UI_STATE_SETTINGS);
    assert(context.volume == 10);
    uint32_t full = context.full_refresh_revision;
    assert(tap_slot(&context, PJ_LAYOUT_SETTINGS_4_0M,
                    PJ_LAYOUT_SLOT_SETTINGS_HOUR_FORMAT));
    assert(!context.clock_24h);
    assert(context.full_refresh_revision == full);

    full = context.full_refresh_revision;
    assert(tap_slot(&context, PJ_LAYOUT_SETTINGS_4_0M,
                    PJ_LAYOUT_SLOT_SETTINGS_THEME));
    assert(context.dark_mode);
    assert(context.full_refresh_revision != full);
    assert(tap_slot(&context, PJ_LAYOUT_SETTINGS_4_0M,
                    PJ_LAYOUT_SLOT_SETTINGS_THEME));
    assert(!context.dark_mode);

    assert(tap_slot(&context, PJ_LAYOUT_SETTINGS_4_0M,
                    PJ_LAYOUT_SLOT_SETTINGS_VOLUME));
    assert(context.state == PJ_UI_STATE_VOLUME);
    pj_ui_preferences_t preferences = current_preferences(&context);
    preferences.volume = 0;
    pj_ui_apply_preferences(&context, &preferences);
    presenter_fixture_t fixture;
    presenter_start(&fixture, &context);
    pj_framebuffer_t fixed_controls = fixture.accepted;
    pj_framebuffer_t previous_frame = fixture.accepted;
    assert(count_black_pixels_in_region(
               &fixture.accepted, 0, 0, PJ_DISPLAY_WIDTH, 100) > 0);
    for (int volume = 0; volume <= 10; volume++) {
        preferences.volume = volume;
        int previous = context.volume;
        pj_ui_apply_preferences(&context, &preferences);
        if (volume != previous) {
            pj_ui_dirty_region_t dirty =
                presenter_accept_partial(&fixture, &context);
            assert(dirty.y >= 0);
            assert(dirty.y + dirty.height <= 100);
        }
        pj_framebuffer_t frame;
        pj_ui_compose_frame(&context, &frame);
        assert_framebuffer_region_equal(
            &frame, &fixed_controls, 0, 100, PJ_DISPLAY_WIDTH, 100);
        assert(count_black_pixels_in_region(
                   &frame, 0, 0, PJ_DISPLAY_WIDTH, 100) < 5000);
        if (volume > 0) {
            assert(framebuffer_differences(&previous_frame, &frame) > 0);
        }
        previous_frame = frame;
    }
    for (int volume = 9; volume >= 0; volume--) {
        preferences.volume = volume;
        pj_ui_apply_preferences(&context, &preferences);
        presenter_accept_partial(&fixture, &context);
    }
    assert(context.volume == 0);
    assert(pj_ui_handle_touch(&context, 50, 150, PJ_TOUCH_TAP));
    assert(context.volume == 0);
    assert(pj_ui_handle_touch(&context, 150, 150, PJ_TOUCH_TAP));
    assert(context.volume == 1);
    assert(!pj_ui_handle_touch(&context, 50, 50, PJ_TOUCH_TAP));
}

static void test_battery_thresholds_and_clock_partials(void)
{
    static const struct {
        int percent;
        pj_carbon_icon_id_t icon;
    } cases[] = {
        {0, PJ_CARBON_ICON_BATTERY_EMPTY},
        {10, PJ_CARBON_ICON_BATTERY_EMPTY},
        {11, PJ_CARBON_ICON_BATTERY_LOW},
        {39, PJ_CARBON_ICON_BATTERY_LOW},
        {40, PJ_CARBON_ICON_BATTERY_HALF},
        {79, PJ_CARBON_ICON_BATTERY_HALF},
        {80, PJ_CARBON_ICON_BATTERY_FULL},
        {100, PJ_CARBON_ICON_BATTERY_FULL},
    };
    pj_ui_context_t context;
    pj_ui_init(&context);
    context.state = PJ_UI_STATE_TIME_TEMP;
    presenter_fixture_t fixture;
    presenter_start(&fixture, &context);
    for (size_t index = 0; index < ARRAY_LEN(cases); index++) {
        pj_ui_set_status(&context, cases[index].percent, 22, 45);
        presenter_accept_partial(&fixture, &context);
        assert_asset_region(&fixture.accepted,
                            pj_carbon_icon_lookup(cases[index].icon, 28),
                            65, 180);
    }
    pj_ui_set_time(&context, 9, 42, 2026, 7, 17);
    presenter_accept_partial(&fixture, &context);
    pj_ui_set_time(&context, 10, 0, 2026, 7, 18);
    presenter_accept_partial(&fixture, &context);
    pj_ui_set_status(&context, 200, 24, -20);
    assert(context.battery_percent == 100);
    assert(context.humidity_percent == -1);
    presenter_accept_partial(&fixture, &context);
}

static void test_alarm_caret_and_toggle_controls(void)
{
    pj_ui_context_t context;
    navigate_home_to(&context, PJ_LAYOUT_SLOT_HOME_TIME);
    assert(tap_slot(&context, PJ_LAYOUT_TIME_4_1,
                    PJ_LAYOUT_SLOT_TIME_ALARM));
    assert(context.state == PJ_UI_STATE_ALARM);
    assert(context.alarm_hour == 7 && context.alarm_minute == 30);
    int original_toggle = context.alarm_on;
    assert(pj_ui_handle_touch(&context, 100, 90, PJ_TOUCH_TAP));
    assert(context.alarm_on == !original_toggle);
    assert(pj_ui_handle_touch(&context, 50, 130, PJ_TOUCH_TAP));
    assert(context.alarm_hour == 8);
    assert(pj_ui_handle_touch(&context, 50, 180, PJ_TOUCH_TAP));
    assert(context.alarm_hour == 7);
    assert(pj_ui_handle_touch(&context, 150, 130, PJ_TOUCH_TAP));
    assert(context.alarm_minute == 45);
    assert(pj_ui_handle_touch(&context, 150, 180, PJ_TOUCH_TAP));
    assert(context.alarm_minute == 30);

    pj_framebuffer_t frame;
    pj_ui_compose_frame(&context, &frame);
    assert(count_black_pixels_in_region(&frame, 0, 110, 100, 45) > 0);
    assert(count_black_pixels_in_region(&frame, 0, 155, 100, 45) > 0);
    assert(count_black_pixels_in_region(&frame, 100, 110, 100, 45) > 0);
    assert(count_black_pixels_in_region(&frame, 100, 155, 100, 45) > 0);
}

static void test_stopwatch_timer_interval_commands(void)
{
    pj_ui_context_t context;
    pj_ui_time_projection_t projection;

    context = (pj_ui_context_t) {0};
    pj_ui_init(&context);
    context.state = PJ_UI_STATE_STOPWATCH;
    assert(pj_ui_handle_touch(&context, 50, 150, PJ_TOUCH_TAP));
    consume_expected_command(&context, PJ_UI_TIME_COMMAND_STOPWATCH_START, 0, 0);
    projection = time_projection(1, 300, 90);
    projection.stopwatch_running = 1;
    pj_ui_set_time_projection(&context, &projection);
    assert(pj_ui_handle_touch(&context, 50, 150, PJ_TOUCH_TAP));
    consume_expected_command(&context, PJ_UI_TIME_COMMAND_STOPWATCH_PAUSE, 0, 0);
    assert(pj_ui_handle_touch(&context, 150, 150, PJ_TOUCH_TAP));
    consume_expected_command(&context, PJ_UI_TIME_COMMAND_STOPWATCH_RESET, 0, 0);

    pj_ui_init(&context);
    context.state = PJ_UI_STATE_TIMER;
    assert(pj_ui_handle_touch(&context, 50, 125, PJ_TOUCH_TAP));
    assert(context.timer_seconds == 90);
    consume_expected_command(&context, PJ_UI_TIME_COMMAND_TIMER_SET, 90000, 0);
    assert(pj_ui_handle_touch(&context, 50, 175, PJ_TOUCH_TAP));
    assert(context.timer_seconds == 60);
    consume_expected_command(&context, PJ_UI_TIME_COMMAND_TIMER_SET, 60000, 0);
    assert(pj_ui_handle_touch(&context, 150, 125, PJ_TOUCH_TAP));
    consume_expected_command(&context, PJ_UI_TIME_COMMAND_TIMER_START, 60000, 0);
    projection = time_projection(0, 59, 60);
    projection.timer_running = 1;
    pj_ui_set_time_projection(&context, &projection);
    assert(pj_ui_handle_touch(&context, 150, 125, PJ_TOUCH_TAP));
    consume_expected_command(&context, PJ_UI_TIME_COMMAND_TIMER_PAUSE, 59000, 0);
    assert(pj_ui_handle_touch(&context, 150, 175, PJ_TOUCH_TAP));
    consume_expected_command(&context, PJ_UI_TIME_COMMAND_TIMER_RESET, 0, 0);

    pj_ui_init(&context);
    context.state = PJ_UI_STATE_INTERVAL;
    assert(pj_ui_handle_touch(&context, 50, 125, PJ_TOUCH_TAP));
    assert(context.interval_seconds == 90);
    consume_expected_command(&context, PJ_UI_TIME_COMMAND_INTERVAL_SET,
                             90000, 90000);
    assert(pj_ui_handle_touch(&context, 50, 175, PJ_TOUCH_TAP));
    assert(context.interval_seconds == 60);
    consume_expected_command(&context, PJ_UI_TIME_COMMAND_INTERVAL_SET,
                             60000, 60000);
    assert(pj_ui_handle_touch(&context, 150, 125, PJ_TOUCH_TAP));
    consume_expected_command(&context, PJ_UI_TIME_COMMAND_INTERVAL_START,
                             60000, 60000);
    projection = time_projection(0, 60, 59);
    projection.interval_running = 1;
    projection.interval_phase = 2;
    pj_ui_set_time_projection(&context, &projection);
    assert(pj_ui_handle_touch(&context, 150, 125, PJ_TOUCH_TAP));
    consume_expected_command(&context, PJ_UI_TIME_COMMAND_INTERVAL_PAUSE,
                             59000, 60000);
    assert(pj_ui_handle_touch(&context, 150, 175, PJ_TOUCH_TAP));
    consume_expected_command(&context, PJ_UI_TIME_COMMAND_INTERVAL_RESET, 0, 0);

    pj_framebuffer_t frame;
    pj_ui_compose_frame(&context, &frame);
    assert(count_black_pixels_in_region(&frame, 0, 99, 200, 3) == 0);
    assert(count_black_pixels_in_region(&frame, 99, 100, 3, 100) == 0);
}

static void test_timer_adjustment_sequence(void)
{
    static const int upward[] = {60, 90, 120};
    static const int downward[] = {90, 60, 30};
    pj_ui_context_t context;
    pj_ui_init(&context);
    context.state = PJ_UI_STATE_TIMER;
    pj_ui_preferences_t preferences = current_preferences(&context);
    preferences.timer_seconds = 30;
    pj_ui_apply_preferences(&context, &preferences);
    assert(context.timer_seconds == 30);
    assert(context.timer_preset_seconds == 30);

    presenter_fixture_t fixture;
    presenter_start(&fixture, &context);
    for (size_t index = 0; index < ARRAY_LEN(upward); index++) {
        assert(pj_ui_handle_touch(&context, 50, 125, PJ_TOUCH_TAP));
        assert(context.timer_seconds == upward[index]);
        assert(context.timer_preset_seconds == upward[index]);
        consume_expected_command(&context, PJ_UI_TIME_COMMAND_TIMER_SET,
                                 (uint64_t)upward[index] * 1000u, 0);
        presenter_accept_partial(&fixture, &context);
    }
    for (size_t index = 0; index < ARRAY_LEN(downward); index++) {
        assert(pj_ui_handle_touch(&context, 50, 175, PJ_TOUCH_TAP));
        assert(context.timer_seconds == downward[index]);
        assert(context.timer_preset_seconds == downward[index]);
        consume_expected_command(&context, PJ_UI_TIME_COMMAND_TIMER_SET,
                                 (uint64_t)downward[index] * 1000u, 0);
        presenter_accept_partial(&fixture, &context);
    }
}

static void test_interval_adjustment_sequence(void)
{
    static const int upward[] = {60, 90, 120};
    static const int downward[] = {90, 60, 30};
    pj_ui_context_t context;
    pj_ui_init(&context);
    context.state = PJ_UI_STATE_INTERVAL;
    pj_ui_preferences_t preferences = current_preferences(&context);
    preferences.interval_seconds = 30;
    pj_ui_apply_preferences(&context, &preferences);
    assert(context.interval_seconds == 30);
    assert(context.interval_preset_seconds == 30);

    presenter_fixture_t fixture;
    presenter_start(&fixture, &context);
    for (size_t index = 0; index < ARRAY_LEN(upward); index++) {
        assert(pj_ui_handle_touch(&context, 50, 125, PJ_TOUCH_TAP));
        assert(context.interval_seconds == upward[index]);
        assert(context.interval_preset_seconds == upward[index]);
        consume_expected_command(&context, PJ_UI_TIME_COMMAND_INTERVAL_SET,
                                 (uint64_t)upward[index] * 1000u,
                                 (uint64_t)upward[index] * 1000u);
        presenter_accept_partial(&fixture, &context);
    }
    for (size_t index = 0; index < ARRAY_LEN(downward); index++) {
        assert(pj_ui_handle_touch(&context, 50, 175, PJ_TOUCH_TAP));
        assert(context.interval_seconds == downward[index]);
        assert(context.interval_preset_seconds == downward[index]);
        consume_expected_command(&context, PJ_UI_TIME_COMMAND_INTERVAL_SET,
                                 (uint64_t)downward[index] * 1000u,
                                 (uint64_t)downward[index] * 1000u);
        presenter_accept_partial(&fixture, &context);
    }
}

static void test_stopwatch_play_pause_is_same_layout_partial(void)
{
    pj_ui_context_t context;
    pj_ui_init(&context);
    context.state = PJ_UI_STATE_STOPWATCH;
    presenter_fixture_t fixture;
    presenter_start(&fixture, &context);
    uint32_t layout = context.layout_epoch;
    uint32_t full = context.full_refresh_revision;

    assert(pj_ui_handle_touch(&context, 50, 150, PJ_TOUCH_TAP));
    consume_expected_command(&context, PJ_UI_TIME_COMMAND_STOPWATCH_START, 0, 0);
    pj_ui_time_projection_t projection = time_projection(0, 60, 60);
    projection.stopwatch_running = 1;
    pj_ui_set_time_projection(&context, &projection);
    assert(context.layout_epoch == layout);
    assert(context.full_refresh_revision == full);
    pj_ui_dirty_region_t dirty = presenter_accept_partial(&fixture, &context);
    assert(dirty.x >= 0 && dirty.x + dirty.width <= 100);
    assert(dirty.y >= 100);

    assert(pj_ui_handle_touch(&context, 50, 150, PJ_TOUCH_TAP));
    consume_expected_command(&context, PJ_UI_TIME_COMMAND_STOPWATCH_PAUSE, 0, 0);
    projection.stopwatch_running = 0;
    pj_ui_set_time_projection(&context, &projection);
    assert(context.layout_epoch == layout);
    assert(context.full_refresh_revision == full);
    dirty = presenter_accept_partial(&fixture, &context);
    assert(dirty.x >= 0 && dirty.x + dirty.width <= 100);
    assert(dirty.y >= 100);
}

static void test_interval_round_uses_duration_type_size_and_fits(void)
{
    pj_ui_context_t context;
    pj_ui_init(&context);
    context.state = PJ_UI_STATE_INTERVAL;
    pj_ui_time_projection_t projection = time_projection(0, 60, 60);
    projection.interval_phase = 1;
    pj_ui_set_time_projection(&context, &projection);
    pj_framebuffer_t frame;
    pj_ui_compose_frame(&context, &frame);
    assert_glyph_region(
        &frame, pj_carbon_glyph_lookup(PJ_CARBON_GLYPH_DIGIT_1, 64),
        100, 20);

    projection.interval_phase = INT_MAX;
    pj_ui_set_time_projection(&context, &projection);
    pj_ui_compose_frame(&context, &frame);
    assert(count_black_pixels_in_region(&frame, 0, 0, 200, 40) > 0);
    assert(count_black_pixels_in_region(&frame, 0, 0, 5, 40) == 0);
    assert(count_black_pixels_in_region(&frame, 195, 0, 5, 40) == 0);
}

static void assert_stopwatch_transition(int from, int to)
{
    pj_ui_context_t context;
    pj_ui_init(&context);
    context.state = PJ_UI_STATE_STOPWATCH;
    pj_ui_time_projection_t projection = time_projection(from, 300, 90);
    projection.stopwatch_running = 1;
    pj_ui_set_time_projection(&context, &projection);
    presenter_fixture_t fixture;
    presenter_start(&fixture, &context);
    projection.stopwatch_elapsed_ms = (uint64_t)to * 1000u;
    pj_ui_set_time_projection(&context, &projection);
    presenter_accept_partial(&fixture, &context);
    projection.stopwatch_elapsed_ms = (uint64_t)from * 1000u;
    pj_ui_set_time_projection(&context, &projection);
    presenter_accept_partial(&fixture, &context);
}

static void assert_timer_transition(int from, int to)
{
    pj_ui_context_t context;
    pj_ui_init(&context);
    context.state = PJ_UI_STATE_TIMER;
    pj_ui_time_projection_t projection = time_projection(0, from, 90);
    projection.timer_running = 1;
    pj_ui_set_time_projection(&context, &projection);
    presenter_fixture_t fixture;
    presenter_start(&fixture, &context);
    projection.timer_remaining_ms = (uint64_t)to * 1000u;
    pj_ui_set_time_projection(&context, &projection);
    presenter_accept_partial(&fixture, &context);
    projection.timer_remaining_ms = (uint64_t)from * 1000u;
    pj_ui_set_time_projection(&context, &projection);
    presenter_accept_partial(&fixture, &context);
}

static void assert_interval_transition(int from, int to)
{
    pj_ui_context_t context;
    pj_ui_init(&context);
    context.state = PJ_UI_STATE_INTERVAL;
    pj_ui_time_projection_t projection = time_projection(0, 300, from);
    projection.interval_running = 1;
    pj_ui_set_time_projection(&context, &projection);
    presenter_fixture_t fixture;
    presenter_start(&fixture, &context);
    projection.interval_remaining_ms = (uint64_t)to * 1000u;
    projection.interval_phase++;
    pj_ui_set_time_projection(&context, &projection);
    presenter_accept_partial(&fixture, &context);
    projection.interval_remaining_ms = (uint64_t)from * 1000u;
    projection.interval_phase--;
    pj_ui_set_time_projection(&context, &projection);
    presenter_accept_partial(&fixture, &context);
}

static void test_all_digit_carries_in_both_directions(void)
{
    static const int transitions[][2] = {
        {8, 9}, {9, 10}, {10, 11}, {11, 12}, {12, 13},
        {19, 20}, {39, 40}, {59, 60},
    };
    for (size_t index = 0; index < ARRAY_LEN(transitions); index++) {
        assert_stopwatch_transition(transitions[index][0], transitions[index][1]);
        assert_timer_transition(transitions[index][0], transitions[index][1]);
        assert_interval_transition(transitions[index][0], transitions[index][1]);
    }
}

static void test_sync_compact_common_scale_phases_and_transactions(void)
{
    pj_ui_context_t context;
    navigate_home_to(&context, PJ_LAYOUT_SLOT_HOME_SETTINGS);
    assert(tap_slot(&context, PJ_LAYOUT_SETTINGS_4_0M,
                    PJ_LAYOUT_SLOT_SETTINGS_SYNC));
    assert(context.state == PJ_UI_STATE_SYNC);
    assert(context.sync_inventory_state == PJ_UI_SYNC_INVENTORY_PENDING);
    uint32_t generation = pj_ui_sync_session_generation(&context);
    uint32_t request_generation = 0;
    assert(generation != 0);
    assert(pj_ui_consume_sync_preflight_request(&context, &request_generation));
    assert(request_generation == generation);
    assert(!pj_ui_consume_sync_preflight_request(&context, &request_generation));

    presenter_fixture_t fixture;
    presenter_start(&fixture, &context);
    pj_ui_set_sync_inventory(&context, generation,
                             PJ_UI_SYNC_INVENTORY_READY, 12, 3, 1);
    presenter_accept_partial(&fixture, &context);
    assert(!pj_ui_consume_sync_transfer_request(&context, &request_generation));
    uint32_t presentation = pj_ui_sync_presentation_generation(&context);
    assert(pj_ui_sync_presentation_committed(&context, presentation));
    assert(pj_ui_consume_sync_transfer_request(&context, &request_generation));
    assert(request_generation == generation);

    static const struct {
        const char *phase;
        int failed;
        const char *error;
        int request_pending;
    } phases[] = {
        {"discovering", 0, "", 0},
        {"pending", 0, "", 1},
        {"running", 0, "", 0},
        {"offline", 0, "network", 0},
        {"auth_failed", 1, "auth", 0},
        {"protocol_failed", 2, "protocol", 0},
        {"failed", 3, "terminal", 0},
        {"succeeded", 0, "", 0},
    };
    for (size_t index = 0; index < ARRAY_LEN(phases); index++) {
        pj_ui_set_sync_detail_for_generation(
            &context, generation, phases[index].phase, phases[index].failed,
            phases[index].error, phases[index].request_pending);
        presenter_accept_partial(&fixture, &context);
        assert(count_black_pixels(&fixture.accepted) > 0);
        for (int band = 0; band < 3; band++) {
            assert(count_black_pixels_in_region(
                       &fixture.accepted, 0, band * PJ_DISPLAY_HEIGHT / 3,
                       PJ_DISPLAY_WIDTH, PJ_DISPLAY_HEIGHT / 3) > 0);
        }
    }
    presentation = pj_ui_sync_presentation_generation(&context);
    assert(pj_ui_sync_presentation_committed(&context, presentation));
    assert(pj_ui_consume_sync_success_return(&context));
    assert(context.state == PJ_UI_STATE_SETTINGS);
}

static void test_exact_presenter_reconstruction_for_dynamic_screens(void)
{
    pj_ui_context_t context;
    presenter_fixture_t fixture;
    pj_ui_preferences_t preferences;
    pj_ui_time_projection_t projection;

    pj_ui_init(&context);
    context.state = PJ_UI_STATE_TIME_TEMP;
    presenter_start(&fixture, &context);
    pj_ui_set_time(&context, 10, 20, 2026, 7, 17);
    presenter_accept_partial(&fixture, &context);
    pj_ui_set_status(&context, 39, 20, 50);
    presenter_accept_partial(&fixture, &context);

    pj_ui_init(&context);
    context.state = PJ_UI_STATE_RECORD;
    context.record_state = PJ_RECORD_ACTIVE;
    presenter_start(&fixture, &context);
    pj_ui_set_recording_elapsed(&context, 1000);
    presenter_accept_partial(&fixture, &context);

    pj_ui_init(&context);
    context.state = PJ_UI_STATE_ALARM;
    presenter_start(&fixture, &context);
    preferences = current_preferences(&context);
    preferences.alarm_enabled = 1;
    preferences.alarm_minute = 45;
    pj_ui_apply_preferences(&context, &preferences);
    presenter_accept_partial(&fixture, &context);

    pj_ui_init(&context);
    context.state = PJ_UI_STATE_STOPWATCH;
    presenter_start(&fixture, &context);
    projection = time_projection(1, 300, 90);
    projection.stopwatch_running = 1;
    pj_ui_set_time_projection(&context, &projection);
    presenter_accept_partial(&fixture, &context);

    pj_ui_init(&context);
    context.state = PJ_UI_STATE_TIMER;
    presenter_start(&fixture, &context);
    projection = time_projection(0, 299, 90);
    projection.timer_running = 1;
    pj_ui_set_time_projection(&context, &projection);
    presenter_accept_partial(&fixture, &context);

    pj_ui_init(&context);
    context.state = PJ_UI_STATE_INTERVAL;
    presenter_start(&fixture, &context);
    projection = time_projection(0, 300, 89);
    projection.interval_running = 1;
    projection.interval_phase = 2;
    pj_ui_set_time_projection(&context, &projection);
    presenter_accept_partial(&fixture, &context);

    pj_ui_init(&context);
    context.state = PJ_UI_STATE_NOTE_DETAIL;
    context.note_detail_transcript = 0;
    presenter_start(&fixture, &context);
    pj_ui_set_audio_state(&context, 0, 1);
    presenter_accept_partial(&fixture, &context);

    pj_ui_init(&context);
    context.state = PJ_UI_STATE_VOLUME;
    presenter_start(&fixture, &context);
    preferences = current_preferences(&context);
    preferences.volume = 9;
    pj_ui_apply_preferences(&context, &preferences);
    presenter_accept_partial(&fixture, &context);

    pj_ui_init(&context);
    context.state = PJ_UI_STATE_LISTEN;
    seed_notes(&context, 6);
    presenter_start(&fixture, &context);
    assert(pj_ui_handle_touch(&context, 100, 100, PJ_TOUCH_SWIPE_LEFT));
    presenter_accept_partial(&fixture, &context);

    pj_ui_init(&context);
    context.state = PJ_UI_STATE_SYNC;
    presenter_start(&fixture, &context);
    pj_ui_set_sync_detail(&context, "running", 0, "", 1);
    presenter_accept_partial(&fixture, &context);

    pj_ui_init(&context);
    context.state = PJ_UI_STATE_SETTINGS;
    presenter_start(&fixture, &context);
    preferences = current_preferences(&context);
    preferences.clock_24h = 0;
    pj_ui_apply_preferences(&context, &preferences);
    presenter_accept_partial(&fixture, &context);
}

static void test_presenter_idle_barrier_rejection_and_navigation_full(void)
{
    pj_ui_context_t context;
    pj_ui_init(&context);
    context.state = PJ_UI_STATE_TIME_TEMP;
    presenter_fixture_t fixture;
    presenter_start(&fixture, &context);

    pj_ui_presenter_frame_t frame;
    pj_ui_presenter_revision_t revision = pj_ui_presenter_revision(&context);
    assert(pj_ui_presenter_prepare(&fixture.presenter, &context, &revision,
                                   &frame) == PJ_UI_FRAME_IDLE);

    context.interaction_generation++;
    revision = pj_ui_presenter_revision(&context);
    assert(pj_ui_presenter_prepare(&fixture.presenter, &context, &revision,
                                   &frame) == PJ_UI_FRAME_BARRIER);
    uint32_t token = frame.token;
    assert(frame.dirty.width == 0 && frame.dirty.height == 0);
    assert(pj_ui_presenter_reject(&fixture.presenter, token));
    pj_ui_presenter_frame_t retry;
    assert(pj_ui_presenter_prepare(&fixture.presenter, &context, &revision,
                                   &retry) == PJ_UI_FRAME_BARRIER);
    assert(retry.token == token);
    assert(retry.framebuffer == frame.framebuffer);
    assert(pj_ui_presenter_accept(&fixture.presenter, retry.token));

    pj_ui_wake(&context);
    assert(context.state == PJ_UI_STATE_HOME);
    assert_presenter_full(&fixture, &context);
    pj_ui_request_full_presentation(&context);
    assert_presenter_full(&fixture, &context);
}

int main(void)
{
    test_geometry_is_exhaustive_and_navigation_is_fixed();
    test_state_metadata_and_deterministic_composition();
    test_record_arming_and_back_contract();
    test_record_sleep_waits_for_audio_ack_before_reentry();
    test_playback_uses_only_compact_play_and_pause();
    test_case_punctuation_and_long_transcript();
    test_note_pages_selection_and_content_partials();
    test_audio_note_timestamps_are_compact();
    test_typed_preferences_revisions_and_theme_inversion();
    test_settings_mapping_and_volume_extrema();
    test_battery_thresholds_and_clock_partials();
    test_alarm_caret_and_toggle_controls();
    test_stopwatch_timer_interval_commands();
    test_timer_adjustment_sequence();
    test_interval_adjustment_sequence();
    test_stopwatch_play_pause_is_same_layout_partial();
    test_interval_round_uses_duration_type_size_and_fits();
    test_all_digit_carries_in_both_directions();
    test_sync_compact_common_scale_phases_and_transactions();
    test_exact_presenter_reconstruction_for_dynamic_screens();
    test_presenter_idle_barrier_rejection_and_navigation_full();
    puts("ui core tests passed");
    return 0;
}
