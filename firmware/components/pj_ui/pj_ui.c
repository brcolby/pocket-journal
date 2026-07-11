#include "pj_ui.h"
#include "pj_icon_carbon.h"
#include "pj_font_space_mono.h"

#if defined(PJ_UI_USE_LVGL)
#include "src/core/lv_obj_pos.h"
#include "src/core/lv_obj_style.h"
#include "src/core/lv_obj_style_gen.h"
#include "src/core/lv_obj_tree.h"
#include "src/core/lv_refr.h"
#include "src/display/lv_display.h"
#include "src/lv_init.h"
#include "src/misc/lv_area.h"
#include "src/misc/lv_color.h"
#include "src/widgets/bar/lv_bar.h"
#include "src/widgets/button/lv_button.h"
#include "src/widgets/canvas/lv_canvas.h"
#include "src/widgets/line/lv_line.h"
#endif

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PJ_PI 3.14159265358979323846

#if defined(PJ_UI_USE_LVGL)
#define PJ_LVGL_PALETTE_BYTES 8
#define PJ_LVGL_STRIDE_BYTES (PJ_DISPLAY_WIDTH / 8)
#define PJ_LVGL_BUFFER_BYTES (PJ_LVGL_PALETTE_BYTES + (PJ_LVGL_STRIDE_BYTES * PJ_DISPLAY_HEIGHT))

static lv_obj_t *g_lvgl_canvas;
static int g_lvgl_canvas_drawing;
static uint8_t g_lvgl_canvas_buffer[PJ_LVGL_BUFFER_BYTES];
#endif

static int lvgl_widgets_active(void)
{
#if defined(PJ_UI_USE_LVGL)
    return g_lvgl_canvas_drawing;
#else
    return 0;
#endif
}

typedef struct {
    pj_ui_state_t state;
    const char *name;
    const char *title;
    pj_ui_state_t parent;
} state_meta_t;

typedef struct {
    const char *label;
    const char *icon;
    pj_ui_state_t next;
    int x;
    int y;
    int w;
    int h;
} tile_t;

static const state_meta_t STATE_META[PJ_UI_STATE_COUNT] = {
    [PJ_UI_STATE_STATIC] = {PJ_UI_STATE_STATIC, "static", "POCKET", PJ_UI_STATE_STATIC},
    [PJ_UI_STATE_TIME_TEMP] = {PJ_UI_STATE_TIME_TEMP, "time_temp", "TIME/TEMP", PJ_UI_STATE_STATIC},
    [PJ_UI_STATE_HOME] = {PJ_UI_STATE_HOME, "home", "HOME", PJ_UI_STATE_TIME_TEMP},
    [PJ_UI_STATE_NOTES] = {PJ_UI_STATE_NOTES, "notes", "NOTES", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_RECORD] = {PJ_UI_STATE_RECORD, "record", "RECORD", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_LISTEN] = {PJ_UI_STATE_LISTEN, "listen", "LISTEN", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_READ] = {PJ_UI_STATE_READ, "read", "READ", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_TIME] = {PJ_UI_STATE_TIME, "time", "TIME", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_ALARM] = {PJ_UI_STATE_ALARM, "alarm", "ALARM", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_STOPWATCH] = {PJ_UI_STATE_STOPWATCH, "stopwatch", "STOPWATCH", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_TIMER] = {PJ_UI_STATE_TIMER, "timer", "TIMER", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_INTERVAL] = {PJ_UI_STATE_INTERVAL, "interval", "INTERVAL", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_SETTINGS] = {PJ_UI_STATE_SETTINGS, "settings", "SETTINGS", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_SYNC] = {PJ_UI_STATE_SYNC, "sync", "SYNC", PJ_UI_STATE_SETTINGS},
    [PJ_UI_STATE_VOLUME] = {PJ_UI_STATE_VOLUME, "volume", "VOLUME", PJ_UI_STATE_SETTINGS},
    [PJ_UI_STATE_CALENDAR] = {PJ_UI_STATE_CALENDAR, "calendar", "CALENDAR", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_NOTE_DETAIL] = {PJ_UI_STATE_NOTE_DETAIL, "note_detail", "NOTE", PJ_UI_STATE_HOME},
};

static const tile_t HOME_TILES[] = {
    {"", "NOTE", PJ_UI_STATE_NOTES, 8, 8, 118, 184},
    {"", "TIME", PJ_UI_STATE_TIME, 134, 8, 58, 86},
    {"", "SET", PJ_UI_STATE_SETTINGS, 134, 106, 58, 86},
};

static const tile_t NOTES_TILES[] = {
    {"", "REC", PJ_UI_STATE_RECORD, 8, 8, 184, 84},
    {"", "PLAY", PJ_UI_STATE_LISTEN, 8, 104, 88, 88},
    {"", "TXT", PJ_UI_STATE_READ, 104, 104, 88, 88},
};

static const tile_t TIME_TILES[] = {
    {"", "ALM", PJ_UI_STATE_ALARM, 8, 8, 88, 88},
    {"", "SW", PJ_UI_STATE_STOPWATCH, 104, 8, 88, 88},
    {"", "TMR", PJ_UI_STATE_TIMER, 8, 104, 88, 88},
    {"", "INT", PJ_UI_STATE_INTERVAL, 104, 104, 88, 88},
};

static const tile_t SETTINGS_TILES[] = {
    {"", "NET", PJ_UI_STATE_SYNC, 8, 8, 88, 88},
    {"", "VOL", PJ_UI_STATE_SETTINGS, 104, 8, 88, 88},
    {"", "MODE", PJ_UI_STATE_SETTINGS, 8, 104, 88, 88},
    {"", "OFF", PJ_UI_STATE_STATIC, 104, 104, 88, 88},
};

static int weekday_from_date(int year, int month, int day)
{
    if (month < 3) {
        month += 12;
        year--;
    }
    int k = year % 100;
    int j = year / 100;
    int h = (day + (13 * (month + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    return (h + 6) % 7;
}

static const char *weekday_name(int weekday)
{
    static const char *names[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    if (weekday < 0 || weekday > 6) {
        return "---";
    }
    return names[weekday];
}

static void mark_full(pj_ui_context_t *ctx)
{
    ctx->dirty.x = 0;
    ctx->dirty.y = 0;
    ctx->dirty.width = PJ_DISPLAY_WIDTH;
    ctx->dirty.height = PJ_DISPLAY_HEIGHT;
    ctx->dirty.partial = 0;
}

static void mark_partial(pj_ui_context_t *ctx, int x, int y, int width, int height)
{
    if (ctx->dirty.width > 0 && ctx->dirty.height > 0 && !ctx->dirty.partial) {
        return;
    }
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x + width > PJ_DISPLAY_WIDTH) {
        width = PJ_DISPLAY_WIDTH - x;
    }
    if (y + height > PJ_DISPLAY_HEIGHT) {
        height = PJ_DISPLAY_HEIGHT - y;
    }
    ctx->dirty.x = x;
    ctx->dirty.y = y;
    ctx->dirty.width = width > 0 ? width : 0;
    ctx->dirty.height = height > 0 ? height : 0;
    ctx->dirty.partial = 1;
}

static void set_state(pj_ui_context_t *ctx, pj_ui_state_t state)
{
    if (state >= 0 && state < PJ_UI_STATE_COUNT) {
        ctx->state = state;
        mark_full(ctx);
    }
}

static void fb_clear(pj_framebuffer_t *fb)
{
    memset(fb->pixels, 0, sizeof(fb->pixels));
#if defined(PJ_UI_USE_LVGL)
    if (g_lvgl_canvas_drawing) {
        memset(&g_lvgl_canvas_buffer[PJ_LVGL_PALETTE_BYTES], 0xFF, PJ_LVGL_STRIDE_BYTES * PJ_DISPLAY_HEIGHT);
    }
#endif
}

static void fb_set(pj_framebuffer_t *fb, int x, int y, int black)
{
    if (x < 0 || y < 0 || x >= PJ_DISPLAY_WIDTH || y >= PJ_DISPLAY_HEIGHT) {
        return;
    }
#if defined(PJ_UI_USE_LVGL)
    if (g_lvgl_canvas_drawing) {
        size_t lvgl_index = PJ_LVGL_PALETTE_BYTES + (size_t)y * PJ_LVGL_STRIDE_BYTES + (size_t)(x >> 3);
        uint8_t lvgl_mask = (uint8_t)(0x80u >> (x & 7));
        if (black) {
            g_lvgl_canvas_buffer[lvgl_index] &= (uint8_t)~lvgl_mask;
        } else {
            g_lvgl_canvas_buffer[lvgl_index] |= lvgl_mask;
        }
    }
#endif
    size_t index = (size_t)y * PJ_DISPLAY_WIDTH + (size_t)x;
    uint8_t mask = (uint8_t)(1u << (index & 7u));
    if (black) {
        fb->pixels[index >> 3u] |= mask;
    } else {
        fb->pixels[index >> 3u] &= (uint8_t)~mask;
    }
}

int pj_framebuffer_get(const pj_framebuffer_t *fb, int x, int y)
{
    if (x < 0 || y < 0 || x >= PJ_DISPLAY_WIDTH || y >= PJ_DISPLAY_HEIGHT) {
        return 0;
    }
    size_t index = (size_t)y * PJ_DISPLAY_WIDTH + (size_t)x;
    return (fb->pixels[index >> 3u] >> (index & 7u)) & 1u;
}

static void draw_hline(pj_framebuffer_t *fb, int x0, int x1, int y)
{
    for (int x = x0; x <= x1; x++) {
        fb_set(fb, x, y, 1);
    }
}

static void draw_vline(pj_framebuffer_t *fb, int x, int y0, int y1)
{
    for (int y = y0; y <= y1; y++) {
        fb_set(fb, x, y, 1);
    }
}

static void draw_rect(pj_framebuffer_t *fb, int x, int y, int w, int h)
{
    draw_hline(fb, x, x + w - 1, y);
    draw_hline(fb, x, x + w - 1, y + h - 1);
    draw_vline(fb, x, y, y + h - 1);
    draw_vline(fb, x + w - 1, y, y + h - 1);
}

static void draw_round_rect(pj_framebuffer_t *fb, int x, int y, int w, int h, int r)
{
    if (r < 1) {
        draw_rect(fb, x, y, w, h);
        return;
    }
    draw_hline(fb, x + r, x + w - r - 1, y);
    draw_hline(fb, x + r, x + w - r - 1, y + h - 1);
    draw_vline(fb, x, y + r, y + h - r - 1);
    draw_vline(fb, x + w - 1, y + r, y + h - r - 1);
    for (int yy = 0; yy <= r; yy++) {
        for (int xx = 0; xx <= r; xx++) {
            int dx = r - xx;
            int dy = r - yy;
            if (dx * dx + dy * dy <= r * r && dx * dx + dy * dy >= (r - 1) * (r - 1)) {
                fb_set(fb, x + r - dx, y + r - dy, 1);
                fb_set(fb, x + w - r - 1 + dx, y + r - dy, 1);
                fb_set(fb, x + r - dx, y + h - r - 1 + dy, 1);
                fb_set(fb, x + w - r - 1 + dx, y + h - r - 1 + dy, 1);
            }
        }
    }
}

static void draw_round_rect_width(pj_framebuffer_t *fb, int x, int y, int w, int h, int r, int width)
{
    for (int i = 0; i < width; i++) {
        draw_round_rect(fb, x + i, y + i, w - 2 * i, h - 2 * i, r > i ? r - i : 1);
    }
}

static void fill_rect(pj_framebuffer_t *fb, int x, int y, int w, int h)
{
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            fb_set(fb, xx, yy, 1);
        }
    }
}

static void fill_circle(pj_framebuffer_t *fb, int cx, int cy, int r)
{
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                fb_set(fb, cx + x, cy + y, 1);
            }
        }
    }
}

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

