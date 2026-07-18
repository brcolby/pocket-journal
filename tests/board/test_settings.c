#include "pj_settings.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t slots[PJ_SETTINGS_SLOT_COUNT][PJ_SETTINGS_RECORD_BYTES];
    size_t sizes[PJ_SETTINGS_SLOT_COUNT];
    pj_settings_io_result_t read_results[PJ_SETTINGS_SLOT_COUNT];
    int fail_write;
    int partial_write;
    unsigned writes;
    unsigned last_slot;
} store_fixture_t;

static pj_settings_io_result_t read_slot(void *context, unsigned slot,
                                         uint8_t *record, size_t *record_size)
{
    store_fixture_t *fixture = context;
    assert(slot < PJ_SETTINGS_SLOT_COUNT);
    if (fixture->read_results[slot] != PJ_SETTINGS_IO_OK) {
        return fixture->read_results[slot];
    }
    assert(*record_size >= fixture->sizes[slot]);
    memcpy(record, fixture->slots[slot], fixture->sizes[slot]);
    *record_size = fixture->sizes[slot];
    return PJ_SETTINGS_IO_OK;
}

static pj_settings_io_result_t write_slot(void *context, unsigned slot,
                                          const uint8_t *record,
                                          size_t record_size)
{
    store_fixture_t *fixture = context;
    assert(slot < PJ_SETTINGS_SLOT_COUNT);
    fixture->writes++;
    fixture->last_slot = slot;
    if (fixture->partial_write) {
        size_t partial_size = record_size / 2u;
        memcpy(fixture->slots[slot], record, partial_size);
        fixture->sizes[slot] = partial_size;
        fixture->read_results[slot] = PJ_SETTINGS_IO_OK;
    }
    if (fixture->fail_write) {
        return PJ_SETTINGS_IO_ERROR;
    }
    memcpy(fixture->slots[slot], record, record_size);
    fixture->sizes[slot] = record_size;
    fixture->read_results[slot] = PJ_SETTINGS_IO_OK;
    return PJ_SETTINGS_IO_OK;
}

static void fixture_init(store_fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->read_results[0] = PJ_SETTINGS_IO_NOT_FOUND;
    fixture->read_results[1] = PJ_SETTINGS_IO_NOT_FOUND;
}

static void put_record(store_fixture_t *fixture, unsigned slot,
                       const pj_settings_t *settings, uint32_t generation)
{
    fixture->sizes[slot] = pj_settings_encode_record(
        settings, generation, fixture->slots[slot], sizeof(fixture->slots[slot]));
    assert(fixture->sizes[slot] == PJ_SETTINGS_RECORD_BYTES);
    fixture->read_results[slot] = PJ_SETTINGS_IO_OK;
}

static pj_settings_store_t make_store(store_fixture_t *fixture)
{
    pj_settings_store_t store;
    pj_settings_store_init(&store, fixture, read_slot, write_slot);
    return store;
}

static void test_record_round_trip(void)
{
    pj_settings_t settings;
    pj_settings_defaults(&settings);
    settings.volume = 4;
    settings.alarm_hour = 19;
    settings.temperature_fahrenheit = 1;
    uint8_t record[PJ_SETTINGS_RECORD_BYTES];
    assert(pj_settings_encode_record(&settings, 42, record, sizeof(record)) ==
           sizeof(record));

    pj_settings_t decoded;
    uint32_t generation = 0;
    assert(pj_settings_decode_record(record, sizeof(record), &decoded,
                                     &generation));
    assert(memcmp(&decoded, &settings, sizeof(settings)) == 0);
    assert(generation == 42);

    record[17] ^= 0x80;
    assert(!pj_settings_decode_record(record, sizeof(record), &decoded, NULL));
    record[17] ^= 0x80;
    record[4]++;
    assert(!pj_settings_decode_record(record, sizeof(record), &decoded, NULL));
    assert(!pj_settings_decode_record(record, sizeof(record) - 1u, &decoded, NULL));
}

static void test_load_outcomes_and_selection(void)
{
    store_fixture_t fixture;
    fixture_init(&fixture);
    pj_settings_store_t store = make_store(&fixture);
    pj_settings_t output;
    memset(&output, 0x5a, sizeof(output));
    pj_settings_t unchanged = output;
    assert(pj_settings_store_load(&store, &output) == PJ_SETTINGS_LOAD_NOT_FOUND);
    assert(memcmp(&output, &unchanged, sizeof(output)) == 0);

    fixture.read_results[0] = PJ_SETTINGS_IO_ERROR;
    assert(pj_settings_store_load(&store, &output) == PJ_SETTINGS_LOAD_ERROR);
    assert(memcmp(&output, &unchanged, sizeof(output)) == 0);

    pj_settings_t older;
    pj_settings_defaults(&older);
    older.volume = 3;
    pj_settings_t newer = older;
    newer.volume = 8;
    put_record(&fixture, 0, &older, 8);
    put_record(&fixture, 1, &newer, 9);
    assert(pj_settings_store_load(&store, &output) == PJ_SETTINGS_LOAD_OK);
    assert(output.volume == 8);
    assert(store.active_slot == 1);
    assert(store.generation == 9);

    fixture.read_results[1] = PJ_SETTINGS_IO_ERROR;
    assert(pj_settings_store_load(&store, &output) == PJ_SETTINGS_LOAD_OK);
    assert(output.volume == 3);
    assert(store.active_slot == 0);
    assert(store.degraded);
    assert(!pj_settings_store_save(&store, &newer));

    fixture.read_results[1] = PJ_SETTINGS_IO_OK;
    fixture.slots[1][17] ^= 0x80;
    assert(pj_settings_store_load(&store, &output) == PJ_SETTINGS_LOAD_OK);
    assert(output.volume == 3);
    assert(!store.degraded);
    assert(pj_settings_store_save(&store, &newer));
}

