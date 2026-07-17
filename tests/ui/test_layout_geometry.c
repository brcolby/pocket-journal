#include "pj_layout_geometry.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static void test_layout_metadata_and_slot_mappings(void)
{
    static const uint8_t expected_rule_counts[PJ_LAYOUT_COUNT] = {3, 3, 5, 5};
    static const uint8_t expected_slot_counts[PJ_LAYOUT_COUNT] = {3, 3, 4, 4};
    static const pj_layout_slot_id_t expected_slots[PJ_LAYOUT_COUNT][4] = {
        {PJ_LAYOUT_SLOT_HOME_TIME, PJ_LAYOUT_SLOT_HOME_NOTES, PJ_LAYOUT_SLOT_HOME_SETTINGS,
         PJ_LAYOUT_SLOT_NONE},
        {PJ_LAYOUT_SLOT_NOTES_RECORD, PJ_LAYOUT_SLOT_NOTES_LISTEN, PJ_LAYOUT_SLOT_NOTES_READ,
         PJ_LAYOUT_SLOT_NONE},
        {PJ_LAYOUT_SLOT_TIME_ALARM, PJ_LAYOUT_SLOT_TIME_STOPWATCH, PJ_LAYOUT_SLOT_TIME_TIMER,
         PJ_LAYOUT_SLOT_TIME_INTERVAL},
        {PJ_LAYOUT_SLOT_SETTINGS_VOLUME, PJ_LAYOUT_SLOT_SETTINGS_THEME,
         PJ_LAYOUT_SLOT_SETTINGS_HOUR_FORMAT, PJ_LAYOUT_SLOT_SETTINGS_SYNC},
    };

    for (int layout_id = 0; layout_id < PJ_LAYOUT_COUNT; layout_id++) {
        const pj_layout_geometry_t *geometry = pj_layout_geometry((pj_layout_id_t)layout_id);
        assert(geometry != NULL);
        assert(geometry->id == (pj_layout_id_t)layout_id);
        assert(geometry->rule_count == expected_rule_counts[layout_id]);
        assert(geometry->slot_count == expected_slot_counts[layout_id]);
        for (uint8_t slot = 0; slot < geometry->slot_count; slot++) {
            assert(geometry->slots[slot].id == expected_slots[layout_id][slot]);
            assert(geometry->slots[slot].hit_region.vertex_count >= 3);
        }
    }
    assert(pj_layout_geometry((pj_layout_id_t)-1) == NULL);
    assert(pj_layout_geometry(PJ_LAYOUT_COUNT) == NULL);
}

static void test_every_pixel_is_owned(void)
{
    for (int layout_id = 0; layout_id < PJ_LAYOUT_COUNT; layout_id++) {
        uint32_t assigned = 0;
        for (uint16_t y = 0; y < PJ_LAYOUT_DISPLAY_HEIGHT; y++) {
            for (uint16_t x = 0; x < PJ_LAYOUT_DISPLAY_WIDTH; x++) {
                assert(pj_layout_hit_test((pj_layout_id_t)layout_id, x, y) != PJ_LAYOUT_SLOT_NONE);
                assigned++;
            }
        }
        assert(assigned == 40000u);
    }
    assert(pj_layout_hit_test(PJ_LAYOUT_HOME_3_1, PJ_LAYOUT_DISPLAY_WIDTH, 0) ==
           PJ_LAYOUT_SLOT_NONE);
    assert(pj_layout_hit_test(PJ_LAYOUT_HOME_3_1, 0, PJ_LAYOUT_DISPLAY_HEIGHT) ==
           PJ_LAYOUT_SLOT_NONE);
}

