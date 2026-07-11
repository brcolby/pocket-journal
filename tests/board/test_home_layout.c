#include "pj_home_layout.h"

#include <assert.h>
#include <string.h>

static uint32_t record_checksum(const uint8_t *record)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < PJ_HOME_RECORD_BYTES - 4u; i++) {
        hash ^= record[i];
        hash *= 16777619u;
    }
    return hash;
}

static void write_record_checksum(uint8_t *record)
{
    uint32_t checksum = record_checksum(record);
    size_t offset = PJ_HOME_RECORD_BYTES - 4u;
    record[offset] = (uint8_t)checksum;
    record[offset + 1u] = (uint8_t)(checksum >> 8u);
    record[offset + 2u] = (uint8_t)(checksum >> 16u);
    record[offset + 3u] = (uint8_t)(checksum >> 24u);
}

static pj_home_layout_t four_slot_layout(void)
{
    pj_home_layout_t layout = {0};
    strcpy(layout.title, "Daily tools");
    layout.slot_count = 4;
    layout.slots[0] = (pj_home_slot_t) {"Capture", "microphone", "record"};
    layout.slots[1] = (pj_home_slot_t) {"Read", "read_me", "read"};
    layout.slots[2] = (pj_home_slot_t) {"Timer", "timer", "timer"};
    layout.slots[3] = (pj_home_slot_t) {"Network", "wifi", "sync"};
    return layout;
}

static void test_defaults_are_fixed_and_valid(void)
{
    pj_home_layout_t layout;
    pj_home_layout_defaults(&layout);
    assert(pj_home_layout_valid(&layout));
    assert(strcmp(layout.title, "Pocket Journal") == 0);
    assert(layout.slot_count == 3);
    assert(strcmp(layout.slots[0].destination, "notes") == 0);
}

static void test_ordered_slots_round_trip(void)
{
    pj_home_layout_t layout = four_slot_layout();
    uint8_t record[PJ_HOME_RECORD_BYTES];
    assert(pj_home_layout_encode_record(&layout, record, sizeof(record)) == sizeof(record));
    pj_home_layout_t decoded;
    assert(pj_home_layout_decode_record(record, sizeof(record), &decoded));
    assert(memcmp(&layout, &decoded, sizeof(layout)) == 0);
    assert(strcmp(decoded.slots[0].destination, "record") == 0);
    assert(strcmp(decoded.slots[3].destination, "sync") == 0);
}

static void test_unused_slot_garbage_is_ignored_and_canonicalized(void)
{
    pj_home_layout_t layout;
    memset(&layout, 0xa5, sizeof(layout));
    memset(layout.title, 0, sizeof(layout.title));
    strcpy(layout.title, "One tool");
    layout.slot_count = 1;
    memset(&layout.slots[0], 0, sizeof(layout.slots[0]));
    layout.slots[0] = (pj_home_slot_t) {"Notes", "notebook", "notes"};
    assert(pj_home_layout_valid(&layout));

    uint8_t first[PJ_HOME_RECORD_BYTES];
    uint8_t second[PJ_HOME_RECORD_BYTES];
    assert(pj_home_layout_encode_record(&layout, first, sizeof(first)) == sizeof(first));
    assert(pj_home_layout_encode_record(&layout, second, sizeof(second)) == sizeof(second));
    assert(memcmp(first, second, sizeof(first)) == 0);

    pj_home_layout_t decoded;
    assert(pj_home_layout_decode_record(first, sizeof(first), &decoded));
    assert(decoded.slot_count == 1);
    assert(strcmp(decoded.slots[0].destination, "notes") == 0);
    const uint8_t zero_slot[sizeof(decoded.slots[1])] = {0};
    assert(memcmp(&decoded.slots[1], zero_slot, sizeof(zero_slot)) == 0);

    pj_home_layout_t canonical;
    assert(pj_home_layout_canonical_copy(&canonical, &layout));
    assert(memcmp(&canonical.slots[1], zero_slot, sizeof(zero_slot)) == 0);
}

static void test_invalid_models_are_rejected(void)
{
    pj_home_layout_t layout = four_slot_layout();
    layout.slot_count = 5;
    assert(!pj_home_layout_valid(&layout));

    layout = four_slot_layout();
    strcpy(layout.slots[1].icon, "downloaded_icon");
    assert(!pj_home_layout_valid(&layout));

    layout = four_slot_layout();
    strcpy(layout.slots[1].destination, "removed_app");
    assert(!pj_home_layout_valid(&layout));

    layout = four_slot_layout();
    strcpy(layout.slots[1].label, " trailing ");
    assert(!pj_home_layout_valid(&layout));
}

static void test_corrupt_storage_falls_back_cleanly(void)
{
    pj_home_layout_t layout = four_slot_layout();
    uint8_t record[PJ_HOME_RECORD_BYTES];
    assert(pj_home_layout_encode_record(&layout, record, sizeof(record)) == sizeof(record));
    pj_home_layout_t decoded;
    assert(!pj_home_layout_decode_or_default(record, sizeof(record) - 1u, &decoded));
    assert(strcmp(decoded.title, "Pocket Journal") == 0);
    assert(strcmp(decoded.slots[0].destination, "notes") == 0);
    record[40] ^= 0x20u;
    assert(!pj_home_layout_decode_or_default(record, sizeof(record), &decoded));
    assert(pj_home_layout_valid(&decoded));
    assert(strcmp(decoded.slots[2].destination, "settings") == 0);
}

static void test_removed_stored_destination_uses_fallback(void)
{
    pj_home_layout_t layout = four_slot_layout();
    uint8_t record[PJ_HOME_RECORD_BYTES];
    assert(pj_home_layout_encode_record(&layout, record, sizeof(record)) == sizeof(record));
    const size_t first_destination = 8u + PJ_HOME_TITLE_LEN + PJ_HOME_LABEL_LEN + PJ_HOME_ICON_LEN;
    memset(record + first_destination, 0, PJ_HOME_DESTINATION_LEN);
    memcpy(record + first_destination, "removed_app", sizeof("removed_app"));
    write_record_checksum(record);

    pj_home_layout_t decoded;
    assert(!pj_home_layout_decode_or_default(record, sizeof(record), &decoded));
    assert(strcmp(decoded.slots[0].destination, "notes") == 0);
}

int main(void)
{
    test_defaults_are_fixed_and_valid();
    test_ordered_slots_round_trip();
    test_unused_slot_garbage_is_ignored_and_canonicalized();
    test_invalid_models_are_rejected();
    test_corrupt_storage_falls_back_cleanly();
    test_removed_stored_destination_uses_fallback();
    return 0;
}