static void test_generation_wrap_and_ambiguous_records(void)
{
    store_fixture_t fixture;
    fixture_init(&fixture);
    pj_settings_t before_wrap;
    pj_settings_defaults(&before_wrap);
    before_wrap.volume = 2;
    pj_settings_t after_wrap = before_wrap;
    after_wrap.volume = 7;
    put_record(&fixture, 0, &before_wrap, UINT32_MAX);
    put_record(&fixture, 1, &after_wrap, 0);

    pj_settings_store_t store = make_store(&fixture);
    pj_settings_t output;
    assert(pj_settings_store_load(&store, &output) == PJ_SETTINGS_LOAD_OK);
    assert(output.volume == 7);

    put_record(&fixture, 0, &before_wrap, 10);
    put_record(&fixture, 1, &after_wrap, 10);
    pj_settings_t unchanged = output;
    assert(pj_settings_store_load(&store, &output) == PJ_SETTINGS_LOAD_ERROR);
    assert(memcmp(&output, &unchanged, sizeof(output)) == 0);
    assert(!store.has_record);
    assert(store.active_slot == -1);
}

static void test_save_rotation_and_power_loss(void)
{
    store_fixture_t fixture;
    fixture_init(&fixture);
    pj_settings_store_t store = make_store(&fixture);
    pj_settings_t first;
    pj_settings_defaults(&first);
    first.volume = 4;
    assert(pj_settings_store_save(&store, &first));
    assert(fixture.last_slot == 0);
    assert(store.generation == 1);

    pj_settings_t second = first;
    second.volume = 9;
    assert(pj_settings_store_save(&store, &second));
    assert(fixture.last_slot == 1);
    assert(store.generation == 2);

    pj_settings_t third = second;
    third.dark_mode = 1;
    fixture.fail_write = 1;
    assert(!pj_settings_store_save(&store, &third));
    assert(store.active_slot == 1);
    assert(store.generation == 2);

    fixture.partial_write = 1;
    assert(!pj_settings_store_save(&store, &third));
    assert(store.active_slot == 1);
    assert(store.generation == 2);

    pj_settings_store_t rebooted = make_store(&fixture);
    pj_settings_t recovered;
    assert(pj_settings_store_load(&rebooted, &recovered) == PJ_SETTINGS_LOAD_OK);
    assert(memcmp(&recovered, &second, sizeof(second)) == 0);
    assert(rebooted.active_slot == 1);
    assert(rebooted.generation == 2);
}

int main(void)
{
    test_record_round_trip();
    test_load_outcomes_and_selection();
    test_generation_wrap_and_ambiguous_records();
    test_save_rotation_and_power_loss();

    pj_settings_t settings;
    pj_settings_defaults(&settings);
    assert(pj_settings_valid(&settings));
    assert(settings.volume == 10);
    assert(settings.dark_mode == 0);
    assert(settings.alarm_hour == 7);
    assert(settings.alarm_minute == 30);
    assert(settings.timer_seconds == 60);
    assert(settings.interval_seconds == 60);
    assert(settings.clock_24h == 1);
    assert(settings.temperature_fahrenheit == 0);
    assert(settings.transcript_font_size == 3);

    assert(pj_settings_codec_volume(-1) == 0);
    assert(pj_settings_codec_volume(0) == 0);
    assert(pj_settings_codec_volume(1) == 10);
    assert(pj_settings_codec_volume(5) == 50);
    assert(pj_settings_codec_volume(10) == 100);
    assert(pj_settings_codec_volume(11) == 100);

    settings.volume = 11;
    assert(!pj_settings_valid(&settings));
    pj_settings_defaults(&settings);
    settings.alarm_hour = 24;
    assert(!pj_settings_valid(&settings));
    pj_settings_defaults(&settings);
    settings.timer_seconds = 29;
    assert(!pj_settings_valid(&settings));
    pj_settings_defaults(&settings);
    settings.interval_seconds = 29;
    assert(!pj_settings_valid(&settings));
    settings.interval_seconds = 30;
    assert(pj_settings_valid(&settings));
    pj_settings_defaults(&settings);
    settings.interval_seconds = 86401;
    assert(!pj_settings_valid(&settings));
    pj_settings_defaults(&settings);
    settings.clock_24h = 2;
    assert(!pj_settings_valid(&settings));
    pj_settings_defaults(&settings);
    settings.temperature_fahrenheit = -1;
    assert(!pj_settings_valid(&settings));
    pj_settings_defaults(&settings);
    settings.transcript_font_size = 4;
    assert(!pj_settings_valid(&settings));

    puts("settings tests passed");
    return 0;
}
