#include "pj_ui.h"
#include "pj_default_static_art.h"
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

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define PJ_UI_MAX_DURATION_SECONDS 86400
#define PJ_UI_NOTES_PER_PAGE 3
#define PJ_UI_NOTE_PAGER_TOP 150
#define PJ_UI_BUTTON_BORDER_WIDTH 3
#define PJ_UI_TIME_CONTROLS_TOP 100
#define PJ_UI_ALARM_TOGGLE_TOP 72
#define PJ_UI_ALARM_CONTROLS_TOP 112

_Static_assert(PJ_DEFAULT_STATIC_ART_WIDTH == PJ_DISPLAY_WIDTH,
               "default static art width must match the display");
_Static_assert(PJ_DEFAULT_STATIC_ART_HEIGHT == PJ_DISPLAY_HEIGHT,
               "default static art height must match the display");
_Static_assert(PJ_DEFAULT_STATIC_ART_BYTES == PJ_FRAMEBUFFER_BYTES,
               "default static art packing must match the framebuffer");

#if defined(PJ_UI_USE_LVGL)
#define PJ_LVGL_PALETTE_BYTES 8
#define PJ_LVGL_STRIDE_BYTES (PJ_DISPLAY_WIDTH / 8)
#define PJ_LVGL_BUFFER_BYTES (PJ_LVGL_PALETTE_BYTES + (PJ_LVGL_STRIDE_BYTES * PJ_DISPLAY_HEIGHT))

static lv_obj_t *g_lvgl_canvas;
static int g_lvgl_canvas_drawing;
static uint8_t g_lvgl_canvas_buffer[PJ_LVGL_BUFFER_BYTES];
#endif

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

typedef struct {
    int x;
    int y;
    int w;
    int h;
} ui_rect_t;

static const state_meta_t STATE_META[PJ_UI_STATE_COUNT] = {
    [PJ_UI_STATE_STATIC] = {PJ_UI_STATE_STATIC, "static", "POCKET", PJ_UI_STATE_STATIC},
    [PJ_UI_STATE_TIME_TEMP] = {PJ_UI_STATE_TIME_TEMP, "time_temp", "TIME/TEMP", PJ_UI_STATE_STATIC},
    [PJ_UI_STATE_HOME] = {PJ_UI_STATE_HOME, "home", "HOME", PJ_UI_STATE_TIME_TEMP},
    [PJ_UI_STATE_NOTES] = {PJ_UI_STATE_NOTES, "notes", "NOTES", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_RECORD] = {PJ_UI_STATE_RECORD, "record", "Record", PJ_UI_STATE_NOTES},
    [PJ_UI_STATE_LISTEN] = {PJ_UI_STATE_LISTEN, "listen", "Listen", PJ_UI_STATE_NOTES},
    [PJ_UI_STATE_READ] = {PJ_UI_STATE_READ, "read", "Read", PJ_UI_STATE_NOTES},
    [PJ_UI_STATE_TIME] = {PJ_UI_STATE_TIME, "time", "TIME", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_ALARM] = {PJ_UI_STATE_ALARM, "alarm", "Alarm", PJ_UI_STATE_TIME},
    [PJ_UI_STATE_STOPWATCH] = {PJ_UI_STATE_STOPWATCH, "stopwatch", "Stopwatch", PJ_UI_STATE_TIME},
    [PJ_UI_STATE_TIMER] = {PJ_UI_STATE_TIMER, "timer", "Timer", PJ_UI_STATE_TIME},
    [PJ_UI_STATE_INTERVAL] = {PJ_UI_STATE_INTERVAL, "interval", "Interval", PJ_UI_STATE_TIME},
    [PJ_UI_STATE_SETTINGS] = {PJ_UI_STATE_SETTINGS, "settings", "SETTINGS", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_SYNC] = {PJ_UI_STATE_SYNC, "sync", "SYNC", PJ_UI_STATE_SETTINGS},
    [PJ_UI_STATE_VOLUME] = {PJ_UI_STATE_VOLUME, "volume", "VOLUME", PJ_UI_STATE_SETTINGS},
    [PJ_UI_STATE_DISPLAY] = {PJ_UI_STATE_DISPLAY, "display", "DISPLAY", PJ_UI_STATE_SETTINGS},
    [PJ_UI_STATE_CALENDAR] = {PJ_UI_STATE_CALENDAR, "calendar", "CALENDAR", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_NOTE_DETAIL] = {PJ_UI_STATE_NOTE_DETAIL, "note_detail", "Note", PJ_UI_STATE_LISTEN},
};

static const tile_t NOTES_TILES[] = {
    {"Record", "REC", PJ_UI_STATE_RECORD, 0, 0, 200, 67},
    {"Listen", "PLAY", PJ_UI_STATE_LISTEN, 0, 67, 200, 66},
    {"Read", "TXT", PJ_UI_STATE_READ, 0, 133, 200, 67},
};