static int max_int(int a, int b)
{
    return a > b ? a : b;
}

static int edge_value(int ax, int ay, int bx, int by, int px, int py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void fill_triangle(pj_framebuffer_t *fb, int ax, int ay, int bx, int by, int cx, int cy)
{
    int min_x = max_int(0, min_int(ax, min_int(bx, cx)));
    int max_x = min_int(PJ_DISPLAY_WIDTH - 1, max_int(ax, max_int(bx, cx)));
    int min_y = max_int(0, min_int(ay, min_int(by, cy)));
    int max_y = min_int(PJ_DISPLAY_HEIGHT - 1, max_int(ay, max_int(by, cy)));
    int area = edge_value(ax, ay, bx, by, cx, cy);
    if (area == 0) {
        return;
    }
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int w0 = edge_value(bx, by, cx, cy, x, y);
            int w1 = edge_value(cx, cy, ax, ay, x, y);
            int w2 = edge_value(ax, ay, bx, by, x, y);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                fb_set(fb, x, y, 1);
            }
        }
    }
}

static void invert_framebuffer(pj_framebuffer_t *fb)
{
    for (size_t i = 0; i < sizeof(fb->pixels); i++) {
        fb->pixels[i] = (uint8_t)~fb->pixels[i];
    }
#if defined(PJ_UI_USE_LVGL)
    if (g_lvgl_canvas_drawing) {
        for (int y = 0; y < PJ_DISPLAY_HEIGHT; y++) {
            for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
                size_t lvgl_index = PJ_LVGL_PALETTE_BYTES + (size_t)y * PJ_LVGL_STRIDE_BYTES + (size_t)(x >> 3);
                uint8_t lvgl_mask = (uint8_t)(0x80u >> (x & 7));
                if (pj_framebuffer_get(fb, x, y)) {
                    g_lvgl_canvas_buffer[lvgl_index] &= (uint8_t)~lvgl_mask;
                } else {
                    g_lvgl_canvas_buffer[lvgl_index] |= lvgl_mask;
                }
            }
        }
    }
#endif
}

static const pj_font_size_t *font_size_for_scale(int scale)
{
    if (scale < 1) {
        scale = 1;
    } else if (scale > PJ_FONT_SPACE_MONO_SIZE_COUNT) {
        scale = PJ_FONT_SPACE_MONO_SIZE_COUNT;
    }
    return &PJ_FONT_SPACE_MONO[scale - 1];
}

static const pj_font_glyph_t *glyph_for_char(const pj_font_size_t *font_size, char c)
{
    unsigned char code = (unsigned char)c;
    if (code < PJ_FONT_SPACE_MONO_FIRST || code > PJ_FONT_SPACE_MONO_LAST) {
        code = '?';
    }
    return &font_size->glyphs[code - PJ_FONT_SPACE_MONO_FIRST];
}

static int glyph_pixel_is_set(const pj_font_glyph_t *glyph, int x, int y)
{
    int bit_index = y * glyph->width + x;
    return (glyph->data[bit_index / 8] >> (bit_index % 8)) & 1u;
}

static void draw_char(pj_framebuffer_t *fb, int x, int y, char c, int scale)
{
    const pj_font_size_t *font_size = font_size_for_scale(scale);
    const pj_font_glyph_t *glyph = glyph_for_char(font_size, c);
    int x0 = x + glyph->x_offset;
    int y0 = y + glyph->y_offset;

    for (int row = 0; row < glyph->height; row++) {
        for (int col = 0; col < glyph->width; col++) {
            if (glyph_pixel_is_set(glyph, col, row)) {
                fb_set(fb, x0 + col, y0 + row, 1);
            }
        }
    }
}

static int text_width(const char *text, int scale)
{
    const pj_font_size_t *font_size = font_size_for_scale(scale);
    int width = 0;
    while (*text != '\0') {
        width += glyph_for_char(font_size, (char)toupper((unsigned char)*text))->advance;
        text++;
    }
    return width;
}

static void draw_text(pj_framebuffer_t *fb, int x, int y, const char *text, int scale)
{
    const pj_font_size_t *font_size = font_size_for_scale(scale);
    int cursor = x;
    while (*text != '\0') {
        char display_char = (char)toupper((unsigned char)*text);
        draw_char(fb, cursor, y, display_char, scale);
        cursor += glyph_for_char(font_size, display_char)->advance;
        text++;
    }
}

static void draw_centered_text(pj_framebuffer_t *fb, int y, const char *text, int scale)
{
    int x = (PJ_DISPLAY_WIDTH - text_width(text, scale)) / 2;
    draw_text(fb, x < 0 ? 0 : x, y, text, scale);
}

static void draw_text_center_at(pj_framebuffer_t *fb, int cx, int cy, const char *text, int scale)
{
    int width = text_width(text, scale);
    draw_text(fb, cx - width / 2, cy - (scale * 5), text, scale);
}

static void draw_circle(pj_framebuffer_t *fb, int cx, int cy, int r)
{
    int x = r;
    int y = 0;
    int err = 0;
    while (x >= y) {
        fb_set(fb, cx + x, cy + y, 1);
        fb_set(fb, cx + y, cy + x, 1);
        fb_set(fb, cx - y, cy + x, 1);
        fb_set(fb, cx - x, cy + y, 1);
        fb_set(fb, cx - x, cy - y, 1);
        fb_set(fb, cx - y, cy - x, 1);
        fb_set(fb, cx + y, cy - x, 1);
        fb_set(fb, cx + x, cy - y, 1);
        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err -= 2 * x + 1;
        }
    }
}

static void draw_line_width(pj_framebuffer_t *fb, int x0, int y0, int x1, int y1, int width)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps = dx < 0 ? -dx : dx;
    int abs_dy = dy < 0 ? -dy : dy;
    if (abs_dy > steps) {
        steps = abs_dy;
    }
    if (steps == 0) {
        fill_circle(fb, x0, y0, width);
        return;
    }
    for (int i = 0; i <= steps; i++) {
        int x = x0 + (dx * i) / steps;
        int y = y0 + (dy * i) / steps;
        fill_circle(fb, x, y, width);
    }
}

