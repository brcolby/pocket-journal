#include "pj_home_layout.h"

#include <ctype.h>
#include <string.h>

#define PJ_HOME_RECORD_VERSION 1u
#define PJ_HOME_RECORD_CRC_OFFSET (PJ_HOME_RECORD_BYTES - 4u)

static const char *const SUPPORTED_ICONS[] = {
    "alarm", "document_audio", "microphone", "notebook", "play", "read_me",
    "repeat", "settings", "time", "timer", "volume_up", "wifi",
};

static const char *const SUPPORTED_DESTINATIONS[] = {
    "notes", "record", "listen", "read", "time", "alarm", "stopwatch",
    "timer", "interval", "settings", "sync", "volume", "calendar",
};

static int terminated(const char *value, size_t size)
{
    return value != NULL && memchr(value, '\0', size) != NULL;
}

static int display_text_valid(const char *value, size_t size)
{
    if (!terminated(value, size) || value[0] == '\0') {
        return 0;
    }
    size_t length = strlen(value);
    if (length >= size || value[0] == ' ' || value[length - 1] == ' ') {
        return 0;
    }
    for (size_t i = 0; i < length; i++) {
        unsigned char ch = (unsigned char)value[i];
        if (ch < 0x20u || ch > 0x7eu) {
            return 0;
        }
    }
    return 1;
}

