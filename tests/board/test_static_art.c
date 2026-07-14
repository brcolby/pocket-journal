#include "pj_static_art.h"
#include "pj_static_art_ui.h"
#include "pj_default_static_art.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int published_set_count;
static int published_clear_count;
static pj_ui_context_t *published_ui;
static uint8_t published_pixels[PJ_STATIC_ART_BYTES];

void pj_ui_set_static_art(pj_ui_context_t *ui, const uint8_t *pixels, size_t pixel_bytes)
{
    assert(ui != NULL);
    assert(pixels != NULL);
    assert(pixel_bytes == sizeof(published_pixels));
    published_ui = ui;
    memcpy(published_pixels, pixels, sizeof(published_pixels));
    published_set_count++;
}

void pj_ui_clear_static_art(pj_ui_context_t *ui)
{
    assert(ui != NULL);
    published_ui = ui;
    published_clear_count++;
}

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

static void test_board_art_publication_sets_or_clears_ui(void)
{
    pj_ui_context_t ui = {0};
    pj_static_art_t art = {0};
    art.pixels[0] = 0xa5;
    art.pixels[PJ_STATIC_ART_BYTES - 1] = 0x80;

    pj_static_art_publish_to_ui(&ui, &art);
    assert(published_set_count == 1);
    assert(published_clear_count == 0);
    assert(published_ui == &ui);
    assert(memcmp(published_pixels, art.pixels, sizeof(art.pixels)) == 0);

    pj_static_art_publish_to_ui(&ui, NULL);
    assert(published_set_count == 1);
    assert(published_clear_count == 1);
    assert(published_ui == &ui);

    pj_static_art_publish_to_ui(NULL, &art);
    assert(published_set_count == 1);
    assert(published_clear_count == 1);
}

int main(void)
{
    test_valid_rows_and_pixel_mapping();
    test_validation_errors();
    test_record_round_trip_and_corruption();
    test_compiled_default_uses_static_art_record_format();
    test_board_art_publication_sets_or_clears_ui();
    puts("static art tests passed");
    return 0;
}