static void analog_endpoint(int cx, int cy, int radius, double degrees, int *x, int *y)
{
    double radians = degrees * PJ_PI / 180.0;
    *x = cx + (int)(sin(radians) * radius);
    *y = cy - (int)(cos(radians) * radius);
}

static void draw_octagon(pj_framebuffer_t *fb, int cx, int cy, int r, int cut)
{
    int x0 = cx - r;
    int x1 = cx + r;
    int y0 = cy - r;
    int y1 = cy + r;
    draw_line_width(fb, x0 + cut, y0, x1 - cut, y0, 1);
    draw_line_width(fb, x1 - cut, y0, x1, y0 + cut, 1);
    draw_line_width(fb, x1, y0 + cut, x1, y1 - cut, 1);
    draw_line_width(fb, x1, y1 - cut, x1 - cut, y1, 1);
    draw_line_width(fb, x1 - cut, y1, x0 + cut, y1, 1);
    draw_line_width(fb, x0 + cut, y1, x0, y1 - cut, 1);
    draw_line_width(fb, x0, y1 - cut, x0, y0 + cut, 1);
    draw_line_width(fb, x0, y0 + cut, x0 + cut, y0, 1);
}

static void draw_kite_hand(pj_framebuffer_t *fb, int cx, int cy, double degrees, int inner, int middle, int outer, int middle_half)
{
    double radians = degrees * PJ_PI / 180.0;
    double sx = sin(radians);
    double sy = -cos(radians);
    double px = cos(radians);
    double py = sin(radians);
    int ix = cx + (int)(sx * inner);
    int iy = cy + (int)(sy * inner);
    int mx = cx + (int)(sx * middle);
    int my = cy + (int)(sy * middle);
    int ox = cx + (int)(sx * outer);
    int oy = cy + (int)(sy * outer);
    fill_triangle(fb, ix, iy,
                  mx + (int)(px * middle_half), my + (int)(py * middle_half),
                  mx - (int)(px * middle_half), my - (int)(py * middle_half));
    fill_triangle(fb, ox, oy,
                  mx + (int)(px * middle_half), my + (int)(py * middle_half),
                  mx - (int)(px * middle_half), my - (int)(py * middle_half));
}

static void draw_battery_indicator(pj_framebuffer_t *fb, int x, int y, int w, int h, int percent)
{
    draw_round_rect(fb, x, y, w, h, 3);
    fill_rect(fb, x + w, y + h / 3, 3, h / 3);
    int fill_w = ((w - 6) * percent) / 100;
    if (fill_w > 0) {
        fill_rect(fb, x + 3, y + 3, fill_w, h - 6);
    }
}

static void draw_icon(pj_framebuffer_t *fb, const char *icon, int cx, int cy, int size)
{
    const char *name = icon;
    if (strcmp(icon, "NOTE") == 0) {
        name = "notebook";
    } else if (strcmp(icon, "TXT") == 0) {
        name = "read_me";
    } else if (strcmp(icon, "TIME") == 0) {
        name = "time";
    } else if (strcmp(icon, "ALM") == 0) {
        name = "alarm";
    } else if (strcmp(icon, "SET") == 0) {
        name = "settings";
    } else if (strcmp(icon, "MODE") == 0) {
        name = "settings_adjust";
    } else if (strcmp(icon, "REC") == 0) {
        name = "microphone";
    } else if (strcmp(icon, "PLAY") == 0) {
        name = "document_audio";
    } else if (strcmp(icon, "SW") == 0) {
        name = "time";
    } else if (strcmp(icon, "TMR") == 0) {
        name = "timer";
    } else if (strcmp(icon, "INT") == 0) {
        name = "repeat";
    } else if (strcmp(icon, "NET") == 0) {
        name = "wifi";
    } else if (strcmp(icon, "VOL") == 0) {
        name = "volume_up";
    } else if (strcmp(icon, "OFF") == 0) {
        name = "power";
    } else if (strcmp(icon, "PLUS") == 0) {
        name = "add";
    } else if (strcmp(icon, "MINUS") == 0) {
        name = "subtract";
    } else if (strcmp(icon, "RESET") == 0) {
        name = "reset";
    }

    const pj_icon_bitmap_t *asset = NULL;
    int best_delta = 999;
    for (size_t i = 0; i < PJ_CARBON_ICON_COUNT; i++) {
        if (strcmp(PJ_CARBON_ICONS[i].name, name) != 0) {
            continue;
        }
        int delta = PJ_CARBON_ICONS[i].width > size ? PJ_CARBON_ICONS[i].width - size : size - PJ_CARBON_ICONS[i].width;
        if (delta < best_delta) {
            asset = &PJ_CARBON_ICONS[i];
            best_delta = delta;
        }
        if (delta == 0) {
            break;
        }
    }

    if (asset == NULL) {
        draw_text_center_at(fb, cx, cy, icon, 1);
        return;
    }

    int left = cx - asset->width / 2;
    int top = cy - asset->height / 2;
    for (int y = 0; y < asset->height; y++) {
        for (int x = 0; x < asset->width; x++) {
            uint8_t byte = asset->data[(size_t)y * asset->stride + (size_t)x / 8];
            if ((byte & (uint8_t)(0x80u >> (x % 8))) != 0) {
                fb_set(fb, left + x, top + y, 1);
            }
        }
    }
}

static void draw_tiles(pj_framebuffer_t *fb, const tile_t *tiles, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        int icon_size = tiles[i].w > 100 ? 58 : 40;
        if (!lvgl_widgets_active()) {
            draw_round_rect_width(fb, tiles[i].x, tiles[i].y, tiles[i].w, tiles[i].h, 12, 3);
        }
        draw_icon(fb, tiles[i].icon, tiles[i].x + tiles[i].w / 2, tiles[i].y + tiles[i].h / 2, icon_size);
    }
}

static int tile_hit(const tile_t *tiles, size_t count, int x, int y, pj_ui_state_t *next)
{
    for (size_t i = 0; i < count; i++) {
        if (x >= tiles[i].x && x < tiles[i].x + tiles[i].w &&
            y >= tiles[i].y && y < tiles[i].y + tiles[i].h) {
            *next = tiles[i].next;
            return 1;
        }
    }
    return 0;
}

void pj_ui_init(pj_ui_context_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = PJ_UI_STATE_STATIC;
    ctx->volume = 5;
    ctx->battery_percent = 84;
    ctx->temperature_c = 22;
    ctx->hour = 9;
    ctx->minute = 41;
    ctx->year = 2026;
    ctx->month = 6;
    ctx->day = 6;
    ctx->weekday = weekday_from_date(ctx->year, ctx->month, ctx->day);
    ctx->alarm_hour = 7;
    ctx->alarm_minute = 30;
    ctx->timer_seconds = 300;
    ctx->interval_seconds = 1500;
    ctx->record_state = PJ_RECORD_IDLE;
    ctx->playback_state = PJ_PLAYBACK_IDLE;
    ctx->note_count = 0;
    ctx->selected_note = 0;
    mark_full(ctx);
}

pj_ui_state_t pj_ui_current_state(const pj_ui_context_t *ctx)
{
    return ctx->state;
}

const char *pj_ui_state_name(pj_ui_state_t state)
{
    if (state < 0 || state >= PJ_UI_STATE_COUNT) {
        return "unknown";
    }
    return STATE_META[state].name;
}

pj_ui_state_t pj_ui_parent_state(pj_ui_state_t state)
{
    if (state < 0 || state >= PJ_UI_STATE_COUNT) {
        return PJ_UI_STATE_STATIC;
    }
    return STATE_META[state].parent;
}

pj_ui_dirty_region_t pj_ui_dirty_region(const pj_ui_context_t *ctx)
{
    return ctx->dirty;
}

int pj_ui_is_dirty(const pj_ui_context_t *ctx)
{
    return ctx->dirty.width > 0 && ctx->dirty.height > 0;
}

void pj_ui_mark_displayed(pj_ui_context_t *ctx)
{
    ctx->dirty.x = 0;
    ctx->dirty.y = 0;
    ctx->dirty.width = 0;
    ctx->dirty.height = 0;
    ctx->dirty.partial = 0;
}

void pj_ui_request_full_refresh(pj_ui_context_t *ctx)
{
    mark_full(ctx);
}

const char *pj_ui_default_font_name(void)
{
    return "IBM Plex Mono Bold";
}

static void clamp_volume(pj_ui_context_t *ctx)
{
    if (ctx->volume < 0) {
        ctx->volume = 0;
    } else if (ctx->volume > 10) {
        ctx->volume = 10;
    }
}

static int notes_per_page(void)
{
    return 3;
}

static int note_page_count(const pj_ui_context_t *ctx)
{
    if (ctx->note_count <= 0) {
        return 1;
    }
    return (ctx->note_count + notes_per_page() - 1) / notes_per_page();
}

