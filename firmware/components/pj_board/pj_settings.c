#include "pj_settings.h"

#include <stddef.h>
#include <string.h>

#define PJ_SETTINGS_RECORD_VERSION 1u
#define PJ_SETTINGS_FIELD_COUNT 10u
#define PJ_SETTINGS_PAYLOAD_OFFSET 12u
#define PJ_SETTINGS_CHECKSUM_OFFSET 52u

static uint32_t crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1u) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

static void put_u32_le(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8u);
    out[2] = (uint8_t)(value >> 16u);
    out[3] = (uint8_t)(value >> 24u);
}

static uint32_t get_u32_le(const uint8_t *in)
{
    return (uint32_t)in[0] |
           ((uint32_t)in[1] << 8u) |
           ((uint32_t)in[2] << 16u) |
           ((uint32_t)in[3] << 24u);
}

static void put_i32_le(uint8_t *out, int value)
{
    put_u32_le(out, (uint32_t)(int32_t)value);
}

static int get_i32_le(const uint8_t *in)
{
    return (int)(int32_t)get_u32_le(in);
}

static int generation_newer(uint32_t candidate, uint32_t current)
{
    return (int32_t)(candidate - current) > 0;
}

void pj_settings_defaults(pj_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }
    *settings = (pj_settings_t) {
        .volume = 10,
        .dark_mode = 0,
        .alarm_enabled = 0,
        .alarm_hour = 7,
        .alarm_minute = 30,
        .timer_seconds = 300,
        .interval_seconds = 90,
        .clock_24h = 1,
        .temperature_fahrenheit = 0,
        .transcript_font_size = 3,
    };
}

int pj_settings_valid(const pj_settings_t *settings)
{
    return settings != NULL &&
           settings->volume >= 0 && settings->volume <= 10 &&
           (settings->dark_mode == 0 || settings->dark_mode == 1) &&
           (settings->alarm_enabled == 0 || settings->alarm_enabled == 1) &&
           settings->alarm_hour >= 0 && settings->alarm_hour <= 23 &&
           settings->alarm_minute >= 0 && settings->alarm_minute <= 59 &&
           settings->timer_seconds >= 30 && settings->timer_seconds <= 86400 &&
           settings->interval_seconds >= 60 && settings->interval_seconds <= 86400 &&
           (settings->clock_24h == 0 || settings->clock_24h == 1) &&
           (settings->temperature_fahrenheit == 0 || settings->temperature_fahrenheit == 1) &&
           settings->transcript_font_size >= 2 && settings->transcript_font_size <= 3;
}

int pj_settings_codec_volume(int volume)
{
    if (volume <= 0) {
        return 0;
    }
    if (volume >= 10) {
        return 100;
    }
    return volume * 10;
}

size_t pj_settings_encode_record(const pj_settings_t *settings,
                                 uint32_t generation, uint8_t *record,
                                 size_t record_size)
{
    if (!pj_settings_valid(settings) || record == NULL ||
        record_size < PJ_SETTINGS_RECORD_BYTES) {
        return 0;
    }

    memset(record, 0, PJ_SETTINGS_RECORD_BYTES);
    memcpy(record, "PJST", 4);
    record[4] = PJ_SETTINGS_RECORD_VERSION;
    record[5] = PJ_SETTINGS_FIELD_COUNT;
    put_u32_le(record + 8, generation);

    const int values[PJ_SETTINGS_FIELD_COUNT] = {
        settings->volume,
        settings->dark_mode,
        settings->alarm_enabled,
        settings->alarm_hour,
        settings->alarm_minute,
        settings->timer_seconds,
        settings->interval_seconds,
        settings->clock_24h,
        settings->temperature_fahrenheit,
        settings->transcript_font_size,
    };
    for (size_t i = 0; i < PJ_SETTINGS_FIELD_COUNT; i++) {
        put_i32_le(record + PJ_SETTINGS_PAYLOAD_OFFSET + i * 4u, values[i]);
    }
    put_u32_le(record + PJ_SETTINGS_CHECKSUM_OFFSET,
               crc32(record, PJ_SETTINGS_CHECKSUM_OFFSET));
    return PJ_SETTINGS_RECORD_BYTES;
}