static int in_catalog(const char *value, const char *const *catalog, size_t count)
{
    if (value == NULL) {
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        if (strcmp(value, catalog[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int pj_home_icon_supported(const char *icon)
{
    return in_catalog(icon, SUPPORTED_ICONS, sizeof(SUPPORTED_ICONS) / sizeof(SUPPORTED_ICONS[0]));
}

int pj_home_destination_supported(const char *destination)
{
    return in_catalog(destination, SUPPORTED_DESTINATIONS,
                      sizeof(SUPPORTED_DESTINATIONS) / sizeof(SUPPORTED_DESTINATIONS[0]));
}

void pj_home_layout_defaults(pj_home_layout_t *layout)
{
    if (layout == NULL) {
        return;
    }
    memset(layout, 0, sizeof(*layout));
    (void)strncpy(layout->title, "Pocket Journal", sizeof(layout->title) - 1u);
    layout->slot_count = 3;
    layout->slots[0] = (pj_home_slot_t) {"Notes", "notebook", "notes"};
    layout->slots[1] = (pj_home_slot_t) {"Time", "time", "time"};
    layout->slots[2] = (pj_home_slot_t) {"Settings", "settings", "settings"};
}

int pj_home_layout_valid(const pj_home_layout_t *layout)
{
    if (layout == NULL || !display_text_valid(layout->title, sizeof(layout->title)) ||
        layout->slot_count == 0 || layout->slot_count > PJ_HOME_MAX_SLOTS) {
        return 0;
    }
    for (uint8_t i = 0; i < layout->slot_count; i++) {
        const pj_home_slot_t *slot = &layout->slots[i];
        if (!display_text_valid(slot->label, sizeof(slot->label)) ||
            !terminated(slot->icon, sizeof(slot->icon)) ||
            !terminated(slot->destination, sizeof(slot->destination)) ||
            !pj_home_icon_supported(slot->icon) ||
            !pj_home_destination_supported(slot->destination)) {
            return 0;
        }
    }
    return 1;
}

int pj_home_layout_canonical_copy(pj_home_layout_t *target, const pj_home_layout_t *source)
{
    if (target == NULL || !pj_home_layout_valid(source)) {
        return 0;
    }
    pj_home_layout_t canonical;
    memset(&canonical, 0, sizeof(canonical));
    memcpy(canonical.title, source->title, strlen(source->title) + 1u);
    canonical.slot_count = source->slot_count;
    for (uint8_t i = 0; i < canonical.slot_count; i++) {
        memcpy(canonical.slots[i].label, source->slots[i].label,
               strlen(source->slots[i].label) + 1u);
        memcpy(canonical.slots[i].icon, source->slots[i].icon,
               strlen(source->slots[i].icon) + 1u);
        memcpy(canonical.slots[i].destination, source->slots[i].destination,
               strlen(source->slots[i].destination) + 1u);
    }
    *target = canonical;
    return 1;
}

static uint32_t checksum(const uint8_t *data, size_t size)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static void write_field(uint8_t *record, size_t *offset, const char *value, size_t field_size)
{
    size_t length = strlen(value);
    memcpy(record + *offset, value, length);
    *offset += field_size;
}

static int read_field(char *value, size_t value_size, const uint8_t *record, size_t *offset)
{
    if (memchr(record + *offset, '\0', value_size) == NULL) {
        return 0;
    }
    memcpy(value, record + *offset, value_size);
    *offset += value_size;
    return 1;
}

size_t pj_home_layout_encode_record(const pj_home_layout_t *layout, uint8_t *record, size_t record_size)
{
    if (!pj_home_layout_valid(layout) || record == NULL || record_size < PJ_HOME_RECORD_BYTES) {
        return 0;
    }
    memset(record, 0, PJ_HOME_RECORD_BYTES);
    record[0] = 'P';
    record[1] = 'J';
    record[2] = 'H';
    record[3] = 'L';
    record[4] = PJ_HOME_RECORD_VERSION;
    record[5] = layout->slot_count;
    size_t offset = 8;
    write_field(record, &offset, layout->title, PJ_HOME_TITLE_LEN);
    for (size_t i = 0; i < layout->slot_count; i++) {
        write_field(record, &offset, layout->slots[i].label, PJ_HOME_LABEL_LEN);
        write_field(record, &offset, layout->slots[i].icon, PJ_HOME_ICON_LEN);
        write_field(record, &offset, layout->slots[i].destination, PJ_HOME_DESTINATION_LEN);
    }
    uint32_t crc = checksum(record, PJ_HOME_RECORD_CRC_OFFSET);
    record[PJ_HOME_RECORD_CRC_OFFSET] = (uint8_t)crc;
    record[PJ_HOME_RECORD_CRC_OFFSET + 1u] = (uint8_t)(crc >> 8u);
    record[PJ_HOME_RECORD_CRC_OFFSET + 2u] = (uint8_t)(crc >> 16u);
    record[PJ_HOME_RECORD_CRC_OFFSET + 3u] = (uint8_t)(crc >> 24u);
    return PJ_HOME_RECORD_BYTES;
}

int pj_home_layout_decode_record(const uint8_t *record, size_t record_size, pj_home_layout_t *layout)
{
    if (record == NULL || layout == NULL || record_size != PJ_HOME_RECORD_BYTES ||
        record[0] != 'P' || record[1] != 'J' || record[2] != 'H' || record[3] != 'L' ||
        record[4] != PJ_HOME_RECORD_VERSION) {
        return 0;
    }
    uint32_t expected = (uint32_t)record[PJ_HOME_RECORD_CRC_OFFSET] |
                        ((uint32_t)record[PJ_HOME_RECORD_CRC_OFFSET + 1u] << 8u) |
                        ((uint32_t)record[PJ_HOME_RECORD_CRC_OFFSET + 2u] << 16u) |
                        ((uint32_t)record[PJ_HOME_RECORD_CRC_OFFSET + 3u] << 24u);
    if (checksum(record, PJ_HOME_RECORD_CRC_OFFSET) != expected) {
        return 0;
    }
    pj_home_layout_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    decoded.slot_count = record[5];
    size_t offset = 8;
    if (!read_field(decoded.title, sizeof(decoded.title), record, &offset)) {
        return 0;
    }
    for (size_t i = 0; i < PJ_HOME_MAX_SLOTS; i++) {
        if (!read_field(decoded.slots[i].label, sizeof(decoded.slots[i].label), record, &offset) ||
            !read_field(decoded.slots[i].icon, sizeof(decoded.slots[i].icon), record, &offset) ||
            !read_field(decoded.slots[i].destination, sizeof(decoded.slots[i].destination), record, &offset)) {
            return 0;
        }
    }
    if (!pj_home_layout_valid(&decoded)) {
        return 0;
    }
    *layout = decoded;
    return 1;
}

int pj_home_layout_decode_or_default(const uint8_t *record, size_t record_size, pj_home_layout_t *layout)
{
    if (pj_home_layout_decode_record(record, record_size, layout)) {
        return 1;
    }
    pj_home_layout_defaults(layout);
    return 0;
}