static void clamp_note_page(pj_ui_context_t *ctx)
{
    int pages = note_page_count(ctx);
    if (ctx->note_page < 0) {
        ctx->note_page = 0;
    } else if (ctx->note_page >= pages) {
        ctx->note_page = pages - 1;
    }
}

void pj_ui_set_notes(pj_ui_context_t *ctx, int count, const char labels[][PJ_UI_NOTE_LABEL_LEN])
{
    if (count < 0) {
        count = 0;
    } else if (count > PJ_UI_MAX_NOTES) {
        count = PJ_UI_MAX_NOTES;
    }
    int changed = ctx->note_count != count;
    for (int i = 0; i < count && !changed; i++) {
        if (strncmp(ctx->note_labels[i], labels[i], PJ_UI_NOTE_LABEL_LEN) != 0) {
            changed = 1;
        }
    }
    if (!changed) {
        return;
    }
    ctx->note_count = count;
    for (int i = 0; i < PJ_UI_MAX_NOTES; i++) {
        ctx->note_labels[i][0] = '\0';
    }
    for (int i = 0; i < count; i++) {
        (void)snprintf(ctx->note_labels[i], PJ_UI_NOTE_LABEL_LEN, "%s", labels[i]);
    }
    clamp_note_page(ctx);
    if (ctx->selected_note >= count) {
        ctx->selected_note = count > 0 ? count - 1 : 0;
    }
    if (ctx->state == PJ_UI_STATE_LISTEN || ctx->state == PJ_UI_STATE_READ) {
        mark_full(ctx);
    }
}

void pj_ui_wake(pj_ui_context_t *ctx)
{
    set_state(ctx, PJ_UI_STATE_TIME_TEMP);
}

void pj_ui_sleep(pj_ui_context_t *ctx)
{
    ctx->record_state = PJ_RECORD_IDLE;
    ctx->playback_state = PJ_PLAYBACK_IDLE;
    ctx->stopwatch_running = 0;
    ctx->timer_running = 0;
    ctx->interval_running = 0;
    set_state(ctx, PJ_UI_STATE_STATIC);
}

void pj_ui_set_status(pj_ui_context_t *ctx, int battery_percent, int temperature_c)
{
    if (ctx->battery_percent == battery_percent && ctx->temperature_c == temperature_c) {
        return;
    }
    ctx->battery_percent = battery_percent;
    ctx->temperature_c = temperature_c;
    mark_partial(ctx, 0, ctx->state == PJ_UI_STATE_TIME_TEMP ? 146 : 0,
                 PJ_DISPLAY_WIDTH, ctx->state == PJ_UI_STATE_TIME_TEMP ? 54 : PJ_DISPLAY_HEIGHT);
}

void pj_ui_set_sync_state(pj_ui_context_t *ctx, int pending, int transferred, int online)
{
    if (pending < 0) {
        pending = 0;
    }
    if (transferred < 0) {
        transferred = 0;
    }
    online = online != 0;
    if (ctx->sync_pending == pending && ctx->sync_transferred == transferred &&
        ctx->sync_online == online) {
        return;
    }
    ctx->sync_pending = pending;
    ctx->sync_transferred = transferred;
    ctx->sync_online = online;
    if (ctx->state == PJ_UI_STATE_SYNC) {
        mark_partial(ctx, 0, 72, PJ_DISPLAY_WIDTH, 118);
    }
}

void pj_ui_set_time(pj_ui_context_t *ctx, int hour, int minute, int year, int month, int day)
{
    if (ctx->hour == hour && ctx->minute == minute && ctx->year == year && ctx->month == month && ctx->day == day) {
        return;
    }
    int date_changed = ctx->year != year || ctx->month != month || ctx->day != day;
    ctx->hour = hour;
    ctx->minute = minute;
    ctx->year = year;
    ctx->month = month;
    ctx->day = day;
    ctx->weekday = weekday_from_date(year, month, day);
    if (ctx->state == PJ_UI_STATE_TIME_TEMP) {
        if (date_changed) {
            mark_full(ctx);
        } else {
            mark_partial(ctx, 0, 0, PJ_DISPLAY_WIDTH, 132);
        }
    }
}

void pj_ui_set_audio_state(pj_ui_context_t *ctx, int recording, int playback_active)
{
    pj_record_state_t previous_record = ctx->record_state;
    pj_playback_state_t previous_playback = ctx->playback_state;
    pj_record_state_t next_record = recording ?
        (previous_record == PJ_RECORD_STOPPING ? PJ_RECORD_STOPPING : PJ_RECORD_ACTIVE) : PJ_RECORD_IDLE;
    pj_playback_state_t next_playback = playback_active ?
        (previous_playback == PJ_PLAYBACK_STOPPING ? PJ_PLAYBACK_STOPPING : PJ_PLAYBACK_ACTIVE) : PJ_PLAYBACK_IDLE;
    if (ctx->record_state == next_record && ctx->playback_state == next_playback) {
        return;
    }

    ctx->record_state = next_record;
    ctx->playback_state = next_playback;
    if (previous_record != PJ_RECORD_IDLE && next_record == PJ_RECORD_IDLE &&
        ctx->state == PJ_UI_STATE_RECORD) {
        set_state(ctx, PJ_UI_STATE_HOME);
        return;
    }
    if (previous_playback != PJ_PLAYBACK_IDLE && next_playback == PJ_PLAYBACK_IDLE &&
        ctx->playback_exit_pending &&
        (ctx->state == PJ_UI_STATE_LISTEN || ctx->state == PJ_UI_STATE_NOTE_DETAIL)) {
        ctx->playback_exit_pending = 0;
        set_state(ctx, pj_ui_parent_state(ctx->state));
        return;
    }
    if (next_playback == PJ_PLAYBACK_IDLE) {
        ctx->playback_exit_pending = 0;
    }
    if (ctx->state == PJ_UI_STATE_RECORD) {
        mark_partial(ctx, 0, 48, PJ_DISPLAY_WIDTH, 116);
    } else if (ctx->state == PJ_UI_STATE_NOTE_DETAIL || ctx->state == PJ_UI_STATE_LISTEN) {
        mark_partial(ctx, 0, 144, PJ_DISPLAY_WIDTH, 40);
    }
}

int pj_ui_handle_aux_long(pj_ui_context_t *ctx)
{
    if (ctx->state == PJ_UI_STATE_STATIC) {
        return 0;
    }
    if (ctx->state == PJ_UI_STATE_RECORD) {
        if (ctx->record_state != PJ_RECORD_ACTIVE) {
            return 0;
        }
        ctx->record_state = PJ_RECORD_STOPPING;
        set_state(ctx, PJ_UI_STATE_HOME);
        return 1;
    }
    if ((ctx->state == PJ_UI_STATE_LISTEN || ctx->state == PJ_UI_STATE_NOTE_DETAIL) &&
        ctx->playback_state != PJ_PLAYBACK_IDLE) {
        if (ctx->playback_state == PJ_PLAYBACK_STOPPING) {
            return 0;
        }
        ctx->playback_state = PJ_PLAYBACK_STOPPING;
        ctx->playback_exit_pending = 1;
        mark_partial(ctx, 0, 144, PJ_DISPLAY_WIDTH, 40);
        return 1;
    }
    set_state(ctx, pj_ui_parent_state(ctx->state));
    return 1;
}

int pj_ui_handle_aux_short(pj_ui_context_t *ctx)
{
    switch (ctx->state) {
    case PJ_UI_STATE_STATIC:
        pj_ui_wake(ctx);
        return 1;
    case PJ_UI_STATE_TIME_TEMP:
        set_state(ctx, PJ_UI_STATE_HOME);
        return 1;
    case PJ_UI_STATE_NOTES:
        ctx->record_state = PJ_RECORD_ACTIVE;
        set_state(ctx, PJ_UI_STATE_RECORD);
        return 1;
    case PJ_UI_STATE_RECORD:
        if (ctx->record_state != PJ_RECORD_ACTIVE) {
            return 0;
        }
        ctx->record_state = PJ_RECORD_STOPPING;
        set_state(ctx, PJ_UI_STATE_HOME);
        return 1;
    case PJ_UI_STATE_LISTEN:
    case PJ_UI_STATE_NOTE_DETAIL:
        if (ctx->playback_state == PJ_PLAYBACK_STOPPING) {
            return 0;
        }
        ctx->playback_state = ctx->playback_state == PJ_PLAYBACK_ACTIVE ?
            PJ_PLAYBACK_STOPPING : PJ_PLAYBACK_ACTIVE;
        ctx->playback_exit_pending = 0;
        mark_partial(ctx, 0, 144, PJ_DISPLAY_WIDTH, 40);
        return 1;
    case PJ_UI_STATE_TIME:
        ctx->stopwatch_running = 1;
        set_state(ctx, PJ_UI_STATE_STOPWATCH);
        return 1;
    case PJ_UI_STATE_ALARM:
        ctx->alarm_on = !ctx->alarm_on;
        mark_partial(ctx, 0, 92, PJ_DISPLAY_WIDTH, 34);
        return 1;
    case PJ_UI_STATE_STOPWATCH:
        ctx->stopwatch_running = !ctx->stopwatch_running;
        mark_partial(ctx, 0, 40, PJ_DISPLAY_WIDTH, 96);
        return 1;
    case PJ_UI_STATE_TIMER:
        ctx->timer_running = !ctx->timer_running;
        mark_partial(ctx, 0, 40, PJ_DISPLAY_WIDTH, 96);
        return 1;
    case PJ_UI_STATE_INTERVAL:
        ctx->interval_running = !ctx->interval_running;
        mark_partial(ctx, 0, 72, PJ_DISPLAY_WIDTH, 74);
        return 1;
    case PJ_UI_STATE_SYNC:
        return 0;
    case PJ_UI_STATE_VOLUME:
        set_state(ctx, PJ_UI_STATE_SETTINGS);
        return 1;
    default:
        return 0;
    }
}