static const tile_t TIME_TILES[] = {
    {"Alarm", "ALM", PJ_UI_STATE_ALARM, 0, 0, 100, 100},
    {"Stopwatch", "SW", PJ_UI_STATE_STOPWATCH, 100, 0, 100, 100},
    {"Timer", "TMR", PJ_UI_STATE_TIMER, 0, 100, 100, 100},
    {"Interval", "INT", PJ_UI_STATE_INTERVAL, 100, 100, 100, 100},
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

static pj_ui_state_t state_from_destination(const char *destination)
{
    if (destination == NULL) {
        return PJ_UI_STATE_HOME;
    }
    for (int state = PJ_UI_STATE_NOTES; state < PJ_UI_STATE_NOTE_DETAIL; state++) {
        if (strcmp(destination, STATE_META[state].name) == 0) {
            return (pj_ui_state_t)state;
        }
    }
    return PJ_UI_STATE_HOME;
}

static size_t home_tiles(const pj_ui_context_t *ctx, tile_t tiles[PJ_HOME_MAX_SLOTS])
{
    size_t count = ctx->home_layout.slot_count;
    if (count > PJ_HOME_MAX_SLOTS) {
        count = PJ_HOME_MAX_SLOTS;
    }
    for (size_t i = 0; i < count; i++) {
        int y = (int)(i * PJ_DISPLAY_HEIGHT / count);
        int next_y = (int)((i + 1u) * PJ_DISPLAY_HEIGHT / count);
        tiles[i] = (tile_t) {
            .label = ctx->home_layout.slots[i].label,
            .icon = ctx->home_layout.slots[i].icon,
            .next = state_from_destination(ctx->home_layout.slots[i].destination),
            .x = 0,
            .y = y,
            .w = PJ_DISPLAY_WIDTH,
            .h = next_y - y,
        };
    }
    return count;
}

static const char *weekday_name(int weekday)
{
    static const char *names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
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
    if (width <= 0 || height <= 0) {
        return;
    }
    if (ctx->dirty.partial && ctx->dirty.width > 0 && ctx->dirty.height > 0) {
        int x1 = ctx->dirty.x < x ? ctx->dirty.x : x;
        int y1 = ctx->dirty.y < y ? ctx->dirty.y : y;
        int x2 = ctx->dirty.x + ctx->dirty.width > x + width ?
            ctx->dirty.x + ctx->dirty.width : x + width;
        int y2 = ctx->dirty.y + ctx->dirty.height > y + height ?
            ctx->dirty.y + ctx->dirty.height : y + height;
        ctx->dirty.x = x1;
        ctx->dirty.y = y1;
        ctx->dirty.width = x2 - x1;
        ctx->dirty.height = y2 - y1;
    } else {
        ctx->dirty.x = x;
        ctx->dirty.y = y;
        ctx->dirty.width = width;
        ctx->dirty.height = height;
    }
    ctx->dirty.partial = 1;
}

static void mark_dynamic_partial(pj_ui_context_t *ctx, int x, int y, int width, int height)
{
    mark_partial(ctx, x, y, width, height);
}

static void set_state(pj_ui_context_t *ctx, pj_ui_state_t state)
{
    if (state >= 0 && state < PJ_UI_STATE_COUNT) {
        ctx->state = state;
        ctx->focus_index = (state == PJ_UI_STATE_LISTEN || state == PJ_UI_STATE_READ) ?
            ctx->note_page * PJ_UI_NOTES_PER_PAGE : 0;
        ctx->focus_idle_ticks = 0;
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
    for (int inset = 0; inset < PJ_UI_BUTTON_BORDER_WIDTH; inset++) {
        draw_hline(fb, x, x + w - 1, y + inset);
        draw_hline(fb, x, x + w - 1, y + h - 1 - inset);
        draw_vline(fb, x + inset, y, y + h - 1);
        draw_vline(fb, x + w - 1 - inset, y, y + h - 1);
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

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

static int max_int(int a, int b)
{
    return a > b ? a : b;
}

static ui_rect_t timer_control_rect(int index)
{
    int row = index < 2 ? 0 : 1;
    int column = index % 2;
    int top = PJ_UI_TIME_CONTROLS_TOP +
        row * (PJ_DISPLAY_HEIGHT - PJ_UI_TIME_CONTROLS_TOP) / 2;
    int bottom = PJ_UI_TIME_CONTROLS_TOP +
        (row + 1) * (PJ_DISPLAY_HEIGHT - PJ_UI_TIME_CONTROLS_TOP) / 2;
    return (ui_rect_t) {
        .x = column * PJ_DISPLAY_WIDTH / 2,
        .y = top,
        .w = PJ_DISPLAY_WIDTH / 2,
        .h = bottom - top,
    };
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

static char display_char(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - ('a' - 'A')) : c;
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
        width += glyph_for_char(font_size, display_char(*text))->advance;
        text++;
    }
    return width;
}

static void draw_text(pj_framebuffer_t *fb, int x, int y, const char *text, int scale)
{
    const pj_font_size_t *font_size = font_size_for_scale(scale);
    int cursor = x;
    while (*text != '\0') {
        char c = display_char(*text);
        draw_char(fb, cursor, y, c, scale);
        cursor += glyph_for_char(font_size, c)->advance;
        text++;
    }
}

static void ellipsize_text(char *output, size_t output_size,
                           const char *text, int scale, int max_width)
{
    if (output_size == 0) {
        return;
    }
    output[0] = '\0';
    if (text_width(text, scale) <= max_width) {
        (void)snprintf(output, output_size, "%s", text);
        return;
    }
    if (output_size < 4u) {
        return;
    }

    size_t length = 0;
    int ellipsis_width = text_width("...", scale);
    while (text[length] != '\0' && length < output_size - 4u) {
        output[length] = text[length];
        output[length + 1u] = '\0';
        if (text_width(output, scale) + ellipsis_width > max_width) {
            output[length] = '\0';
            break;
        }
        length++;
    }
    memcpy(output + length, "...", 4u);
}

static void draw_centered_text(pj_framebuffer_t *fb, int y, const char *text, int scale)
{
    int x = (PJ_DISPLAY_WIDTH - text_width(text, scale)) / 2;
    draw_text(fb, x < 0 ? 0 : x, y, text, scale);
}

static void draw_text_center_at(pj_framebuffer_t *fb, int cx, int cy, const char *text, int scale)
{
    const pj_font_size_t *font_size = font_size_for_scale(scale);
    int width = text_width(text, scale);
    draw_text(fb, cx - width / 2, cy - font_size->line_height / 2, text, scale);
}

static void draw_text_center_at_double(pj_framebuffer_t *fb, int cx, int cy,
                                       const char *text, int scale)
{
    const pj_font_size_t *font_size = font_size_for_scale(scale);
    int cursor = cx - text_width(text, scale);
    int top = cy - font_size->line_height;
    while (*text != '\0') {
        const pj_font_glyph_t *glyph = glyph_for_char(font_size, display_char(*text));
        for (int row = 0; row < glyph->height; row++) {
            for (int col = 0; col < glyph->width; col++) {
                if (glyph_pixel_is_set(glyph, col, row)) {
                    fill_rect(fb, cursor + 2 * (glyph->x_offset + col),
                              top + 2 * (glyph->y_offset + row), 2, 2);
                }
            }
        }
        cursor += 2 * glyph->advance;
        text++;
    }
}

static void draw_text_centered_ellipsized(pj_framebuffer_t *fb, int cx, int cy,
                                          const char *text, int scale, int max_width)
{
    char clipped[PJ_UI_NOTE_LABEL_LEN];
    ellipsize_text(clipped, sizeof(clipped), text, scale, max_width);
    draw_text_center_at(fb, cx, cy, clipped, scale);
}

static void append_line_ellipsis(char *line, size_t line_size, int scale,
                                 int max_width, int split_word)
{
    size_t length = strlen(line);
    if (split_word) {
        char *space = strrchr(line, ' ');
        if (space != NULL && space > line) {
            *space = '\0';
            length = (size_t)(space - line);
        }
    }
    while (length > 0 && line[length - 1u] == ' ') {
        line[--length] = '\0';
    }
    int ellipsis_width = text_width("...", scale);
    while (length > 0 &&
           (length + 4u > line_size || text_width(line, scale) + ellipsis_width > max_width)) {
        line[--length] = '\0';
    }
    if (length + 4u <= line_size) {
        memcpy(line + length, "...", 4u);
    }
}

static void draw_wrapped_text(pj_framebuffer_t *fb, int x, int y, const char *text,
                              int scale, int max_width, int max_lines)
{
    char line[PJ_UI_NOTE_LABEL_LEN];
    size_t used = 0;
    int line_number = 0;
    const pj_font_size_t *font_size = font_size_for_scale(scale);

    while (*text != '\0' && line_number < max_lines) {
        if (*text == ' ' && used == 0) {
            text++;
            continue;
        }
        line[used] = *text;
        line[used + 1u] = '\0';
        if (text_width(line, scale) > max_width && used > 0) {
            line[used] = '\0';
            if (line_number == max_lines - 1) {
                append_line_ellipsis(line, sizeof(line), scale, max_width,
                                     *text != ' ' && line[used - 1u] != ' ');
                draw_text(fb, x, y + line_number * (font_size->line_height + 5), line, scale);
                return;
            }
            char *space = strrchr(line, ' ');
            if (space != NULL) {
                *space = '\0';
                size_t remaining = strlen(space + 1);
                draw_text(fb, x, y + line_number * (font_size->line_height + 5), line, scale);
                memmove(line, space + 1, remaining + 1u);
                used = remaining;
            } else {
                draw_text(fb, x, y + line_number * (font_size->line_height + 5), line, scale);
                used = 0;
            }
            line_number++;
            continue;
        }
        used++;
        text++;
        if (used + 1u >= sizeof(line)) {
            break;
        }
    }
    if (used > 0 && line_number < max_lines) {
        line[used] = '\0';
        if (*text != '\0') {
            append_line_ellipsis(line, sizeof(line), scale, max_width,
                                 *text != ' ' && line[used - 1u] != ' ');
        }
        draw_text(fb, x, y + line_number * (font_size->line_height + 5), line, scale);
    }
}

static void draw_icon(pj_framebuffer_t *fb, const char *icon, int cx, int cy, int size)
{
    if (strcmp(icon, "LEFT") == 0 || strcmp(icon, "RIGHT") == 0) {
        int half = max_int(10, size / 3);
        int thickness = max_int(3, size / 9);
        int points_right = strcmp(icon, "RIGHT") == 0;
        for (int offset = 0; offset <= half; offset++) {
            int x = points_right ? cx + half - offset : cx - half + offset;
            fill_rect(fb, x - thickness / 2, cy - offset - thickness / 2,
                      thickness, thickness);
            fill_rect(fb, x - thickness / 2, cy + offset - thickness / 2,
                      thickness, thickness);
        }
        return;
    }

    if (strcmp(icon, "PLUS") == 0 || strcmp(icon, "MINUS") == 0) {
        int length = max_int(22, size);
        int thickness = max_int(6, size / 5);
        fill_rect(fb, cx - length / 2, cy - thickness / 2, length, thickness);
        if (strcmp(icon, "PLUS") == 0) {
            fill_rect(fb, cx - thickness / 2, cy - length / 2, thickness, length);
        }
        return;
    }

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
    } else if (strcmp(icon, "RESET") == 0) {
        name = "reset";
    } else if (strcmp(icon, "START") == 0) {
        name = "play";
    } else if (strcmp(icon, "PAUSE") == 0) {
        name = "pause";
    } else if (strcmp(icon, "STOP") == 0) {
        name = "stop";
    } else if (strcmp(icon, "SNOOZE") == 0) {
        name = "time";
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

    int pixel_scale = size >= asset->width * 2 ? 2 : 1;
    int left = cx - asset->width * pixel_scale / 2;
    int top = cy - asset->height * pixel_scale / 2;
    for (int y = 0; y < asset->height; y++) {
        for (int x = 0; x < asset->width; x++) {
            uint8_t byte = asset->data[(size_t)y * asset->stride + (size_t)x / 8];
            if ((byte & (uint8_t)(0x80u >> (x % 8))) != 0) {
                fill_rect(fb, left + x * pixel_scale, top + y * pixel_scale,
                          pixel_scale, pixel_scale);
            }
        }
    }
}

static void draw_icon_menu(pj_framebuffer_t *fb, const tile_t *tiles, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        const tile_t *tile = &tiles[i];
        draw_rect(fb, tile->x, tile->y, tile->w, tile->h);
        int icon_x = tile->x + tile->w / 2;
        int icon_y = tile->y + tile->h / 2;
        int icon_size = min_int(tile->w, tile->h) >= 90 ? 66 : 58;
        draw_icon(fb, tile->icon, icon_x, icon_y, icon_size);
    }
}

static void draw_adjustment_controls(pj_framebuffer_t *fb, int top)
{
    const char *units[] = {"HR", "HR", "MIN", "MIN"};
    const char *actions[] = {"MINUS", "PLUS", "MINUS", "PLUS"};
    for (int i = 0; i < 4; i++) {
        int column = i % 2;
        int row = i / 2;
        int x = column * PJ_DISPLAY_WIDTH / 2;
        int next_x = (column + 1) * PJ_DISPLAY_WIDTH / 2;
        int y = top + row * (PJ_DISPLAY_HEIGHT - top) / 2;
        int next_y = top + (row + 1) * (PJ_DISPLAY_HEIGHT - top) / 2;
        draw_rect(fb, x, y, next_x - x, next_y - y);
        draw_icon(fb, actions[i], x + 22, (y + next_y) / 2,
                  min_int(34, next_y - y - 8));
        draw_text_center_at(fb, x + 66, (y + next_y) / 2, units[i], 3);
    }
}

static void draw_icon_controls(pj_framebuffer_t *fb, const char *const icons[],
                               int count, int top, int columns)
{
    int rows = (count + columns - 1) / columns;
    for (int i = 0; i < count; i++) {
        int column = i % columns;
        int row = i / columns;
        int x = column * PJ_DISPLAY_WIDTH / columns;
        int next_x = (column + 1) * PJ_DISPLAY_WIDTH / columns;
        int y = top + row * (PJ_DISPLAY_HEIGHT - top) / rows;
        int next_y = top + (row + 1) * (PJ_DISPLAY_HEIGHT - top) / rows;
        draw_rect(fb, x, y, next_x - x, next_y - y);
        draw_icon(fb, icons[i], (x + next_x) / 2, (y + next_y) / 2,
                  min_int(next_x - x, next_y - y) - 12);
    }
}

static void draw_timer_controls(pj_framebuffer_t *fb, const char *play_icon)
{
    const char *icons[] = {play_icon, "RESET", "MINUS", "PLUS"};
    for (int i = 0; i < 4; i++) {
        ui_rect_t control = timer_control_rect(i);
        draw_rect(fb, control.x, control.y, control.w, control.h);
        draw_icon(fb, icons[i], control.x + control.w / 2,
                  control.y + control.h / 2,
                  min_int(control.w, control.h) - 10);
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
    ctx->humidity_percent = 45;
    ctx->clock_24h = 1;
    ctx->temperature_fahrenheit = 0;
    ctx->transcript_font_size = 3;
    ctx->hour = 9;
    ctx->minute = 41;
    ctx->year = 2026;
    ctx->month = 6;
    ctx->day = 6;
    ctx->weekday = weekday_from_date(ctx->year, ctx->month, ctx->day);
    ctx->alarm_hour = 7;
    ctx->alarm_minute = 30;
    ctx->timer_seconds = 300;
    ctx->timer_preset_seconds = 300;
    ctx->interval_seconds = 90;
    ctx->interval_preset_seconds = 90;
    ctx->record_state = PJ_RECORD_IDLE;
    ctx->playback_state = PJ_PLAYBACK_IDLE;
    ctx->note_count = 0;
    ctx->selected_note = 0;
    pj_home_layout_defaults(&ctx->home_layout);
    mark_full(ctx);
}

int pj_ui_set_home_layout(pj_ui_context_t *ctx, const pj_home_layout_t *layout)
{
    pj_home_layout_t canonical;
    if (ctx == NULL || !pj_home_layout_canonical_copy(&canonical, layout)) {
        return 0;
    }
    if (memcmp(&ctx->home_layout, &canonical, sizeof(canonical)) == 0) {
        return 1;
    }
    ctx->home_layout = canonical;
    if (ctx->state == PJ_UI_STATE_HOME) {
        mark_full(ctx);
    }
    return 1;
}

void pj_ui_restore_default_home(pj_ui_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    pj_home_layout_t fallback;
    pj_home_layout_defaults(&fallback);
    (void)pj_ui_set_home_layout(ctx, &fallback);
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
    if (ctx->dirty.width > 0 && ctx->dirty.height > 0 && ctx->dirty.partial) {
        if (ctx->partial_refresh_count < INT_MAX) {
            ctx->partial_refresh_count++;
        }
    } else if (ctx->dirty.width > 0 && ctx->dirty.height > 0) {
        ctx->partial_refresh_count = 0;
    }
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
    return PJ_UI_NOTES_PER_PAGE;
}

static void restore_selected_note_list(pj_ui_context_t *ctx, pj_ui_state_t state)
{
    ctx->note_page = ctx->selected_note / notes_per_page();
    set_state(ctx, state);
    ctx->focus_index = ctx->selected_note;
}

static int note_page_count(const pj_ui_context_t *ctx)
{
    if (ctx->note_count <= 0) {
        return 1;
    }
    return (ctx->note_count + notes_per_page() - 1) / notes_per_page();
}

static int jog_note_page(pj_ui_context_t *ctx, int direction)
{
    int target = ctx->note_page + direction;
    if (target < 0 || target >= note_page_count(ctx)) {
        return 0;
    }
    ctx->note_page = target;
    ctx->focus_index = target * notes_per_page();
    mark_full(ctx);
    return 1;
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
    set_state(ctx, PJ_UI_STATE_HOME);
}

void pj_ui_sleep(pj_ui_context_t *ctx)
{
    ctx->record_state = PJ_RECORD_IDLE;
    ctx->playback_state = PJ_PLAYBACK_IDLE;
    set_state(ctx, PJ_UI_STATE_STATIC);
}

void pj_ui_set_status(pj_ui_context_t *ctx, int battery_percent, int temperature_c,
                      int humidity_percent)
{
    if (ctx->battery_percent == battery_percent && ctx->temperature_c == temperature_c &&
        ctx->humidity_percent == humidity_percent) {
        return;
    }
    ctx->battery_percent = battery_percent;
    ctx->temperature_c = temperature_c;
    ctx->humidity_percent = humidity_percent;
    if (ctx->state == PJ_UI_STATE_TIME_TEMP) {
        mark_partial(ctx, 0, 118, PJ_DISPLAY_WIDTH, 82);
    }
}

void pj_ui_set_preferences(pj_ui_context_t *ctx, int clock_24h,
                           int temperature_fahrenheit, int transcript_font_size)
{
    if (ctx == NULL) {
        return;
    }
    clock_24h = clock_24h != 0;
    temperature_fahrenheit = temperature_fahrenheit != 0;
    transcript_font_size = transcript_font_size < 2 ? 2 :
                           transcript_font_size > 3 ? 3 : transcript_font_size;
    if (ctx->clock_24h == clock_24h &&
        ctx->temperature_fahrenheit == temperature_fahrenheit &&
        ctx->transcript_font_size == transcript_font_size) {
        return;
    }
    ctx->clock_24h = clock_24h;
    ctx->temperature_fahrenheit = temperature_fahrenheit;
    ctx->transcript_font_size = transcript_font_size;
    mark_full(ctx);
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
        mark_partial(ctx, 0, 50, PJ_DISPLAY_WIDTH, 150);
    }
}

void pj_ui_set_sync_detail(pj_ui_context_t *ctx, const char *phase, int failed,
                           const char *error, int request_pending)
{
    if (ctx == NULL) {
        return;
    }
    failed = failed < 0 ? 0 : failed;
    request_pending = request_pending != 0;
    const char *safe_phase = phase == NULL ? "idle" : phase;
    const char *safe_error = error == NULL ? "" : error;
    if (ctx->sync_failed == failed &&
        ctx->sync_request_pending == request_pending &&
        strcmp(ctx->sync_phase, safe_phase) == 0 &&
        strcmp(ctx->sync_error, safe_error) == 0) {
        return;
    }
    ctx->sync_failed = failed;
    ctx->sync_request_pending = request_pending;
    (void)snprintf(ctx->sync_phase, sizeof(ctx->sync_phase), "%s", safe_phase);
    (void)snprintf(ctx->sync_error, sizeof(ctx->sync_error), "%s", safe_error);
    if (ctx->state == PJ_UI_STATE_SYNC) {
        mark_partial(ctx, 0, 0, PJ_DISPLAY_WIDTH, PJ_DISPLAY_HEIGHT);
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
            mark_dynamic_partial(ctx, 0, 0, PJ_DISPLAY_WIDTH, 84);
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
        if (ctx->state == PJ_UI_STATE_NOTE_DETAIL) {
            restore_selected_note_list(ctx, PJ_UI_STATE_LISTEN);
        } else {
            set_state(ctx, PJ_UI_STATE_NOTES);
        }
        return;
    }
    if (next_playback == PJ_PLAYBACK_IDLE) {
        ctx->playback_exit_pending = 0;
    }
    if (ctx->state == PJ_UI_STATE_RECORD) {
        mark_partial(ctx, 0, 0, PJ_DISPLAY_WIDTH, PJ_DISPLAY_HEIGHT);
    } else if (ctx->state == PJ_UI_STATE_NOTE_DETAIL || ctx->state == PJ_UI_STATE_LISTEN) {
        mark_full(ctx);
    }
}

void pj_ui_set_recording_elapsed(pj_ui_context_t *ctx, uint64_t elapsed_ms)
{
    if (ctx == NULL) {
        return;
    }
    uint64_t elapsed_seconds = elapsed_ms / 1000u;
    int seconds = elapsed_seconds > INT_MAX ? INT_MAX : (int)elapsed_seconds;
    if (ctx->recording_seconds == seconds) {
        return;
    }
    ctx->recording_seconds = seconds;
    if (ctx->state == PJ_UI_STATE_RECORD) {
        mark_dynamic_partial(ctx, 0, 55, PJ_DISPLAY_WIDTH, 90);
    }
}

static int elapsed_seconds_from_ms(uint64_t milliseconds)
{
    uint64_t seconds = milliseconds / 1000u;
    return seconds > INT_MAX ? INT_MAX : (int)seconds;
}

static int countdown_seconds_from_ms(uint64_t milliseconds)
{
    uint64_t seconds = milliseconds / 1000u + (milliseconds % 1000u != 0);
    return seconds > INT_MAX ? INT_MAX : (int)seconds;
}

void pj_ui_set_time_projection(pj_ui_context_t *ctx, const pj_ui_time_projection_t *projection)
{
    if (ctx == NULL || projection == NULL) {
        return;
    }

    int next_stopwatch_seconds = elapsed_seconds_from_ms(projection->stopwatch_elapsed_ms);
    int next_timer_seconds = countdown_seconds_from_ms(projection->timer_remaining_ms);
    int next_interval_seconds = countdown_seconds_from_ms(projection->interval_remaining_ms);
    int next_interval_round = projection->interval_phase > INT_MAX ?
        INT_MAX : (int)projection->interval_phase;
    int alarm_changed = ctx->alarm_on != (projection->alarm_enabled != 0) ||
        ctx->alarm_hour != projection->alarm_hour ||
        ctx->alarm_minute != projection->alarm_minute;
    int stopwatch_running_changed =
        ctx->stopwatch_running != (projection->stopwatch_running != 0);
    int timer_running_changed = ctx->timer_running != (projection->timer_running != 0);
    int interval_running_changed =
        ctx->interval_running != (projection->interval_running != 0);
    int stopwatch_seconds_changed = ctx->stopwatch_seconds != next_stopwatch_seconds;
    int timer_seconds_changed = ctx->timer_seconds != next_timer_seconds;
    int interval_value_changed = ctx->interval_seconds != next_interval_seconds ||
        ctx->interval_round != next_interval_round;
    int stopwatch_changed = ctx->stopwatch_running != (projection->stopwatch_running != 0) ||
        stopwatch_seconds_changed;
    int timer_changed = ctx->timer_running != (projection->timer_running != 0) ||
        timer_seconds_changed;
    int interval_changed = ctx->interval_running != (projection->interval_running != 0) ||
        interval_value_changed;

    ctx->alarm_on = projection->alarm_enabled != 0;
    ctx->alarm_hour = projection->alarm_hour;
    ctx->alarm_minute = projection->alarm_minute;
    ctx->stopwatch_running = projection->stopwatch_running != 0;
    ctx->stopwatch_seconds = next_stopwatch_seconds;
    ctx->timer_running = projection->timer_running != 0;
    ctx->timer_seconds = next_timer_seconds;
    ctx->interval_running = projection->interval_running != 0;
    ctx->interval_seconds = next_interval_seconds;
    ctx->interval_round = next_interval_round;
    ctx->active_alert = projection->active_alert;
    ctx->alert_audio_deferred = projection->alert_audio_deferred != 0;
    ctx->recovery_time_uncertain = projection->recovery_time_uncertain != 0;

    if (ctx->state == PJ_UI_STATE_ALARM && alarm_changed) {
        mark_partial(ctx, 0, 0, PJ_DISPLAY_WIDTH, PJ_UI_ALARM_CONTROLS_TOP);
    } else if (ctx->state == PJ_UI_STATE_STOPWATCH && stopwatch_changed) {
        if (stopwatch_seconds_changed) {
            mark_dynamic_partial(ctx, 0, 0, PJ_DISPLAY_WIDTH, PJ_UI_TIME_CONTROLS_TOP);
        }
        if (stopwatch_running_changed) {
            mark_partial(ctx, 0, PJ_UI_TIME_CONTROLS_TOP, PJ_DISPLAY_WIDTH / 2,
                         PJ_DISPLAY_HEIGHT - PJ_UI_TIME_CONTROLS_TOP);
        }
    } else if (ctx->state == PJ_UI_STATE_TIMER && timer_changed) {
        if (timer_seconds_changed) {
            mark_dynamic_partial(ctx, 0, 0, PJ_DISPLAY_WIDTH, PJ_UI_TIME_CONTROLS_TOP);
        }
        if (timer_running_changed) {
            ui_rect_t control = timer_control_rect(0);
            mark_partial(ctx, control.x, control.y, control.w, control.h);
        }
    } else if (ctx->state == PJ_UI_STATE_INTERVAL && interval_changed) {
        if (interval_value_changed) {
            mark_dynamic_partial(ctx, 0, 0, PJ_DISPLAY_WIDTH, PJ_UI_TIME_CONTROLS_TOP);
        }
        if (interval_running_changed) {
            ui_rect_t control = timer_control_rect(0);
            mark_partial(ctx, control.x, control.y, control.w, control.h);
        }
    }
}

int pj_ui_consume_time_command(pj_ui_context_t *ctx, pj_ui_time_command_t *command)
{
    if (ctx == NULL || command == NULL || ctx->time_command.type == PJ_UI_TIME_COMMAND_NONE) {
        return 0;
    }
    *command = ctx->time_command;
    memset(&ctx->time_command, 0, sizeof(ctx->time_command));
    return 1;
}

int pj_ui_discard_pending_interval_command(pj_ui_context_t *ctx)
{
    if (ctx == NULL ||
        (ctx->time_command.type != PJ_UI_TIME_COMMAND_INTERVAL_START &&
         ctx->time_command.type != PJ_UI_TIME_COMMAND_INTERVAL_PAUSE &&
         ctx->time_command.type != PJ_UI_TIME_COMMAND_INTERVAL_RESET)) {
        return 0;
    }
    memset(&ctx->time_command, 0, sizeof(ctx->time_command));
    return 1;
}

static void queue_time_command(pj_ui_context_t *ctx, pj_ui_time_command_type_t type,
                               uint64_t duration_ms, uint64_t secondary_duration_ms)
{
    if (ctx->time_command.type != PJ_UI_TIME_COMMAND_NONE) {
        return;
    }
    ctx->time_command.type = type;
    ctx->time_command.duration_ms = duration_ms;
    ctx->time_command.secondary_duration_ms = secondary_duration_ms;
}

static void set_focus_index(pj_ui_context_t *ctx, int next)
{
    ctx->focus_index = next;
    ctx->focus_idle_ticks = 0;
}

static int reset_time_page_and_return(pj_ui_context_t *ctx)
{
    if (ctx->time_command.type != PJ_UI_TIME_COMMAND_NONE) {
        return 0;
    }
    pj_ui_time_command_type_t command = PJ_UI_TIME_COMMAND_NONE;
    switch (ctx->state) {
    case PJ_UI_STATE_STOPWATCH:
        command = PJ_UI_TIME_COMMAND_STOPWATCH_RESET;
        ctx->stopwatch_running = 0;
        ctx->stopwatch_seconds = 0;
        break;
    case PJ_UI_STATE_TIMER:
        command = PJ_UI_TIME_COMMAND_TIMER_RESET;
        ctx->timer_running = 0;
        ctx->timer_seconds = ctx->timer_preset_seconds;
        break;
    case PJ_UI_STATE_INTERVAL:
        command = PJ_UI_TIME_COMMAND_INTERVAL_RESET;
        ctx->interval_running = 0;
        ctx->interval_seconds = ctx->interval_preset_seconds;
        ctx->interval_round = 0;
        break;
    default:
        return 0;
    }
    queue_time_command(ctx, command, 0, 0);
    set_state(ctx, PJ_UI_STATE_TIME);
    return 1;
}

int pj_ui_handle_aux_long(pj_ui_context_t *ctx)
{
    if (ctx->state == PJ_UI_STATE_STATIC || ctx->state == PJ_UI_STATE_TIME_TEMP) {
        return 0;
    }
    if (ctx->state == PJ_UI_STATE_STOPWATCH || ctx->state == PJ_UI_STATE_TIMER ||
        ctx->state == PJ_UI_STATE_INTERVAL) {
        return reset_time_page_and_return(ctx);
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
        mark_full(ctx);
        return 1;
    }
    if (ctx->state == PJ_UI_STATE_NOTE_DETAIL) {
        restore_selected_note_list(ctx, ctx->note_detail_transcript ?
                                   PJ_UI_STATE_READ : PJ_UI_STATE_LISTEN);
    } else {
        set_state(ctx, pj_ui_parent_state(ctx->state));
    }
    return 1;
}

static int activate_focused_control(pj_ui_context_t *ctx)
{
    switch (ctx->state) {
    case PJ_UI_STATE_ALARM:
        if (ctx->focus_index == 0) ctx->alarm_on = !ctx->alarm_on;
        else if (ctx->focus_index == 1) ctx->alarm_hour = (ctx->alarm_hour + 23) % 24;
        else if (ctx->focus_index == 2) ctx->alarm_hour = (ctx->alarm_hour + 1) % 24;
        else if (ctx->focus_index == 3) ctx->alarm_minute = (ctx->alarm_minute + 45) % 60;
        else ctx->alarm_minute = (ctx->alarm_minute + 15) % 60;
        mark_partial(ctx, 0, 0, PJ_DISPLAY_WIDTH, PJ_UI_ALARM_CONTROLS_TOP);
        return 1;
    case PJ_UI_STATE_STOPWATCH:
        queue_time_command(ctx, ctx->focus_index == 1 ?
                           PJ_UI_TIME_COMMAND_STOPWATCH_RESET :
                           (ctx->stopwatch_running ?
                            PJ_UI_TIME_COMMAND_STOPWATCH_PAUSE :
                            PJ_UI_TIME_COMMAND_STOPWATCH_START), 0, 0);
        return 1;
    case PJ_UI_STATE_TIMER:
        if (ctx->focus_index == 0) {
            int seconds = ctx->timer_seconds > 0 ?
                ctx->timer_seconds : ctx->timer_preset_seconds;
            queue_time_command(ctx, ctx->timer_running ?
                               PJ_UI_TIME_COMMAND_TIMER_PAUSE :
                               PJ_UI_TIME_COMMAND_TIMER_START,
                               (uint64_t)seconds * 1000u, 0);
        } else if (ctx->focus_index == 1) {
            queue_time_command(ctx, PJ_UI_TIME_COMMAND_TIMER_RESET, 0, 0);
        } else {
            int delta = ctx->focus_index == 2 ? -30 : 30;
            ctx->timer_seconds = max_int(30, min_int(PJ_UI_MAX_DURATION_SECONDS,
                                                     ctx->timer_seconds + delta));
            ctx->timer_preset_seconds = ctx->timer_seconds;
            queue_time_command(ctx, ctx->timer_running ?
                               PJ_UI_TIME_COMMAND_TIMER_START :
                               PJ_UI_TIME_COMMAND_TIMER_RESET,
                               (uint64_t)ctx->timer_seconds * 1000u, 0);
            mark_dynamic_partial(ctx, 0, 0, PJ_DISPLAY_WIDTH,
                                 PJ_UI_TIME_CONTROLS_TOP);
        }
        ctx->focus_idle_ticks = 0;
        return 1;
    case PJ_UI_STATE_INTERVAL:
        if (ctx->focus_index == 0) {
            int seconds = ctx->interval_seconds > 0 ?
                ctx->interval_seconds : ctx->interval_preset_seconds;
            queue_time_command(ctx, ctx->interval_running ?
                               PJ_UI_TIME_COMMAND_INTERVAL_PAUSE :
                               PJ_UI_TIME_COMMAND_INTERVAL_START,
                               (uint64_t)seconds * 1000u,
                               (uint64_t)ctx->interval_preset_seconds * 1000u);
        } else if (ctx->focus_index == 1) {
            queue_time_command(ctx, PJ_UI_TIME_COMMAND_INTERVAL_RESET, 0, 0);
        } else {
            int delta = ctx->focus_index == 2 ? -60 : 60;
            ctx->interval_seconds = max_int(60, min_int(PJ_UI_MAX_DURATION_SECONDS,
                                                        ctx->interval_seconds + delta));
            ctx->interval_preset_seconds = ctx->interval_seconds;
            queue_time_command(ctx, ctx->interval_running ?
                               PJ_UI_TIME_COMMAND_INTERVAL_START :
                               PJ_UI_TIME_COMMAND_INTERVAL_RESET,
                               (uint64_t)ctx->interval_seconds * 1000u,
                               (uint64_t)ctx->interval_preset_seconds * 1000u);
            mark_dynamic_partial(ctx, 0, 0, PJ_DISPLAY_WIDTH,
                                 PJ_UI_TIME_CONTROLS_TOP);
        }
        ctx->focus_idle_ticks = 0;
        return 1;
    case PJ_UI_STATE_SETTINGS:
    case PJ_UI_STATE_DISPLAY:
        if (ctx->focus_index == 0) {
            set_state(ctx, PJ_UI_STATE_VOLUME);
            return 1;
        }
        if (ctx->focus_index == 1) {
            ctx->dark_mode = !ctx->dark_mode;
        } else if (ctx->focus_index == 2) {
            ctx->clock_24h = !ctx->clock_24h;
        } else {
            set_state(ctx, PJ_UI_STATE_SYNC);
            return 1;
        }
        mark_full(ctx);
        return 1;
    case PJ_UI_STATE_VOLUME:
        ctx->volume += ctx->focus_index == 0 ? -1 : 1;
        clamp_volume(ctx);
        mark_partial(ctx, 0, 0, PJ_DISPLAY_WIDTH, 100);
        return 1;
    default:
        return 0;
    }
}

int pj_ui_handle_aux_short(pj_ui_context_t *ctx)
{
    if (ctx == NULL) {
        return 0;
    }
    switch (ctx->state) {
    case PJ_UI_STATE_TIME_TEMP:
        set_state(ctx, PJ_UI_STATE_HOME);
        return 1;
    case PJ_UI_STATE_NOTE_DETAIL:
        if (ctx->note_detail_transcript ||
            ctx->playback_state == PJ_PLAYBACK_STOPPING) {
            return 0;
        }
        ctx->playback_state = ctx->playback_state == PJ_PLAYBACK_ACTIVE ?
            PJ_PLAYBACK_STOPPING : PJ_PLAYBACK_ACTIVE;
        ctx->playback_exit_pending = 0;
        mark_full(ctx);
        return 1;
    case PJ_UI_STATE_ALARM:
        ctx->alarm_on = !ctx->alarm_on;
        mark_partial(ctx, 0, 0, PJ_DISPLAY_WIDTH, PJ_UI_ALARM_CONTROLS_TOP);
        return 1;
    case PJ_UI_STATE_STOPWATCH:
        queue_time_command(ctx, ctx->stopwatch_running ?
                           PJ_UI_TIME_COMMAND_STOPWATCH_PAUSE :
                           PJ_UI_TIME_COMMAND_STOPWATCH_START, 0, 0);
        return 1;
    case PJ_UI_STATE_TIMER: {
        int seconds = ctx->timer_seconds > 0 ?
            ctx->timer_seconds : ctx->timer_preset_seconds;
        queue_time_command(ctx, ctx->timer_running ?
                           PJ_UI_TIME_COMMAND_TIMER_PAUSE :
                           PJ_UI_TIME_COMMAND_TIMER_START,
                           (uint64_t)seconds * 1000u, 0);
        ctx->focus_idle_ticks = 0;
        return 1;
    }
    case PJ_UI_STATE_INTERVAL: {
        int seconds = ctx->interval_seconds > 0 ?
            ctx->interval_seconds : ctx->interval_preset_seconds;
        queue_time_command(ctx, ctx->interval_running ?
                           PJ_UI_TIME_COMMAND_INTERVAL_PAUSE :
                           PJ_UI_TIME_COMMAND_INTERVAL_START,
                           (uint64_t)seconds * 1000u,
                           (uint64_t)ctx->interval_preset_seconds * 1000u);
        ctx->focus_idle_ticks = 0;
        return 1;
    }
    default:
        return 0;
    }
}

int pj_ui_handle_aux_double(pj_ui_context_t *ctx)
{
    if (ctx == NULL ||
        (ctx->state != PJ_UI_STATE_TIME_TEMP && ctx->state != PJ_UI_STATE_HOME) ||
        ctx->record_state != PJ_RECORD_IDLE ||
        ctx->playback_state != PJ_PLAYBACK_IDLE) {
        return 0;
    }
    ctx->record_state = PJ_RECORD_ACTIVE;
    ctx->recording_seconds = 0;
    set_state(ctx, PJ_UI_STATE_RECORD);
    return 1;
}

int pj_ui_tick(pj_ui_context_t *ctx)
{
    if ((ctx->state == PJ_UI_STATE_TIMER || ctx->state == PJ_UI_STATE_INTERVAL) &&
        ctx->focus_index != 0) {
        ctx->focus_idle_ticks++;
        if (ctx->focus_idle_ticks >= PJ_UI_FOCUS_TIMEOUT_TICKS) {
            set_focus_index(ctx, 0);
        }
    } else {
        ctx->focus_idle_ticks = 0;
    }
    return 0;
}

int pj_ui_handle_touch(pj_ui_context_t *ctx, int x, int y, pj_touch_kind_t kind)
{
    pj_ui_state_t next = ctx->state;

    if (kind == PJ_TOUCH_SWIPE_LEFT &&
        (ctx->state == PJ_UI_STATE_LISTEN || ctx->state == PJ_UI_STATE_READ)) {
        return jog_note_page(ctx, 1);
    }
    if (kind == PJ_TOUCH_SWIPE_RIGHT &&
        (ctx->state == PJ_UI_STATE_LISTEN || ctx->state == PJ_UI_STATE_READ)) {
        return jog_note_page(ctx, -1);
    }
    if (kind != PJ_TOUCH_TAP) {
        return 0;
    }

    switch (ctx->state) {
    case PJ_UI_STATE_STATIC:
        return 0;
    case PJ_UI_STATE_TIME_TEMP:
        set_state(ctx, PJ_UI_STATE_HOME);
        return 1;
    case PJ_UI_STATE_HOME: {
        tile_t tiles[PJ_HOME_MAX_SLOTS];
        size_t tile_count = home_tiles(ctx, tiles);
        if (tile_hit(tiles, tile_count, x, y, &next)) {
            if (next == PJ_UI_STATE_RECORD &&
                (ctx->record_state != PJ_RECORD_IDLE ||
                 ctx->playback_state != PJ_PLAYBACK_IDLE)) {
                return 0;
            }
            set_state(ctx, next);
            return 1;
        }
        break;
    }
    case PJ_UI_STATE_NOTES:
        if (tile_hit(NOTES_TILES, sizeof(NOTES_TILES) / sizeof(NOTES_TILES[0]), x, y, &next)) {
            for (size_t i = 0; i < sizeof(NOTES_TILES) / sizeof(NOTES_TILES[0]); i++) {
                if (next == NOTES_TILES[i].next) ctx->focus_index = (int)i;
            }
            if (next == PJ_UI_STATE_RECORD) {
                if (ctx->record_state != PJ_RECORD_IDLE ||
                    ctx->playback_state != PJ_PLAYBACK_IDLE) {
                    return 0;
                }
                ctx->record_state = PJ_RECORD_ACTIVE;
                ctx->recording_seconds = 0;
            }
            set_state(ctx, next);
            return 1;
        }
        break;
    case PJ_UI_STATE_TIME:
        if (tile_hit(TIME_TILES, sizeof(TIME_TILES) / sizeof(TIME_TILES[0]), x, y, &next)) {
            for (size_t i = 0; i < sizeof(TIME_TILES) / sizeof(TIME_TILES[0]); i++) {
                if (next == TIME_TILES[i].next) ctx->focus_index = (int)i;
            }
            set_state(ctx, next);
            return 1;
        }
        break;
    case PJ_UI_STATE_SETTINGS:
    case PJ_UI_STATE_DISPLAY:
        set_focus_index(ctx, (y < PJ_DISPLAY_HEIGHT / 2 ? 0 : 2) +
                             (x < PJ_DISPLAY_WIDTH / 2 ? 0 : 1));
        return activate_focused_control(ctx);
    case PJ_UI_STATE_VOLUME:
        if (y < 100) {
            return 0;
        }
        set_focus_index(ctx, x < 100 ? 0 : 1);
        return activate_focused_control(ctx);
    case PJ_UI_STATE_LISTEN:
    case PJ_UI_STATE_READ:
        if (y >= PJ_UI_NOTE_PAGER_TOP) {
            return jog_note_page(ctx, x < PJ_DISPLAY_WIDTH / 2 ? -1 : 1);
        }
        if (ctx->note_count > 0) {
            int row = min_int(notes_per_page() - 1,
                              y * notes_per_page() / PJ_UI_NOTE_PAGER_TOP);
            int index = ctx->note_page * notes_per_page() + row;
            if (row >= 0 && row < notes_per_page() && index < ctx->note_count) {
                ctx->selected_note = index;
                set_focus_index(ctx, index);
                ctx->note_detail_transcript = ctx->state == PJ_UI_STATE_READ;
                set_state(ctx, PJ_UI_STATE_NOTE_DETAIL);
            }
            return 1;
        }
        break;
    case PJ_UI_STATE_NOTE_DETAIL:
        if (ctx->note_detail_transcript) {
            return 0;
        }
        if (ctx->playback_state != PJ_PLAYBACK_STOPPING) {
            ctx->playback_state = ctx->playback_state == PJ_PLAYBACK_ACTIVE ?
                PJ_PLAYBACK_STOPPING : PJ_PLAYBACK_ACTIVE;
            ctx->playback_exit_pending = 0;
            mark_full(ctx);
            return 1;
        }
        break;
    case PJ_UI_STATE_ALARM:
        if (y >= PJ_UI_ALARM_TOGGLE_TOP) {
            if (y < PJ_UI_ALARM_CONTROLS_TOP) {
                set_focus_index(ctx, 0);
            } else {
                int row = min_int(1, (y - PJ_UI_ALARM_CONTROLS_TOP) * 2 /
                                  (PJ_DISPLAY_HEIGHT - PJ_UI_ALARM_CONTROLS_TOP));
                set_focus_index(ctx, 1 + row * 2 + x / 100);
            }
            return activate_focused_control(ctx);
        }
        break;
    case PJ_UI_STATE_STOPWATCH:
        if (y >= PJ_UI_TIME_CONTROLS_TOP) {
            set_focus_index(ctx, x < 100 ? 0 : 1);
            return activate_focused_control(ctx);
        }
        break;
    case PJ_UI_STATE_TIMER:
        if (y >= PJ_UI_TIME_CONTROLS_TOP) {
            set_focus_index(ctx, (y < (PJ_UI_TIME_CONTROLS_TOP + PJ_DISPLAY_HEIGHT) / 2 ?
                                  0 : 2) + x / 100);
            return activate_focused_control(ctx);
        }
        break;
    case PJ_UI_STATE_INTERVAL:
        if (y >= PJ_UI_TIME_CONTROLS_TOP) {
            set_focus_index(ctx, (y < (PJ_UI_TIME_CONTROLS_TOP + PJ_DISPLAY_HEIGHT) / 2 ?
                                  0 : 2) + x / 100);
            return activate_focused_control(ctx);
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
    const uint8_t *pixels = pj_default_static_art;
    for (int y = 0; y < PJ_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
            size_t index = (size_t)y * PJ_DISPLAY_WIDTH + (size_t)x;
            if ((pixels[index >> 3u] >> (index & 7u)) & 1u) {
                fb_set(fb, x, y, 1);
            }
        }
    }
}

static void draw_time_temp(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    char text[32];
    int display_hour = ctx->hour;
    if (!ctx->clock_24h) {
        display_hour = ctx->hour % 12;
        if (display_hour == 0) display_hour = 12;
        (void)snprintf(text, sizeof(text), "%d:%02d", display_hour, ctx->minute);
    } else {
        (void)snprintf(text, sizeof(text), "%02d:%02d", display_hour, ctx->minute);
    }
    draw_text_center_at_double(fb, 100, 43, text, 4);
    (void)snprintf(text, sizeof(text), "%s %02d/%02d", weekday_name(ctx->weekday),
                   ctx->month, ctx->day);
    draw_text_center_at(fb, 100, 100, text, 4);
    int temperature_f =
        (ctx->temperature_c * 9 + (ctx->temperature_c >= 0 ? 2 : -2)) / 5 + 32;
    if (ctx->humidity_percent < 0) {
        (void)snprintf(text, sizeof(text), "%dC %dF --%%",
                       ctx->temperature_c, temperature_f);
    } else if (ctx->temperature_fahrenheit) {
        (void)snprintf(text, sizeof(text), "%dF %dC %d%%", temperature_f,
                       ctx->temperature_c, ctx->humidity_percent);
    } else {
        (void)snprintf(text, sizeof(text), "%dC %dF %d%%", ctx->temperature_c,
                       temperature_f, ctx->humidity_percent);
    }
    draw_text_center_at(fb, 100, 140, text, 4);
    (void)snprintf(text, sizeof(text), "%d%%", ctx->battery_percent);
    draw_text_center_at(fb, 100, 180, text, 4);
}

static void draw_record(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    char elapsed[16];
    (void)snprintf(elapsed, sizeof(elapsed), "%02d:%02d",
                   ctx->recording_seconds / 60, ctx->recording_seconds % 60);
    draw_text_center_at_double(fb, 100, 100, elapsed, 4);
}

static void draw_notes_list(const pj_ui_context_t *ctx, pj_framebuffer_t *fb, const char *kind)
{
    if (ctx->note_count <= 0) {
        draw_text_center_at(fb, 100, PJ_UI_NOTE_PAGER_TOP / 2,
                            strcmp(kind, "AUD") == 0 ? "NO AUDIO" : "NO TEXT", 4);
    }
    for (int i = 0; i < notes_per_page(); i++) {
        int y = i * PJ_UI_NOTE_PAGER_TOP / notes_per_page();
        int next_y = (i + 1) * PJ_UI_NOTE_PAGER_TOP / notes_per_page();
        int note_index = ctx->note_page * notes_per_page() + i;
        if (note_index >= ctx->note_count) {
            continue;
        }
        draw_rect(fb, 0, y, PJ_DISPLAY_WIDTH, next_y - y);
        draw_text_centered_ellipsized(
            fb, 100, (y + next_y) / 2,
            ctx->note_labels[note_index][0] != '\0'
                ? ctx->note_labels[note_index]
                : "RECORDING",
            4, 190);
    }
    draw_rect(fb, 0, PJ_UI_NOTE_PAGER_TOP, PJ_DISPLAY_WIDTH / 2,
              PJ_DISPLAY_HEIGHT - PJ_UI_NOTE_PAGER_TOP);
    draw_rect(fb, PJ_DISPLAY_WIDTH / 2, PJ_UI_NOTE_PAGER_TOP,
              PJ_DISPLAY_WIDTH / 2, PJ_DISPLAY_HEIGHT - PJ_UI_NOTE_PAGER_TOP);
    draw_icon(fb, "LEFT", PJ_DISPLAY_WIDTH / 4,
              (PJ_UI_NOTE_PAGER_TOP + PJ_DISPLAY_HEIGHT) / 2, 34);
    draw_icon(fb, "RIGHT", 3 * PJ_DISPLAY_WIDTH / 4,
              (PJ_UI_NOTE_PAGER_TOP + PJ_DISPLAY_HEIGHT) / 2, 34);
}

static void draw_settings(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    const char *values[] = {
        "VOLUME",
        ctx->dark_mode ? "DARK" : "LIGHT",
        ctx->clock_24h ? "24H" : "12H",
        "SYNC",
    };
    for (int i = 0; i < 4; i++) {
        int x = (i % 2) * PJ_DISPLAY_WIDTH / 2;
        int y = (i / 2) * PJ_DISPLAY_HEIGHT / 2;
        int scale = 4;
        while (scale > 2 && text_width(values[i], scale) > PJ_DISPLAY_WIDTH / 2 - 10) {
            scale--;
        }
        draw_rect(fb, x, y, PJ_DISPLAY_WIDTH / 2, PJ_DISPLAY_HEIGHT / 2);
        if (scale == 2 && text_width(values[i], 1) * 2 <= PJ_DISPLAY_WIDTH / 2 - 10) {
            draw_text_center_at_double(fb, x + PJ_DISPLAY_WIDTH / 4,
                                       y + PJ_DISPLAY_HEIGHT / 4, values[i], 1);
        } else {
            draw_text_center_at(fb, x + PJ_DISPLAY_WIDTH / 4,
                                y + PJ_DISPLAY_HEIGHT / 4, values[i], scale);
        }
    }
}

static void draw_volume(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    draw_rect(fb, 0, 0, PJ_DISPLAY_WIDTH, 100);
    int inner_width = PJ_DISPLAY_WIDTH - 2 * PJ_UI_BUTTON_BORDER_WIDTH;
    int width = (ctx->volume * inner_width) / 10;
    if (width > 0) {
        fill_rect(fb, PJ_UI_BUTTON_BORDER_WIDTH, PJ_UI_BUTTON_BORDER_WIDTH,
                  width, 100 - 2 * PJ_UI_BUTTON_BORDER_WIDTH);
    }
    const char *actions[] = {"MINUS", "PLUS"};
    draw_icon_controls(fb, actions, 2, 100, 2);
}

static void draw_sync(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    const char *status = "IDLE";
    if (strcmp(ctx->sync_phase, "succeeded") == 0) {
        status = "COMPLETE";
    } else if (strcmp(ctx->sync_phase, "failed") == 0) {
        status = ctx->sync_request_pending && !ctx->sync_online ?
                 "OFFLINE" : "FAILED";
    } else if (strcmp(ctx->sync_phase, "discovering") == 0 ||
               strcmp(ctx->sync_phase, "requesting") == 0 ||
               strcmp(ctx->sync_phase, "running") == 0) {
        status = "ACTIVE";
    } else if (ctx->sync_request_pending ||
               strcmp(ctx->sync_phase, "pending") == 0) {
        status = "PENDING";
    }
    draw_text_center_at(fb, 100, 34, status, 4);
    char text[24];
    (void)snprintf(text, sizeof(text), "PENDING %d", ctx->sync_pending);
    draw_text_center_at(fb, 100, 86, text, 3);
    (void)snprintf(text, sizeof(text), "SENT %d  FAIL %d",
                   ctx->sync_transferred, ctx->sync_failed);
    draw_text_center_at(fb, 100, 126, text, 2);
    if (ctx->sync_error[0] != '\0') {
        char message[18];
        size_t i = 0U;
        for (; i + 1U < sizeof(message) && ctx->sync_error[i] != '\0'; i++) {
            char ch = ctx->sync_error[i];
            message[i] = ch >= 'a' && ch <= 'z' ? (char)(ch - 'a' + 'A') : ch;
        }
        message[i] = '\0';
        draw_text_center_at(fb, 100, 171, message, 2);
    } else {
        draw_text_center_at(fb, 100, 171,
                            ctx->sync_online ? "CONNECTED" : "WAITING", 2);
    }
}

static void format_hms(char *out, size_t out_size, int seconds)
{
    int hours = seconds / 3600;
    int minutes = (seconds / 60) % 60;
    int secs = seconds % 60;
    (void)snprintf(out, out_size, "%02d:%02d:%02d", hours, minutes, secs);
}

static void format_duration(char *out, size_t out_size, int seconds)
{
    if (seconds < 3600) {
        (void)snprintf(out, out_size, "%02d:%02d", seconds / 60, seconds % 60);
    } else {
        format_hms(out, out_size, seconds);
    }
}

static void draw_duration_value(pj_framebuffer_t *fb, int cy, const char *text)
{
    if (strlen(text) <= 5u) {
        draw_text_center_at_double(fb, 100, cy, text, 4);
    } else {
        draw_text_center_at(fb, 100, cy, text, 4);
    }
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
    case PJ_UI_STATE_HOME: {
        tile_t tiles[PJ_HOME_MAX_SLOTS];
        size_t tile_count = home_tiles(ctx, tiles);
        draw_icon_menu(fb, tiles, tile_count);
        break;
    }
    case PJ_UI_STATE_NOTES:
        draw_icon_menu(fb, NOTES_TILES, sizeof(NOTES_TILES) / sizeof(NOTES_TILES[0]));
        break;
    case PJ_UI_STATE_TIME:
        draw_icon_menu(fb, TIME_TILES, sizeof(TIME_TILES) / sizeof(TIME_TILES[0]));
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
    case PJ_UI_STATE_ALARM: {
        if (ctx->clock_24h) {
            (void)snprintf(text, sizeof(text), "%02d:%02d", ctx->alarm_hour, ctx->alarm_minute);
            draw_text_center_at_double(fb, 100, 36, text, 4);
        } else {
            int hour = ctx->alarm_hour % 12;
            if (hour == 0) hour = 12;
            (void)snprintf(text, sizeof(text), "%d:%02d", hour, ctx->alarm_minute);
            draw_text_center_at_double(fb, 78, 36, text, 3);
            draw_text_center_at(fb, 164, 36,
                                ctx->alarm_hour < 12 ? "AM" : "PM", 3);
        }
        draw_rect(fb, 0, PJ_UI_ALARM_TOGGLE_TOP, PJ_DISPLAY_WIDTH,
                  PJ_UI_ALARM_CONTROLS_TOP - PJ_UI_ALARM_TOGGLE_TOP);
        draw_text_center_at(fb, 100,
                            (PJ_UI_ALARM_TOGGLE_TOP + PJ_UI_ALARM_CONTROLS_TOP) / 2,
                            ctx->alarm_on ? "ON" : "OFF", 4);
        draw_adjustment_controls(fb, PJ_UI_ALARM_CONTROLS_TOP);
        break;
    }
    case PJ_UI_STATE_STOPWATCH:
        format_duration(text, sizeof(text), ctx->stopwatch_seconds);
        draw_duration_value(fb, 50, text);
        { const char *actions[] = {ctx->stopwatch_running ? "PAUSE" : "START", "RESET"};
          draw_icon_controls(fb, actions, 2, PJ_UI_TIME_CONTROLS_TOP, 2); }
        break;
    case PJ_UI_STATE_TIMER:
        format_duration(text, sizeof(text), ctx->timer_seconds);
        draw_duration_value(fb, 50, text);
        draw_timer_controls(fb, ctx->timer_running ? "PAUSE" : "START");
        break;
    case PJ_UI_STATE_INTERVAL:
        (void)snprintf(text, sizeof(text), "%d", ctx->interval_round);
        draw_text_center_at_double(fb, 100, 20, text, 3);
        format_duration(text, sizeof(text), ctx->interval_seconds);
        draw_duration_value(fb, 70, text);
        draw_timer_controls(fb, ctx->interval_running ? "PAUSE" : "START");
        break;
    case PJ_UI_STATE_CALENDAR:
        (void)snprintf(text, sizeof(text), "%02d/%02d", ctx->month, ctx->day);
        draw_text_center_at_double(fb, 100, 100, text, 4);
        break;
    case PJ_UI_STATE_NOTE_DETAIL:
        if (ctx->note_detail_transcript) {
            int scale = ctx->transcript_font_size;
            const pj_font_size_t *font_size = font_size_for_scale(scale);
            int max_lines = 192 / (font_size->line_height + 5);
            draw_wrapped_text(fb, 4, 4,
                ctx->note_labels[ctx->selected_note][0] != '\0' ? ctx->note_labels[ctx->selected_note] : "Transcript pending",
                scale, 192, max_lines);
        } else {
            draw_icon(fb, ctx->playback_state == PJ_PLAYBACK_ACTIVE ? "PAUSE" :
                           ctx->playback_state == PJ_PLAYBACK_STOPPING ? "STOP" : "START",
                      100, 100, 190);
        }
        break;
    case PJ_UI_STATE_SYNC:
        draw_sync(ctx, fb);
        break;
    case PJ_UI_STATE_VOLUME:
        draw_volume(ctx, fb);
        break;
    case PJ_UI_STATE_DISPLAY:
        draw_settings(ctx, fb);
        break;
        default:
            draw_centered_text(fb, 90, "UNKNOWN", 2);
            break;
    }

    if (ctx->dark_mode && ctx->state != PJ_UI_STATE_STATIC) {
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

static void lvgl_add_widgets(const pj_ui_context_t *ctx)
{
    (void)ctx;
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
