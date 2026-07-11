#include "pj_static_art.h"

#include <stdio.h>
#include <string.h>

#define PJ_STATIC_ART_RECORD_VERSION 1u

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

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

int pj_static_art_from_rows(int width, int height, const char *encoding,
                            const char *const rows[], size_t row_count,
                            pj_static_art_t *art, char *error, size_t error_size)
{
    if (art == NULL) {
        set_error(error, error_size, "missing output buffer");
        return 0;
    }
    if (width != PJ_STATIC_ART_WIDTH || height != PJ_STATIC_ART_HEIGHT) {
        set_error(error, error_size, "width and height must be 200");
        return 0;
    }
    if (encoding == NULL || strcmp(encoding, "rows") != 0) {
        set_error(error, error_size, "encoding must be rows");
        return 0;
    }
    if (rows == NULL || row_count != PJ_STATIC_ART_HEIGHT) {
        set_error(error, error_size, "rows must contain exactly 200 strings");
        return 0;
    }

    memset(art->pixels, 0, sizeof(art->pixels));
    for (int y = 0; y < PJ_STATIC_ART_HEIGHT; y++) {
        const char *row = rows[y];
        if (row == NULL || strlen(row) != PJ_STATIC_ART_WIDTH) {
            set_error(error, error_size, "each row must contain exactly 200 pixels");
            return 0;
        }
        for (int x = 0; x < PJ_STATIC_ART_WIDTH; x++) {
            char pixel = row[x];
            if (pixel != '0' && pixel != '.' && pixel != '1' && pixel != '#') {
                set_error(error, error_size, "pixels must use only 0, 1, . or #");
                return 0;
            }
            if (pixel == '1' || pixel == '#') {
                size_t index = (size_t)y * PJ_STATIC_ART_WIDTH + (size_t)x;
                art->pixels[index >> 3u] |= (uint8_t)(1u << (index & 7u));
            }
        }
    }
    set_error(error, error_size, "");
    return 1;
}

int pj_static_art_pixel(const pj_static_art_t *art, int x, int y)
{
    if (art == NULL || x < 0 || y < 0 || x >= PJ_STATIC_ART_WIDTH || y >= PJ_STATIC_ART_HEIGHT) {
        return 0;
    }
    size_t index = (size_t)y * PJ_STATIC_ART_WIDTH + (size_t)x;
    return (art->pixels[index >> 3u] >> (index & 7u)) & 1u;
}

size_t pj_static_art_encode_record(const pj_static_art_t *art, uint8_t *record, size_t record_size)
{
    if (art == NULL || record == NULL || record_size < PJ_STATIC_ART_RECORD_BYTES) {
        return 0;
    }
    memcpy(record, "PJAR", 4);
    record[4] = PJ_STATIC_ART_RECORD_VERSION;
    record[5] = 0;
    record[6] = 0;
    record[7] = 0;
    put_u32_le(record + 8, crc32(art->pixels, sizeof(art->pixels)));
    memcpy(record + 12, art->pixels, sizeof(art->pixels));
    return PJ_STATIC_ART_RECORD_BYTES;
}

int pj_static_art_decode_record(const uint8_t *record, size_t record_size, pj_static_art_t *art)
{
    if (record == NULL || art == NULL || record_size != PJ_STATIC_ART_RECORD_BYTES ||
        memcmp(record, "PJAR", 4) != 0 || record[4] != PJ_STATIC_ART_RECORD_VERSION ||
        record[5] != 0 || record[6] != 0 || record[7] != 0) {
        return 0;
    }
    const uint8_t *pixels = record + 12;
    if (get_u32_le(record + 8) != crc32(pixels, PJ_STATIC_ART_BYTES)) {
        return 0;
    }
    memcpy(art->pixels, pixels, sizeof(art->pixels));
    return 1;
}