int pj_ui_handle_aux_double(pj_ui_context_t *ctx)
{
    if (ctx->state == PJ_UI_STATE_RECORD ||
        ctx->record_state != PJ_RECORD_IDLE || ctx->playback_state != PJ_PLAYBACK_IDLE ||
        ctx->stopwatch_running || ctx->timer_running || ctx->interval_running) {
        return 0;
    }

    ctx->record_state = PJ_RECORD_ACTIVE;
    set_state(ctx, PJ_UI_STATE_RECORD);
    return 1;
}

int pj_ui_tick(pj_ui_context_t *ctx)
{
    switch (ctx->state) {
    case PJ_UI_STATE_STOPWATCH:
        if (ctx->stopwatch_running) {
            ctx->stopwatch_seconds++;
            mark_partial(ctx, 0, 40, PJ_DISPLAY_WIDTH, 96);
            return 1;
        }
        break;
    case PJ_UI_STATE_TIMER:
        if (ctx->timer_running && ctx->timer_seconds > 0) {
            ctx->timer_seconds--;
            if (ctx->timer_seconds == 0) {
                ctx->timer_running = 0;
            }
            mark_partial(ctx, 0, 40, PJ_DISPLAY_WIDTH, 96);
            return 1;
        }
        break;
    case PJ_UI_STATE_INTERVAL:
        if (ctx->interval_running && ctx->interval_seconds > 0) {
            ctx->interval_seconds--;
            if (ctx->interval_seconds == 0) {
                ctx->interval_round++;
                ctx->interval_seconds = (ctx->interval_round % 2) == 0 ? 300 : 1500;
            }
            mark_partial(ctx, 0, 24, PJ_DISPLAY_WIDTH, 120);
            return 1;
        }
        break;
    default:
        break;
    }
    return 0;
}

int pj_ui_handle_touch(pj_ui_context_t *ctx, int x, int y, pj_touch_kind_t kind)
{
    pj_ui_state_t next = ctx->state;

    if (kind == PJ_TOUCH_LONG_PRESS || kind == PJ_TOUCH_SWIPE_RIGHT) {
        return pj_ui_handle_aux_long(ctx);
    }
    if (kind != PJ_TOUCH_TAP) {
        return 0;
    }

    switch (ctx->state) {
    case PJ_UI_STATE_STATIC:
        pj_ui_wake(ctx);
        return 1;
    case PJ_UI_STATE_TIME_TEMP:
        set_state(ctx, PJ_UI_STATE_HOME);
        return 1;
    case PJ_UI_STATE_HOME:
        if (tile_hit(HOME_TILES, sizeof(HOME_TILES) / sizeof(HOME_TILES[0]), x, y, &next)) {
            set_state(ctx, next);
            return 1;
        }
        break;
    case PJ_UI_STATE_NOTES:
        if (tile_hit(NOTES_TILES, sizeof(NOTES_TILES) / sizeof(NOTES_TILES[0]), x, y, &next)) {
            if (next == PJ_UI_STATE_RECORD) {
                ctx->record_state = PJ_RECORD_ACTIVE;
            }
            set_state(ctx, next);
            return 1;
        }
        break;
    case PJ_UI_STATE_TIME:
        if (tile_hit(TIME_TILES, sizeof(TIME_TILES) / sizeof(TIME_TILES[0]), x, y, &next)) {
            set_state(ctx, next);
            return 1;
        }
        break;
    case PJ_UI_STATE_SETTINGS:
        if (tile_hit(SETTINGS_TILES, sizeof(SETTINGS_TILES) / sizeof(SETTINGS_TILES[0]), x, y, &next)) {
            if (x >= 104 && y < 96) {
                ctx->volume += y < 52 ? 1 : -1;
                clamp_volume(ctx);
                mark_partial(ctx, 104, 8, 88, 88);
                return 1;
            }
            if (x < 100 && y >= 100) {
                ctx->dark_mode = !ctx->dark_mode;
                mark_full(ctx);
                return 1;
            }
            set_state(ctx, next);
            return 1;
        }
        break;
    case PJ_UI_STATE_VOLUME:
        ctx->volume += x < 100 ? -1 : 1;
        clamp_volume(ctx);
        mark_partial(ctx, 0, 80, PJ_DISPLAY_WIDTH, 60);
        return 1;
    case PJ_UI_STATE_LISTEN:
    case PJ_UI_STATE_READ:
        if (x >= 148 && y < 92) {
            if (ctx->note_page > 0) {
                ctx->note_page--;
                mark_full(ctx);
            }
            return 1;
        }
        if (x >= 148 && y >= 108) {
            if (ctx->note_page + 1 < note_page_count(ctx)) {
                ctx->note_page++;
                mark_full(ctx);
            }
            return 1;
        }
        if (x < 148 && y >= 8 && y < 180 && ctx->note_count > 0) {
            int row = (y - 8) / 58;
            int index = ctx->note_page * notes_per_page() + row;
            if (row >= 0 && row < notes_per_page() && index < ctx->note_count) {
                ctx->selected_note = index;
                set_state(ctx, PJ_UI_STATE_NOTE_DETAIL);
            }
            return 1;
        }
        break;
    case PJ_UI_STATE_ALARM:
        if (y < 118) {
            ctx->alarm_on = !ctx->alarm_on;
            mark_partial(ctx, 0, 92, PJ_DISPLAY_WIDTH, 34);
            return 1;
        }
        if (y >= 136) {
            if (x < 50) {
                ctx->alarm_hour = (ctx->alarm_hour + 23) % 24;
            } else if (x < 100) {
                ctx->alarm_hour = (ctx->alarm_hour + 1) % 24;
            } else if (x < 150) {
                ctx->alarm_minute = (ctx->alarm_minute + 45) % 60;
            } else {
                ctx->alarm_minute = (ctx->alarm_minute + 15) % 60;
            }
            mark_partial(ctx, 0, 28, PJ_DISPLAY_WIDTH, 66);
            return 1;
        }
        break;
    case PJ_UI_STATE_STOPWATCH:
        if (y >= 138 && x >= 128) {
            ctx->stopwatch_seconds = 0;
            ctx->stopwatch_running = 0;
            mark_partial(ctx, 0, 40, PJ_DISPLAY_WIDTH, 96);
            return 1;
        }
        break;
    case PJ_UI_STATE_TIMER:
        if (y >= 138) {
            if (x < 70) {
                ctx->timer_seconds -= 30;
                if (ctx->timer_seconds < 30) {
                    ctx->timer_seconds = 30;
                }
            } else if (x < 132) {
                ctx->timer_seconds += 30;
            } else {
                ctx->timer_seconds = 300;
                ctx->timer_running = 0;
            }
            mark_partial(ctx, 0, 40, PJ_DISPLAY_WIDTH, 96);
            return 1;
        }
        break;
    case PJ_UI_STATE_INTERVAL:
        if (y >= 138) {
            if (x < 70) {
                ctx->interval_seconds -= 60;
                if (ctx->interval_seconds < 60) {
                    ctx->interval_seconds = 60;
                }
            } else if (x < 132) {
                ctx->interval_seconds += 60;
            } else {
                ctx->interval_seconds = 1500;
                ctx->interval_round = 0;
                ctx->interval_running = 0;
            }
            mark_partial(ctx, 0, 24, PJ_DISPLAY_WIDTH, 120);
            return 1;
        }
        break;
    default:
        break;
    }

    return 0;
}

static void draw_home_static(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    (void)ctx;
    draw_circle(fb, 100, 98, 66);
    draw_circle(fb, 100, 98, 58);
    fill_rect(fb, 74, 78, 12, 12);
    fill_rect(fb, 114, 78, 12, 12);
    for (int x = 66; x <= 134; x++) {
        int dx = x - 100;
        int y = 120 + (dx * dx) / 160;
        fb_set(fb, x, y, 1);
        fb_set(fb, x, y + 1, 1);
    }
    draw_round_rect(fb, 62, 164, 76, 20, 8);
}