int pj_settings_decode_record(const uint8_t *record, size_t record_size,
                              pj_settings_t *settings, uint32_t *generation)
{
    if (record == NULL || settings == NULL ||
        record_size != PJ_SETTINGS_RECORD_BYTES ||
        memcmp(record, "PJST", 4) != 0 ||
        record[4] != PJ_SETTINGS_RECORD_VERSION ||
        record[5] != PJ_SETTINGS_FIELD_COUNT ||
        record[6] != 0 || record[7] != 0 ||
        get_u32_le(record + PJ_SETTINGS_CHECKSUM_OFFSET) !=
            crc32(record, PJ_SETTINGS_CHECKSUM_OFFSET)) {
        return 0;
    }

    pj_settings_t decoded = {
        .volume = get_i32_le(record + 12),
        .dark_mode = get_i32_le(record + 16),
        .alarm_enabled = get_i32_le(record + 20),
        .alarm_hour = get_i32_le(record + 24),
        .alarm_minute = get_i32_le(record + 28),
        .timer_seconds = get_i32_le(record + 32),
        .interval_seconds = get_i32_le(record + 36),
        .clock_24h = get_i32_le(record + 40),
        .temperature_fahrenheit = get_i32_le(record + 44),
        .transcript_font_size = get_i32_le(record + 48),
    };
    if (!pj_settings_valid(&decoded)) {
        return 0;
    }

    *settings = decoded;
    if (generation != NULL) {
        *generation = get_u32_le(record + 8);
    }
    return 1;
}

void pj_settings_store_init(pj_settings_store_t *store, void *context,
                            pj_settings_read_slot_fn read_slot,
                            pj_settings_write_slot_fn write_slot)
{
    if (store == NULL) {
        return;
    }
    *store = (pj_settings_store_t) {
        .context = context,
        .read_slot = read_slot,
        .write_slot = write_slot,
        .active_slot = -1,
    };
}

pj_settings_load_result_t pj_settings_store_load(pj_settings_store_t *store,
                                                 pj_settings_t *settings)
{
    if (store == NULL || settings == NULL || store->read_slot == NULL) {
        return PJ_SETTINGS_LOAD_ERROR;
    }

    store->has_record = 0;
    store->active_slot = -1;
    store->generation = 0;
    store->degraded = 0;

    pj_settings_t candidates[PJ_SETTINGS_SLOT_COUNT];
    uint32_t generations[PJ_SETTINGS_SLOT_COUNT] = {0};
    int valid[PJ_SETTINGS_SLOT_COUNT] = {0};
    int saw_io_error = 0;
    int saw_invalid = 0;
    int saw_not_found = 0;

    for (unsigned slot = 0; slot < PJ_SETTINGS_SLOT_COUNT; slot++) {
        uint8_t record[PJ_SETTINGS_RECORD_BYTES];
        size_t record_size = sizeof(record);
        pj_settings_io_result_t result = store->read_slot(
            store->context, slot, record, &record_size);
        if (result == PJ_SETTINGS_IO_NOT_FOUND) {
            saw_not_found = 1;
            continue;
        }
        if (result != PJ_SETTINGS_IO_OK) {
            saw_io_error = 1;
            continue;
        }
        if (!pj_settings_decode_record(record, record_size, &candidates[slot],
                                       &generations[slot])) {
            saw_invalid = 1;
            continue;
        }
        valid[slot] = 1;
    }

    int selected = -1;
    if (valid[0] && valid[1]) {
        if (generations[0] == generations[1] &&
            memcmp(&candidates[0], &candidates[1], sizeof(candidates[0])) != 0) {
            return PJ_SETTINGS_LOAD_ERROR;
        }
        selected = generation_newer(generations[1], generations[0]) ? 1 : 0;
    } else if (valid[0]) {
        selected = 0;
    } else if (valid[1]) {
        selected = 1;
    }

    if (selected < 0) {
        return (saw_io_error || saw_invalid) ? PJ_SETTINGS_LOAD_ERROR :
               saw_not_found ? PJ_SETTINGS_LOAD_NOT_FOUND :
               PJ_SETTINGS_LOAD_ERROR;
    }

    *settings = candidates[selected];
    store->has_record = 1;
    store->active_slot = selected;
    store->generation = generations[selected];
    store->degraded = saw_io_error;
    return PJ_SETTINGS_LOAD_OK;
}

int pj_settings_store_save(pj_settings_store_t *store,
                           const pj_settings_t *settings)
{
    if (store == NULL || store->write_slot == NULL || store->degraded ||
        !pj_settings_valid(settings)) {
        return 0;
    }

    unsigned target = store->has_record ?
        (unsigned)(1 - store->active_slot) : 0u;
    uint32_t generation = store->has_record ? store->generation + 1u : 1u;
    uint8_t record[PJ_SETTINGS_RECORD_BYTES];
    if (pj_settings_encode_record(settings, generation, record,
                                  sizeof(record)) != sizeof(record) ||
        store->write_slot(store->context, target, record, sizeof(record)) !=
            PJ_SETTINGS_IO_OK) {
        return 0;
    }

    store->has_record = 1;
    store->active_slot = (int)target;
    store->generation = generation;
    return 1;
}