static void test_representative_centers(void)
{
    assert(pj_layout_hit_test(PJ_LAYOUT_HOME_3_1, 20, 80) == PJ_LAYOUT_SLOT_HOME_TIME);
    assert(pj_layout_hit_test(PJ_LAYOUT_HOME_3_1, 100, 180) == PJ_LAYOUT_SLOT_HOME_NOTES);
    assert(pj_layout_hit_test(PJ_LAYOUT_HOME_3_1, 180, 80) == PJ_LAYOUT_SLOT_HOME_SETTINGS);

    assert(pj_layout_hit_test(PJ_LAYOUT_NOTES_3_1M, 100, 20) == PJ_LAYOUT_SLOT_NOTES_RECORD);
    assert(pj_layout_hit_test(PJ_LAYOUT_NOTES_3_1M, 20, 120) == PJ_LAYOUT_SLOT_NOTES_LISTEN);
    assert(pj_layout_hit_test(PJ_LAYOUT_NOTES_3_1M, 180, 120) == PJ_LAYOUT_SLOT_NOTES_READ);

    assert(pj_layout_hit_test(PJ_LAYOUT_TIME_4_1, 40, 40) == PJ_LAYOUT_SLOT_TIME_ALARM);
    assert(pj_layout_hit_test(PJ_LAYOUT_TIME_4_1, 160, 40) == PJ_LAYOUT_SLOT_TIME_STOPWATCH);
    assert(pj_layout_hit_test(PJ_LAYOUT_TIME_4_1, 40, 160) == PJ_LAYOUT_SLOT_TIME_TIMER);
    assert(pj_layout_hit_test(PJ_LAYOUT_TIME_4_1, 160, 160) == PJ_LAYOUT_SLOT_TIME_INTERVAL);
    /* Pixel centers with x + y == 199 lie exactly on the retained center diagonal. */
    assert(pj_layout_hit_test(PJ_LAYOUT_TIME_4_1, 100, 99) == PJ_LAYOUT_SLOT_TIME_ALARM);

    assert(pj_layout_hit_test(PJ_LAYOUT_SETTINGS_4_0M, 40, 40) ==
           PJ_LAYOUT_SLOT_SETTINGS_VOLUME);
    assert(pj_layout_hit_test(PJ_LAYOUT_SETTINGS_4_0M, 160, 40) ==
           PJ_LAYOUT_SLOT_SETTINGS_THEME);
    assert(pj_layout_hit_test(PJ_LAYOUT_SETTINGS_4_0M, 40, 160) ==
           PJ_LAYOUT_SLOT_SETTINGS_HOUR_FORMAT);
    assert(pj_layout_hit_test(PJ_LAYOUT_SETTINGS_4_0M, 160, 160) ==
           PJ_LAYOUT_SLOT_SETTINGS_SYNC);
    assert(pj_layout_hit_test(PJ_LAYOUT_SETTINGS_4_0M, 100, 99) ==
           PJ_LAYOUT_SLOT_SETTINGS_THEME);

    for (int layout_id = 0; layout_id < PJ_LAYOUT_COUNT; layout_id++) {
        const pj_layout_geometry_t *geometry = pj_layout_geometry((pj_layout_id_t)layout_id);
        assert(geometry != NULL);
        for (uint8_t slot = 0; slot < geometry->slot_count; slot++) {
            uint16_t x = (uint16_t)(geometry->slots[slot].icon_center.x / PJ_LAYOUT_COORD_SCALE);
            uint16_t y = (uint16_t)(geometry->slots[slot].icon_center.y / PJ_LAYOUT_COORD_SCALE);
            assert(pj_layout_hit_test((pj_layout_id_t)layout_id, x, y) ==
                   geometry->slots[slot].id);
        }
    }
}

static void test_no_outer_box_and_center_diagonal_is_retained(void)
{
    for (int layout_id = 0; layout_id < PJ_LAYOUT_COUNT; layout_id++) {
        assert(!pj_layout_pixel_is_rule((pj_layout_id_t)layout_id, 0, 0));
        assert(!pj_layout_pixel_is_rule((pj_layout_id_t)layout_id, 199, 0));
        assert(!pj_layout_pixel_is_rule((pj_layout_id_t)layout_id, 0, 199));
        assert(!pj_layout_pixel_is_rule((pj_layout_id_t)layout_id, 199, 199));
    }

    for (int layout_id = PJ_LAYOUT_TIME_4_1; layout_id <= PJ_LAYOUT_SETTINGS_4_0M;
         layout_id++) {
        const pj_layout_geometry_t *geometry = pj_layout_geometry((pj_layout_id_t)layout_id);
        const int16_t maximum_x = (int16_t)(PJ_LAYOUT_DISPLAY_WIDTH * PJ_LAYOUT_COORD_SCALE);
        const int16_t maximum_y = (int16_t)(PJ_LAYOUT_DISPLAY_HEIGHT * PJ_LAYOUT_COORD_SCALE);
        uint8_t completely_interior = 0;
        assert(geometry != NULL);
        for (uint8_t index = 0; index < geometry->rule_count; index++) {
            const pj_layout_rule_t *rule = &geometry->rules[index];
            if (rule->start.x > 0 && rule->start.x < maximum_x && rule->start.y > 0 &&
                rule->start.y < maximum_y && rule->end.x > 0 && rule->end.x < maximum_x &&
                rule->end.y > 0 && rule->end.y < maximum_y) {
                completely_interior++;
            }
        }
        assert(completely_interior == 1);
    }
}

static size_t rule_pixels_in_row(pj_layout_id_t layout_id, uint16_t y)
{
    size_t count = 0;
    for (uint16_t x = 0; x < PJ_LAYOUT_DISPLAY_WIDTH; x++) {
        if (pj_layout_pixel_is_rule(layout_id, x, y)) {
            count++;
        }
    }
    return count;
}

static void test_four_pixel_rule_raster(void)
{
    assert(PJ_LAYOUT_RULE_WIDTH_PX == 4u);
    assert(rule_pixels_in_row(PJ_LAYOUT_HOME_3_1, 10) == 4u);
    assert(rule_pixels_in_row(PJ_LAYOUT_NOTES_3_1M, 190) == 4u);
    assert(rule_pixels_in_row(PJ_LAYOUT_TIME_4_1, 190) == 4u);
    assert(rule_pixels_in_row(PJ_LAYOUT_SETTINGS_4_0M, 190) == 4u);
    assert(!pj_layout_pixel_is_rule(PJ_LAYOUT_HOME_3_1, PJ_LAYOUT_DISPLAY_WIDTH, 0));
}

int main(void)
{
    test_layout_metadata_and_slot_mappings();
    test_every_pixel_is_owned();
    test_representative_centers();
    test_no_outer_box_and_center_diagonal_is_retained();
    test_four_pixel_rule_raster();
    return 0;
}