static void draw_time_temp(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    char text[24];
    int cx = 58;
    int cy = 65;
    int r = 52;

    draw_octagon(fb, cx, cy, r, 20);
    draw_octagon(fb, cx, cy, r - 2, 18);
    for (int i = 0; i < 60; i++) {
        int x0;
        int y0;
        int x1;
        int y1;
        int major = (i % 5) == 0;
        analog_endpoint(cx, cy, major ? r - 13 : r - 7, i * 6.0, &x0, &y0);
        analog_endpoint(cx, cy, r - 3, i * 6.0, &x1, &y1);
        draw_line_width(fb, x0, y0, x1, y1, major ? 1 : 0);
    }

    double hour_degrees = (double)(ctx->hour % 12) * 30.0 + (double)ctx->minute * 0.5;
    double minute_degrees = (double)ctx->minute * 6.0;
    draw_kite_hand(fb, cx, cy, hour_degrees, 10, 22, 31, 3);
    draw_kite_hand(fb, cx, cy, minute_degrees, 13, 31, 45, 2);

    (void)snprintf(text, sizeof(text), "%02d:", ctx->hour);
    draw_text_center_at(fb, 151, 44, text, 5);
    (void)snprintf(text, sizeof(text), "%02d:", ctx->minute);
    draw_text_center_at(fb, 151, 90, text, 5);

    (void)snprintf(text, sizeof(text), "%02d%02d", ctx->month, ctx->day);
    draw_text_center_at(fb, 33, 177, text, 2);
    draw_text_center_at(fb, 82, 177, weekday_name(ctx->weekday), 2);
    (void)snprintf(text, sizeof(text), "%dC", ctx->temperature_c);
    draw_text_center_at(fb, 126, 164, text, 2);
    (void)snprintf(text, sizeof(text), "%dF", (ctx->temperature_c * 9) / 5 + 32);
    draw_text_center_at(fb, 126, 190, text, 2);
    draw_battery_indicator(fb, 162, 158, 30, 20, ctx->battery_percent);
    (void)snprintf(text, sizeof(text), "%d%%", ctx->battery_percent);
    draw_text_center_at(fb, 177, 191, text, 2);
}

static void draw_record(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    draw_icon(fb, "REC", 100, 82, ctx->record_state == PJ_RECORD_ACTIVE ? 82 : 66);
    if (ctx->record_state == PJ_RECORD_ACTIVE) {
        fill_rect(fb, 88, 136, 24, 24);
    }
}

static void draw_notes_list(const pj_ui_context_t *ctx, pj_framebuffer_t *fb, const char *kind)
{
    char text[24];
    if (ctx->note_count <= 0) {
        draw_text_center_at(fb, 78, 90, "NO NOTES", 2);
    }
    for (int i = 0; i < notes_per_page(); i++) {
        int y = 8 + i * 58;
        int note_index = ctx->note_page * notes_per_page() + i;
        if (note_index >= ctx->note_count) {
            continue;
        }
        if (!lvgl_widgets_active()) {
            draw_round_rect_width(fb, 8, y, 136, 46, 10, 2);
        }
        draw_icon(fb, strcmp(kind, "AUD") == 0 ? "PLAY" : "TXT", 28, y + 23, 24);
        draw_text(fb, 50, y + 13, ctx->note_labels[note_index][0] != '\0' ? ctx->note_labels[note_index] : "RECORDING", 1);
    }
    if (!lvgl_widgets_active()) {
        draw_round_rect_width(fb, 152, 8, 40, 78, 10, 2);
    }
    draw_icon(fb, "MINUS", 172, 47, 24);
    if (!lvgl_widgets_active()) {
        draw_round_rect_width(fb, 152, 114, 40, 78, 10, 2);
    }
    draw_icon(fb, "PLUS", 172, 153, 24);
    (void)snprintf(text, sizeof(text), "%d/%d", ctx->note_page + 1, note_page_count(ctx));
    draw_text_center_at(fb, 172, 101, text, 1);
}

static void draw_settings(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    for (size_t i = 0; i < sizeof(SETTINGS_TILES) / sizeof(SETTINGS_TILES[0]); i++) {
        const tile_t *tile = &SETTINGS_TILES[i];
        if (!lvgl_widgets_active()) {
            draw_round_rect_width(fb, tile->x, tile->y, tile->w, tile->h, 12, 3);
        }
        if (strcmp(tile->icon, "VOL") != 0) {
            draw_icon(fb, tile->icon, tile->x + tile->w / 2, tile->y + tile->h / 2, 40);
        }
    }
    if (!lvgl_widgets_active()) {
        draw_hline(fb, 107, 188, 52);
    }
    draw_icon(fb, "PLUS", 148, 29, 20);
    draw_icon(fb, "MINUS", 148, 75, 20);
    if (!lvgl_widgets_active()) {
        draw_round_rect_width(fb, 118, 42, 58, 20, 7, 2);
        int width = (ctx->volume * 48) / 10;
        if (width > 0) {
            fill_rect(fb, 123, 47, width, 10);
        }
    }
}

static void draw_sync(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    char text[20];
    draw_centered_text(fb, 34, "SYNC", 3);
    (void)snprintf(text, sizeof(text), "PENDING %d", ctx->sync_pending);
    draw_centered_text(fb, 92, text, 2);
    (void)snprintf(text, sizeof(text), "SENT %d", ctx->sync_transferred);
    draw_centered_text(fb, 126, text, 2);
    draw_centered_text(fb, 170, ctx->sync_online ? "ONLINE" : "OFFLINE", 1);
}

static void draw_change_buttons(pj_framebuffer_t *fb)
{
    if (!lvgl_widgets_active()) {
        draw_round_rect_width(fb, 8, 140, 56, 52, 12, 3);
    }
    draw_icon(fb, "MINUS", 36, 166, 26);
    if (!lvgl_widgets_active()) {
        draw_round_rect_width(fb, 72, 140, 56, 52, 12, 3);
    }
    draw_icon(fb, "PLUS", 100, 166, 26);
    if (!lvgl_widgets_active()) {
        draw_round_rect_width(fb, 136, 140, 56, 52, 12, 3);
    }
    draw_icon(fb, "RESET", 164, 166, 26);
}

static void draw_alarm_buttons(pj_framebuffer_t *fb)
{
    if (!lvgl_widgets_active()) {
        draw_round_rect_width(fb, 4, 140, 44, 52, 10, 3);
    }
    draw_text_center_at(fb, 26, 166, "H-", 2);
    if (!lvgl_widgets_active()) {
        draw_round_rect_width(fb, 52, 140, 44, 52, 10, 3);
    }
    draw_text_center_at(fb, 74, 166, "H+", 2);
    if (!lvgl_widgets_active()) {
        draw_round_rect_width(fb, 104, 140, 44, 52, 10, 3);
    }
    draw_text_center_at(fb, 126, 166, "M-", 2);
    if (!lvgl_widgets_active()) {
        draw_round_rect_width(fb, 152, 140, 44, 52, 10, 3);
    }
    draw_text_center_at(fb, 174, 166, "M+", 2);
}

static void format_hms(char *out, size_t out_size, int seconds)
{
    int hours = seconds / 3600;
    int minutes = (seconds / 60) % 60;
    int secs = seconds % 60;
    (void)snprintf(out, out_size, "%02d:%02d:%02d", hours, minutes, secs);
}

