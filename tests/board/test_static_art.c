#include "pj_static_art.h"
#include "pj_default_static_art.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void fill_rows(char storage[PJ_STATIC_ART_HEIGHT][PJ_STATIC_ART_WIDTH + 1],
                      const char *rows[PJ_STATIC_ART_HEIGHT])
{
    for (int y = 0; y < PJ_STATIC_ART_HEIGHT; y++) {
        memset(storage[y], '.', PJ_STATIC_ART_WIDTH);
        storage[y][PJ_STATIC_ART_WIDTH] = '\0';
        rows[y] = storage[y];
    }
}

static void test_valid_rows_and_pixel_mapping(void)
{
    char storage[PJ_STATIC_ART_HEIGHT][PJ_STATIC_ART_WIDTH + 1];
    const char *rows[PJ_STATIC_ART_HEIGHT];
    pj_static_art_t art;
    char error[80];
    fill_rows(storage, rows);
    storage[0][0] = '1';
    storage[12][37] = '#';
    storage[199][199] = '1';

    assert(pj_static_art_from_rows(200, 200, "rows", rows, 200, &art, error, sizeof(error)) == 1);
    assert(error[0] == '\0');
    assert(pj_static_art_pixel(&art, 0, 0) == 1);
    assert(pj_static_art_pixel(&art, 37, 12) == 1);
    assert(pj_static_art_pixel(&art, 199, 199) == 1);
    assert(pj_static_art_pixel(&art, 1, 0) == 0);
}

static void test_validation_errors(void)
{
    char storage[PJ_STATIC_ART_HEIGHT][PJ_STATIC_ART_WIDTH + 1];
    const char *rows[PJ_STATIC_ART_HEIGHT];
    pj_static_art_t art;
    char error[80];
    fill_rows(storage, rows);

    assert(!pj_static_art_from_rows(199, 200, "rows", rows, 200, &art, error, sizeof(error)));
    assert(strstr(error, "width") != NULL);
    assert(!pj_static_art_from_rows(200, 200, "bits", rows, 200, &art, error, sizeof(error)));
    assert(strstr(error, "encoding") != NULL);
    assert(!pj_static_art_from_rows(200, 200, "rows", rows, 199, &art, error, sizeof(error)));
    assert(strstr(error, "200 strings") != NULL);

    storage[7][9] = 'x';
    assert(!pj_static_art_from_rows(200, 200, "rows", rows, 200, &art, error, sizeof(error)));
    assert(strstr(error, "pixels") != NULL);
    storage[7][9] = '.';
    storage[8][PJ_STATIC_ART_WIDTH - 1] = '\0';
    assert(!pj_static_art_from_rows(200, 200, "rows", rows, 200, &art, error, sizeof(error)));
    assert(strstr(error, "each row") != NULL);
}

static void test_record_round_trip_and_corruption(void)
{
    pj_static_art_t art = {0};
    pj_static_art_t decoded = {0};
    uint8_t record[PJ_STATIC_ART_RECORD_BYTES];
    art.pixels[0] = 0xa5;
    art.pixels[PJ_STATIC_ART_BYTES - 1] = 0x80;

    assert(pj_static_art_encode_record(&art, record, sizeof(record)) == sizeof(record));
    assert(pj_static_art_decode_record(record, sizeof(record), &decoded) == 1);
    assert(memcmp(art.pixels, decoded.pixels, sizeof(art.pixels)) == 0);

    record[123] ^= 1u;
    assert(pj_static_art_decode_record(record, sizeof(record), &decoded) == 0);
    record[123] ^= 1u;
    record[4] = 2;
    assert(pj_static_art_decode_record(record, sizeof(record), &decoded) == 0);
    assert(pj_static_art_decode_record(record, sizeof(record) - 1, &decoded) == 0);
}

static void test_compiled_default_uses_static_art_record_format(void)
{
    pj_static_art_t art;
    pj_static_art_t decoded;
    uint8_t record[PJ_STATIC_ART_RECORD_BYTES];
    int black_pixels = 0;

    assert(PJ_DEFAULT_STATIC_ART_WIDTH == PJ_STATIC_ART_WIDTH);
    assert(PJ_DEFAULT_STATIC_ART_HEIGHT == PJ_STATIC_ART_HEIGHT);
    assert(PJ_DEFAULT_STATIC_ART_BYTES == PJ_STATIC_ART_BYTES);
    memcpy(art.pixels, pj_default_static_art, sizeof(art.pixels));
    for (int y = 0; y < PJ_STATIC_ART_HEIGHT; y++) {
        for (int x = 0; x < PJ_STATIC_ART_WIDTH; x++) {
            black_pixels += pj_static_art_pixel(&art, x, y);
        }
    }
    assert(black_pixels == PJ_DEFAULT_STATIC_ART_BLACK_PIXELS);
    assert(pj_static_art_encode_record(&art, record, sizeof(record)) == sizeof(record));
    assert(pj_static_art_decode_record(record, sizeof(record), &decoded) == 1);
    assert(memcmp(art.pixels, decoded.pixels, sizeof(art.pixels)) == 0);
}

int main(void)
{
    test_valid_rows_and_pixel_mapping();
    test_validation_errors();
    test_record_round_trip_and_corruption();
    test_compiled_default_uses_static_art_record_format();
    puts("static art tests passed");
    return 0;
}
