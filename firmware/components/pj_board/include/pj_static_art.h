#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_STATIC_ART_WIDTH 200
#define PJ_STATIC_ART_HEIGHT 200
#define PJ_STATIC_ART_BYTES ((PJ_STATIC_ART_WIDTH * PJ_STATIC_ART_HEIGHT) / 8)
#define PJ_STATIC_ART_RECORD_BYTES (12 + PJ_STATIC_ART_BYTES)

typedef struct {
    uint8_t pixels[PJ_STATIC_ART_BYTES];
} pj_static_art_t;

int pj_static_art_from_rows(int width, int height, const char *encoding,
                            const char *const rows[], size_t row_count,
                            pj_static_art_t *art, char *error, size_t error_size);
int pj_static_art_pixel(const pj_static_art_t *art, int x, int y);
size_t pj_static_art_encode_record(const pj_static_art_t *art, uint8_t *record, size_t record_size);
int pj_static_art_decode_record(const uint8_t *record, size_t record_size, pj_static_art_t *art);

#ifdef __cplusplus
}
#endif
