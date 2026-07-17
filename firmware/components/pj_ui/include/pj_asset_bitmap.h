#pragma once

#include <stddef.h>
#include <stdint.h>

#define PJ_ASSET_ENCODING_ROW_MAJOR_MSB_FIRST 1

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    const uint8_t *data;
} pj_asset_bitmap_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint16_t advance;
    int16_t x_offset;
    int16_t y_offset;
    const uint8_t *data;
} pj_asset_glyph_t;