static void render_scene(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    char text[24];
    fb_clear(fb);

    switch (ctx->state) {
    case PJ_UI_STATE_STATIC:
        draw_home_static(ctx, fb);
        break;
    case PJ_UI_STATE_TIME_TEMP:
        draw_time_temp(ctx, fb);
        break;
    case PJ_UI_STATE_HOME:
        draw_tiles(fb, HOME_TILES, sizeof(HOME_TILES) / sizeof(HOME_TILES[0]));
        break;
    case PJ_UI_STATE_NOTES:
        draw_tiles(fb, NOTES_TILES, sizeof(NOTES_TILES) / sizeof(NOTES_TILES[0]));
        break;
    case PJ_UI_STATE_TIME:
        draw_tiles(fb, TIME_TILES, sizeof(TIME_TILES) / sizeof(TIME_TILES[0]));
        break;
    case PJ_UI_STATE_SETTINGS:
        draw_settings(ctx, fb);
        break;
    case PJ_UI_STATE_RECORD:
        draw_record(ctx, fb);
        break;
    case PJ_UI_STATE_LISTEN:
        draw_notes_list(ctx, fb, "AUD");
        break;
    case PJ_UI_STATE_READ:
        draw_notes_list(ctx, fb, "TXT");
        break;
    case PJ_UI_STATE_ALARM:
        (void)snprintf(text, sizeof(text), "%02d:%02d", ctx->alarm_hour, ctx->alarm_minute);
        draw_text_center_at(fb, 100, 52, text, 4);
        draw_text_center_at(fb, 100, 108, ctx->alarm_on ? "ON" : "OFF", 3);
        draw_alarm_buttons(fb);
        break;
    case PJ_UI_STATE_STOPWATCH:
        format_hms(text, sizeof(text), ctx->stopwatch_seconds);
        draw_text_center_at(fb, 100, 62, text, 3);
        draw_text_center_at(fb, 100, 116, ctx->stopwatch_running ? "RUN" : "STOP", 3);
        if (!lvgl_widgets_active()) {
            draw_round_rect_width(fb, 136, 140, 56, 52, 12, 3);
        }
        draw_icon(fb, "RESET", 164, 166, 26);
        break;
    case PJ_UI_STATE_TIMER:
        format_hms(text, sizeof(text), ctx->timer_seconds);
        draw_text_center_at(fb, 100, 62, text, 3);
        draw_text_center_at(fb, 100, 116, ctx->timer_running ? "RUN" : "STOP", 3);
        draw_change_buttons(fb);
        break;
    case PJ_UI_STATE_INTERVAL:
        (void)snprintf(text, sizeof(text), "%d", ctx->interval_round);
        draw_text_center_at(fb, 100, 34, text, 4);
        format_hms(text, sizeof(text), ctx->interval_seconds);
        draw_text_center_at(fb, 100, 90, text, 3);
        draw_text_center_at(fb, 100, 130, ctx->interval_running ? "RUN" : "STOP", 2);
        draw_change_buttons(fb);
        break;
    case PJ_UI_STATE_CALENDAR:
        (void)snprintf(text, sizeof(text), "%02d/%02d", ctx->month, ctx->day);
        draw_centered_text(fb, 22, text, 2);
        draw_text(fb, 24, 64, "09 DESIGN", 1);
        draw_text(fb, 24, 96, "11 SYNC", 1);
        draw_text(fb, 24, 128, "15 WALK", 1);
        break;
    case PJ_UI_STATE_NOTE_DETAIL:
        draw_text_center_at(fb, 100, 30,
                            ctx->note_labels[ctx->selected_note][0] != '\0' ? ctx->note_labels[ctx->selected_note] : "NOTE",
                            2);
        draw_text(fb, 18, 72, "AUDIO FILE", 1);
        draw_text(fb, 18, 94, "ON SDCARD", 1);
        draw_text(fb, 18, 116, "READY", 1);
        draw_centered_text(fb, 160,
                           ctx->playback_state == PJ_PLAYBACK_ACTIVE ? "PLAYING" :
                           ctx->playback_state == PJ_PLAYBACK_STOPPING ? "STOPPING" : "AUX PLAY",
                           1);
        break;
    case PJ_UI_STATE_SYNC:
        draw_sync(ctx, fb);
        break;
    case PJ_UI_STATE_VOLUME:
        draw_settings(ctx, fb);
        break;
    default:
        draw_centered_text(fb, 90, "UNKNOWN", 2);
        break;
    }

    if (ctx->dark_mode) {
        invert_framebuffer(fb);
    }
}

#if defined(PJ_UI_USE_LVGL)

static int g_lvgl_ready;
static lv_display_t *g_lvgl_display;
static uint8_t g_lvgl_draw_buffer[PJ_LVGL_BUFFER_BYTES];
static pj_framebuffer_t g_lvgl_stage_framebuffer;
static int g_lvgl_flush_count;
static pj_framebuffer_t *g_lvgl_flush_target;
static lv_area_t g_lvgl_flush_clip_area;
static int g_lvgl_flush_clip_valid;
static int g_lvgl_flush_overlay_only;
static int g_lvgl_flush_overlay_black;

static lv_color_t lvgl_i1_color(int black)
{
    return black ? lv_color_make(0x00, 0x00, 0x00) : lv_color_make(0xFF, 0xFF, 0xFF);
}

static lv_color32_t lvgl_palette_color(uint8_t red, uint8_t green, uint8_t blue)
{
    lv_color32_t color = {
        .blue = blue,
        .green = green,
        .red = red,
        .alpha = 0xFF,
    };
    return color;
}

static void lvgl_set_target_pixel(pj_framebuffer_t *fb, int x, int y, int black)
{
    if (fb == NULL || x < 0 || y < 0 || x >= PJ_DISPLAY_WIDTH || y >= PJ_DISPLAY_HEIGHT) {
        return;
    }
    size_t index = (size_t)y * PJ_DISPLAY_WIDTH + (size_t)x;
    uint8_t mask = (uint8_t)(1u << (index & 7u));
    if (black) {
        fb->pixels[index >> 3u] |= mask;
    } else {
        fb->pixels[index >> 3u] &= (uint8_t)~mask;
    }
}

static int lvgl_area_intersection(const lv_area_t *a, const lv_area_t *b, lv_area_t *out)
{
    out->x1 = a->x1 > b->x1 ? a->x1 : b->x1;
    out->y1 = a->y1 > b->y1 ? a->y1 : b->y1;
    out->x2 = a->x2 < b->x2 ? a->x2 : b->x2;
    out->y2 = a->y2 < b->y2 ? a->y2 : b->y2;
    return out->x1 <= out->x2 && out->y1 <= out->y2;
}

static void lvgl_copy_px_map_to_framebuffer(const lv_area_t *area, const uint8_t *px_map)
{
    lv_area_t clip;
    lv_area_t display_area;

    if (g_lvgl_flush_target == NULL || px_map == NULL) {
        return;
    }

    lv_area_set(&display_area, 0, 0, PJ_DISPLAY_WIDTH - 1, PJ_DISPLAY_HEIGHT - 1);
    if (!lvgl_area_intersection(area, &display_area, &clip)) {
        return;
    }
    if (g_lvgl_flush_clip_valid && !lvgl_area_intersection(&clip, &g_lvgl_flush_clip_area, &clip)) {
        return;
    }

    int source_width = area->x2 - area->x1 + 1;
    if (source_width <= 0) {
        return;
    }
    int source_stride = (source_width + 7) / 8;
    const uint8_t *pixels = px_map + PJ_LVGL_PALETTE_BYTES;

    for (int y = clip.y1; y <= clip.y2; y++) {
        int rel_y = y - area->y1;
        if (rel_y < 0) {
            continue;
        }
        const uint8_t *row = pixels + (size_t)rel_y * (size_t)source_stride;
        for (int x = clip.x1; x <= clip.x2; x++) {
            int rel_x = x - area->x1;
            if (rel_x < 0) {
                continue;
            }
            uint8_t mask = (uint8_t)(0x80u >> (rel_x & 7));
            int black = (row[rel_x >> 3] & mask) == 0;
            if (g_lvgl_flush_overlay_only && black != g_lvgl_flush_overlay_black) {
                continue;
            }
            lvgl_set_target_pixel(g_lvgl_flush_target, x, y, black);
        }
    }
}

static void lvgl_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    g_lvgl_flush_count++;
    lvgl_copy_px_map_to_framebuffer(area, px_map);
    lv_display_flush_ready(display);
}

static void lvgl_create_canvas(void)
{
    g_lvgl_canvas = lv_canvas_create(lv_screen_active());
    lv_obj_set_pos(g_lvgl_canvas, 0, 0);
    lv_obj_set_size(g_lvgl_canvas, PJ_DISPLAY_WIDTH, PJ_DISPLAY_HEIGHT);
    lv_canvas_set_buffer(g_lvgl_canvas, g_lvgl_canvas_buffer, PJ_DISPLAY_WIDTH, PJ_DISPLAY_HEIGHT, LV_COLOR_FORMAT_I1);
    lv_canvas_set_palette(g_lvgl_canvas, 0, lvgl_palette_color(0x00, 0x00, 0x00));
    lv_canvas_set_palette(g_lvgl_canvas, 1, lvgl_palette_color(0xFF, 0xFF, 0xFF));
    memset(&g_lvgl_canvas_buffer[PJ_LVGL_PALETTE_BYTES], 0xFF, PJ_LVGL_STRIDE_BYTES * PJ_DISPLAY_HEIGHT);
}

static void lvgl_renderer_init(void)
{
    if (g_lvgl_ready) {
        return;
    }

    lv_init();
    g_lvgl_display = lv_display_create(PJ_DISPLAY_WIDTH, PJ_DISPLAY_HEIGHT);
    lv_display_set_color_format(g_lvgl_display, LV_COLOR_FORMAT_I1);
    lv_display_set_antialiasing(g_lvgl_display, false);
    lv_display_set_flush_cb(g_lvgl_display, lvgl_flush_cb);
    lv_display_set_buffers(g_lvgl_display, g_lvgl_draw_buffer, NULL, sizeof(g_lvgl_draw_buffer),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_obj_set_style_bg_color(lv_screen_active(), lvgl_i1_color(0), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

    g_lvgl_ready = 1;
}

static void lvgl_dirty_area(const pj_ui_context_t *ctx, lv_area_t *area)
{
    if (ctx->dirty.partial && ctx->dirty.width > 0 && ctx->dirty.height > 0) {
        int x1 = ctx->dirty.x;
        int y1 = ctx->dirty.y;
        int x2 = ctx->dirty.x + ctx->dirty.width - 1;
        int y2 = ctx->dirty.y + ctx->dirty.height - 1;

        if (x1 < 0) {
            x1 = 0;
        }
        if (y1 < 0) {
            y1 = 0;
        }
        if (x2 >= PJ_DISPLAY_WIDTH) {
            x2 = PJ_DISPLAY_WIDTH - 1;
        }
        if (y2 >= PJ_DISPLAY_HEIGHT) {
            y2 = PJ_DISPLAY_HEIGHT - 1;
        }

        x1 &= ~7;
        x2 |= 7;
        if (x2 >= PJ_DISPLAY_WIDTH) {
            x2 = PJ_DISPLAY_WIDTH - 1;
        }

        if (x1 <= x2 && y1 <= y2) {
            lv_area_set(area, x1, y1, x2, y2);
            return;
        }
    }

    lv_area_set(area, 0, 0, PJ_DISPLAY_WIDTH - 1, PJ_DISPLAY_HEIGHT - 1);
}

static int lvgl_canvas_pixel_is_black(int x, int y)
{
    size_t index = PJ_LVGL_PALETTE_BYTES + (size_t)y * PJ_LVGL_STRIDE_BYTES + (size_t)(x >> 3);
    uint8_t mask = (uint8_t)(0x80u >> (x & 7));
    return (g_lvgl_canvas_buffer[index] & mask) == 0;
}

static void lvgl_copy_canvas_to_framebuffer(pj_framebuffer_t *fb, const lv_area_t *area)
{
    for (int y = area->y1; y <= area->y2; y++) {
        if (y < 0 || y >= PJ_DISPLAY_HEIGHT) {
            continue;
        }
        for (int x = area->x1; x <= area->x2; x++) {
            if (x < 0 || x >= PJ_DISPLAY_WIDTH) {
                continue;
            }
            lvgl_set_target_pixel(fb, x, y, lvgl_canvas_pixel_is_black(x, y));
        }
    }
}

static void lvgl_copy_framebuffer_area(pj_framebuffer_t *dst, const pj_framebuffer_t *src, const lv_area_t *area)
{
    for (int y = area->y1; y <= area->y2; y++) {
        if (y < 0 || y >= PJ_DISPLAY_HEIGHT) {
            continue;
        }
        for (int x = area->x1; x <= area->x2; x++) {
            if (x < 0 || x >= PJ_DISPLAY_WIDTH) {
                continue;
            }
            lvgl_set_target_pixel(dst, x, y, pj_framebuffer_get(src, x, y));
        }
    }
}

static lv_obj_t *lvgl_outline_button(int x, int y, int w, int h, int radius, int border_width, int color_index)
{
    lv_obj_t *button = lv_button_create(lv_screen_active());

    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lvgl_i1_color(color_index), LV_PART_MAIN);
    lv_obj_set_style_border_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_side(button, LV_BORDER_SIDE_FULL, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, border_width, LV_PART_MAIN);
    lv_obj_set_style_radius(button, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
    return button;
}

static void lvgl_tile_buttons(const tile_t *tiles, size_t count, int color_index)
{
    for (size_t i = 0; i < count; i++) {
        lvgl_outline_button(tiles[i].x, tiles[i].y, tiles[i].w, tiles[i].h, 12, 3, color_index);
    }
}

static void lvgl_note_list_widgets(const pj_ui_context_t *ctx, int color_index)
{
    for (int i = 0; i < notes_per_page(); i++) {
        int note_index = ctx->note_page * notes_per_page() + i;
        if (note_index >= ctx->note_count) {
            continue;
        }
        lvgl_outline_button(8, 8 + i * 58, 136, 46, 10, 2, color_index);
    }
    lvgl_outline_button(152, 8, 40, 78, 10, 2, color_index);
    lvgl_outline_button(152, 114, 40, 78, 10, 2, color_index);
}

static void lvgl_settings_widgets(const pj_ui_context_t *ctx, int color_index)
{
    static const lv_point_precise_t split_points[] = {{0, 0}, {81, 0}};

    lvgl_tile_buttons(SETTINGS_TILES, sizeof(SETTINGS_TILES) / sizeof(SETTINGS_TILES[0]), color_index);

    lv_obj_t *split = lv_line_create(lv_screen_active());
    lv_obj_remove_style_all(split);
    lv_obj_set_pos(split, 107, 52);
    lv_line_set_points(split, split_points, 2);
    lv_obj_set_style_line_color(split, lvgl_i1_color(color_index), LV_PART_MAIN);
    lv_obj_set_style_line_opa(split, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_line_width(split, 1, LV_PART_MAIN);

    lv_obj_t *bar = lv_bar_create(lv_screen_active());
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, 118, 42);
    lv_obj_set_size(bar, 58, 20);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, lvgl_i1_color(color_index), LV_PART_MAIN);
    lv_obj_set_style_border_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_FULL, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lvgl_i1_color(color_index), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, 10);
    lv_bar_set_value(bar, ctx->volume, LV_ANIM_OFF);
}

static void lvgl_change_buttons(int color_index)
{
    lvgl_outline_button(8, 140, 56, 52, 12, 3, color_index);
    lvgl_outline_button(72, 140, 56, 52, 12, 3, color_index);
    lvgl_outline_button(136, 140, 56, 52, 12, 3, color_index);
}

static void lvgl_alarm_buttons(int color_index)
{
    lvgl_outline_button(4, 140, 44, 52, 10, 3, color_index);
    lvgl_outline_button(52, 140, 44, 52, 10, 3, color_index);
    lvgl_outline_button(104, 140, 44, 52, 10, 3, color_index);
    lvgl_outline_button(152, 140, 44, 52, 10, 3, color_index);
}

static void lvgl_add_widgets(const pj_ui_context_t *ctx)
{
    int color_index = ctx->dark_mode ? 0 : 1;

    switch (ctx->state) {
    case PJ_UI_STATE_HOME:
        lvgl_tile_buttons(HOME_TILES, sizeof(HOME_TILES) / sizeof(HOME_TILES[0]), color_index);
        break;
    case PJ_UI_STATE_NOTES:
        lvgl_tile_buttons(NOTES_TILES, sizeof(NOTES_TILES) / sizeof(NOTES_TILES[0]), color_index);
        break;
    case PJ_UI_STATE_TIME:
        lvgl_tile_buttons(TIME_TILES, sizeof(TIME_TILES) / sizeof(TIME_TILES[0]), color_index);
        break;
    case PJ_UI_STATE_SETTINGS:
    case PJ_UI_STATE_VOLUME:
        lvgl_settings_widgets(ctx, color_index);
        break;
    case PJ_UI_STATE_LISTEN:
    case PJ_UI_STATE_READ:
        lvgl_note_list_widgets(ctx, color_index);
        break;
    case PJ_UI_STATE_ALARM:
        lvgl_alarm_buttons(color_index);
        break;
    case PJ_UI_STATE_STOPWATCH:
        lvgl_outline_button(136, 140, 56, 52, 12, 3, color_index);
        break;
    case PJ_UI_STATE_TIMER:
    case PJ_UI_STATE_INTERVAL:
        lvgl_change_buttons(color_index);
        break;
    default:
        break;
    }
}

void pj_ui_render(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    lv_area_t area;

    if (ctx == NULL || fb == NULL) {
        return;
    }

    lvgl_renderer_init();
    lvgl_dirty_area(ctx, &area);
    lv_obj_set_style_bg_color(lv_screen_active(), lvgl_i1_color(ctx->dark_mode ? 1 : 0), 0);

    if (!ctx->dirty.partial) {
        fb_clear(fb);
    }

    lv_obj_clean(lv_screen_active());
    lvgl_create_canvas();

    g_lvgl_canvas_drawing = 1;
    render_scene(ctx, &g_lvgl_stage_framebuffer);
    g_lvgl_canvas_drawing = 0;
    lvgl_add_widgets(ctx);
    lvgl_copy_framebuffer_area(fb, &g_lvgl_stage_framebuffer, &area);

    g_lvgl_flush_target = fb;
    g_lvgl_flush_clip_area = area;
    g_lvgl_flush_clip_valid = 1;
    g_lvgl_flush_overlay_only = 1;
    g_lvgl_flush_overlay_black = ctx->dark_mode ? 0 : 1;
    g_lvgl_flush_count = 0;
    lv_obj_invalidate_area(g_lvgl_canvas, &area);
    lv_obj_invalidate_area(lv_screen_active(), &area);
    lv_refr_now(g_lvgl_display);
    g_lvgl_flush_target = NULL;
    g_lvgl_flush_clip_valid = 0;
    g_lvgl_flush_overlay_only = 0;

    if (g_lvgl_flush_count == 0) {
        lvgl_copy_canvas_to_framebuffer(fb, &area);
    }
}

#else

void pj_ui_render(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    render_scene(ctx, fb);
}

#endif
