#include "pj_ui.h"
#include "pj_font_space_mono.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

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
} menu_item_t;

static const state_meta_t STATE_META[PJ_UI_STATE_COUNT] = {
    [PJ_UI_STATE_STATIC] = {PJ_UI_STATE_STATIC, "static", "POCKET", PJ_UI_STATE_STATIC},
    [PJ_UI_STATE_TIME_TEMP] = {PJ_UI_STATE_TIME_TEMP, "time_temp", "TIME/TEMP", PJ_UI_STATE_STATIC},
    [PJ_UI_STATE_HOME] = {PJ_UI_STATE_HOME, "home", "HOME", PJ_UI_STATE_TIME_TEMP},
    [PJ_UI_STATE_NOTES] = {PJ_UI_STATE_NOTES, "notes", "NOTES", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_RECORD] = {PJ_UI_STATE_RECORD, "record", "RECORD", PJ_UI_STATE_NOTES},
    [PJ_UI_STATE_LISTEN] = {PJ_UI_STATE_LISTEN, "listen", "LISTEN", PJ_UI_STATE_NOTES},
    [PJ_UI_STATE_READ] = {PJ_UI_STATE_READ, "read", "READ", PJ_UI_STATE_NOTES},
    [PJ_UI_STATE_TIME] = {PJ_UI_STATE_TIME, "time", "TIME", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_ALARM] = {PJ_UI_STATE_ALARM, "alarm", "ALARM", PJ_UI_STATE_TIME},
    [PJ_UI_STATE_STOPWATCH] = {PJ_UI_STATE_STOPWATCH, "stopwatch", "STOPWATCH", PJ_UI_STATE_TIME},
    [PJ_UI_STATE_TIMER] = {PJ_UI_STATE_TIMER, "timer", "TIMER", PJ_UI_STATE_TIME},
    [PJ_UI_STATE_INTERVAL] = {PJ_UI_STATE_INTERVAL, "interval", "INTERVAL", PJ_UI_STATE_TIME},
    [PJ_UI_STATE_SETTINGS] = {PJ_UI_STATE_SETTINGS, "settings", "SETTINGS", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_SYNC] = {PJ_UI_STATE_SYNC, "sync", "SYNC", PJ_UI_STATE_SETTINGS},
    [PJ_UI_STATE_VOLUME] = {PJ_UI_STATE_VOLUME, "volume", "VOLUME", PJ_UI_STATE_SETTINGS},
    [PJ_UI_STATE_CALENDAR] = {PJ_UI_STATE_CALENDAR, "calendar", "CALENDAR", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_TBD] = {PJ_UI_STATE_TBD, "tbd", "TBD", PJ_UI_STATE_HOME},
    [PJ_UI_STATE_NOTE_DETAIL] = {PJ_UI_STATE_NOTE_DETAIL, "note_detail", "NOTE", PJ_UI_STATE_READ},
};

static const menu_item_t HOME_MENU[] = {
    {"NOTES", "NOTE", PJ_UI_STATE_NOTES},
    {"TIME", "TIME", PJ_UI_STATE_TIME},
    {"CALENDAR", "CAL", PJ_UI_STATE_CALENDAR},
    {"TBD", "STAR", PJ_UI_STATE_TBD},
    {"SETTINGS", "GEAR", PJ_UI_STATE_SETTINGS},
};

static const menu_item_t NOTES_MENU[] = {
    {"RECORD", "REC", PJ_UI_STATE_RECORD},
    {"LISTEN", "PLAY", PJ_UI_STATE_LISTEN},
    {"READ", "TXT", PJ_UI_STATE_READ},
};

static const menu_item_t TIME_MENU[] = {
    {"ALARM", "BELL", PJ_UI_STATE_ALARM},
    {"STOPWATCH", "WATCH", PJ_UI_STATE_STOPWATCH},
    {"TIMER", "SAND", PJ_UI_STATE_TIMER},
    {"INTERVAL", "LOOP", PJ_UI_STATE_INTERVAL},
};

static const menu_item_t SETTINGS_MENU[] = {
    {"SYNC", "SYNC", PJ_UI_STATE_SYNC},
    {"VOLUME", "VOL", PJ_UI_STATE_SETTINGS},
    {"DARK", "MODE", PJ_UI_STATE_SETTINGS},
};

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
    ctx->dirty.x = x;
    ctx->dirty.y = y;
    ctx->dirty.width = width;
    ctx->dirty.height = height;
    ctx->dirty.partial = 1;
}

static void fb_clear(pj_framebuffer_t *fb)
{
    memset(fb->pixels, 0, sizeof(fb->pixels));
}

static void fb_set(pj_framebuffer_t *fb, int x, int y, int black)
{
    if (x < 0 || y < 0 || x >= PJ_DISPLAY_WIDTH || y >= PJ_DISPLAY_HEIGHT) {
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

static void fill_rect(pj_framebuffer_t *fb, int x, int y, int w, int h)
{
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            fb_set(fb, xx, yy, 1);
        }
    }
}

static void invert_framebuffer(pj_framebuffer_t *fb)
{
    for (size_t i = 0; i < sizeof(fb->pixels); i++) {
        fb->pixels[i] = (uint8_t)~fb->pixels[i];
    }
}

static void draw_triangle_left(pj_framebuffer_t *fb, int x, int y, int size)
{
    for (int row = 0; row < size; row++) {
        int width = row + 1;
        for (int col = 0; col < width; col++) {
            fb_set(fb, x + col, y + (size / 2) - row + col, 1);
            fb_set(fb, x + col, y + (size / 2) + row - col, 1);
        }
    }
}

static void draw_round_rect(pj_framebuffer_t *fb, int x, int y, int w, int h)
{
    draw_hline(fb, x + 4, x + w - 5, y);
    draw_hline(fb, x + 4, x + w - 5, y + h - 1);
    draw_vline(fb, x, y + 4, y + h - 5);
    draw_vline(fb, x + w - 1, y + 4, y + h - 5);
    fb_set(fb, x + 2, y + 2, 1);
    fb_set(fb, x + w - 3, y + 2, 1);
    fb_set(fb, x + 2, y + h - 3, 1);
    fb_set(fb, x + w - 3, y + h - 3, 1);
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

static void draw_back(pj_framebuffer_t *fb)
{
    draw_triangle_left(fb, 8, 7, 16);
}

static void draw_menu(pj_framebuffer_t *fb, const menu_item_t *items, size_t count)
{
    int top = 42;
    int row_h = (PJ_DISPLAY_HEIGHT - top - 8) / (int)count;
    for (size_t i = 0; i < count; i++) {
        int y = top + (int)i * row_h;
        draw_round_rect(fb, 12, y, 176, row_h - 4);
        draw_text(fb, 24, y + ((row_h - 4) / 2) - 4, items[i].icon, 1);
        draw_text(fb, 68, y + ((row_h - 4) / 2) - 7, items[i].label, 2);
    }
}

static int menu_hit(const menu_item_t *items, size_t count, int y, pj_ui_state_t *next)
{
    int top = 42;
    if (y < top || y >= PJ_DISPLAY_HEIGHT - 8) {
        return 0;
    }
    int row_h = (PJ_DISPLAY_HEIGHT - top - 8) / (int)count;
    int index = (y - top) / row_h;
    if (index < 0 || (size_t)index >= count) {
        return 0;
    }
    *next = items[index].next;
    return 1;
}

void pj_ui_init(pj_ui_context_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = PJ_UI_STATE_STATIC;
    ctx->volume = 5;
    ctx->sync_pending = 3;
    ctx->sync_transferred = 0;
    ctx->battery_percent = 84;
    ctx->record_state = PJ_RECORD_IDLE;
    ctx->note_count = 3;
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

const char *pj_ui_default_font_name(void)
{
    return "Space Mono Bold";
}

static void clamp_volume(pj_ui_context_t *ctx)
{
    if (ctx->volume < 0) {
        ctx->volume = 0;
    } else if (ctx->volume > 10) {
        ctx->volume = 10;
    }
}

int pj_ui_handle_touch(pj_ui_context_t *ctx, int x, int y, pj_touch_kind_t kind)
{
    pj_ui_state_t next = ctx->state;
    mark_full(ctx);

    if (kind == PJ_TOUCH_SWIPE_RIGHT) {
        next = pj_ui_parent_state(ctx->state);
    } else if (kind != PJ_TOUCH_TAP) {
        return 0;
    } else if (x >= 0 && x < 42 && y >= 0 && y < 32 && ctx->state != PJ_UI_STATE_STATIC) {
        next = pj_ui_parent_state(ctx->state);
    } else {
        switch (ctx->state) {
        case PJ_UI_STATE_STATIC:
            next = PJ_UI_STATE_TIME_TEMP;
            break;
        case PJ_UI_STATE_TIME_TEMP:
            next = PJ_UI_STATE_HOME;
            break;
        case PJ_UI_STATE_HOME:
            (void)menu_hit(HOME_MENU, sizeof(HOME_MENU) / sizeof(HOME_MENU[0]), y, &next);
            break;
        case PJ_UI_STATE_NOTES:
            (void)menu_hit(NOTES_MENU, sizeof(NOTES_MENU) / sizeof(NOTES_MENU[0]), y, &next);
            break;
        case PJ_UI_STATE_TIME:
            (void)menu_hit(TIME_MENU, sizeof(TIME_MENU) / sizeof(TIME_MENU[0]), y, &next);
            break;
        case PJ_UI_STATE_SETTINGS:
            if (y >= 92 && y < 142) {
                ctx->volume += x < 100 ? -1 : 1;
                clamp_volume(ctx);
                mark_partial(ctx, 20, 92, 160, 50);
                return 1;
            }
            if (y >= 142) {
                ctx->dark_mode = !ctx->dark_mode;
                mark_partial(ctx, 20, 142, 160, 42);
                return 1;
            }
            if (menu_hit(SETTINGS_MENU, sizeof(SETTINGS_MENU) / sizeof(SETTINGS_MENU[0]), y, &next)) {
                if (next == PJ_UI_STATE_SETTINGS) {
                    return 0;
                }
            }
            break;
        case PJ_UI_STATE_SYNC:
            if (ctx->sync_pending > 0) {
                ctx->sync_pending--;
                ctx->sync_transferred++;
                mark_partial(ctx, 20, 72, 160, 70);
                return 1;
            }
            break;
        case PJ_UI_STATE_VOLUME:
            if (x < 100) {
                ctx->volume--;
            } else {
                ctx->volume++;
            }
            clamp_volume(ctx);
            mark_partial(ctx, 24, 88, 152, 62);
            return 1;
        case PJ_UI_STATE_RECORD:
            if (y < 62) {
                break;
            }
            if (y > 152) {
                ctx->record_state = PJ_RECORD_IDLE;
                next = PJ_UI_STATE_NOTES;
                break;
            }
            if (ctx->record_state == PJ_RECORD_IDLE) {
                ctx->record_state = PJ_RECORD_ACTIVE;
            } else if (ctx->record_state == PJ_RECORD_ACTIVE) {
                ctx->record_state = PJ_RECORD_PAUSED;
            } else {
                ctx->record_state = PJ_RECORD_ACTIVE;
            }
            mark_partial(ctx, 14, 62, 172, 100);
            return 1;
        case PJ_UI_STATE_LISTEN:
        case PJ_UI_STATE_READ:
            if (y >= 45 && y < 160) {
                next = PJ_UI_STATE_NOTE_DETAIL;
            }
            break;
        default:
            break;
        }
    }

    if (next != ctx->state) {
        ctx->state = next;
        if (next == PJ_UI_STATE_RECORD) {
            ctx->record_state = PJ_RECORD_ACTIVE;
        }
        return 1;
    }
    return 0;
}

static void draw_home_static(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    (void)ctx;
    draw_round_rect(fb, 34, 34, 132, 132);
    fill_rect(fb, 70, 78, 14, 14);
    fill_rect(fb, 116, 78, 14, 14);
    for (int x = 70; x <= 130; x++) {
        int dx = x - 100;
        int y = 118 + (dx * dx) / 130;
        fb_set(fb, x, y, 1);
        fb_set(fb, x, y + 1, 1);
    }
}

static void draw_settings(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    draw_back(fb);
    draw_round_rect(fb, 12, 42, 176, 38);
    draw_text(fb, 24, 52, "SYNC", 1);
    draw_text(fb, 72, 49, "SYNC", 2);

    draw_round_rect(fb, 12, 88, 176, 46);
    draw_text(fb, 24, 102, "-", 2);
    draw_text(fb, 160, 102, "+", 2);
    draw_rect(fb, 56, 105, 88, 14);
    fill_rect(fb, 60, 109, ctx->volume * 8, 6);

    draw_round_rect(fb, 12, 142, 176, 38);
    draw_text(fb, 24, 152, "MODE", 1);
    draw_text(fb, 72, 149, "DARK", 2);
}

static void draw_sync(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    draw_back(fb);
    draw_centered_text(fb, 48, "SYNC", 3);
    draw_text(fb, 24, 90, "PENDING", 2);
    draw_char(fb, 132, 90, (char)('0' + ctx->sync_pending), 2);
    draw_text(fb, 24, 122, "SENT", 2);
    draw_char(fb, 132, 122, (char)('0' + ctx->sync_transferred), 2);
}

static void draw_record(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    draw_back(fb);
    draw_centered_text(fb, 8, "SAT 06/06 09:41", 1);
    fill_rect(fb, 86, 42, 28, 28);
    if (ctx->record_state == PJ_RECORD_PAUSED) {
        draw_centered_text(fb, 82, "PAUSED", 2);
    } else {
        draw_centered_text(fb, 82, "REC", 3);
    }
    draw_round_rect(fb, 14, 142, 52, 36);
    draw_round_rect(fb, 74, 142, 52, 36);
    draw_round_rect(fb, 134, 142, 52, 36);
    draw_text(fb, 28, 153, "II", 2);
    draw_text(fb, 93, 153, ">", 2);
    draw_text(fb, 150, 153, "V", 2);
}

static void draw_notes_list(pj_framebuffer_t *fb, const char *kind)
{
    draw_back(fb);
    for (int i = 0; i < 3; i++) {
        int y = 44 + i * 48;
        draw_round_rect(fb, 14, y, 172, 38);
        draw_text(fb, 24, y + 8, kind, 1);
        draw_text(fb, 72, y + 8, i == 0 ? "SAT 09:41" : (i == 1 ? "FRI 18:12" : "THU 07:30"), 1);
    }
}

void pj_ui_render(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    fb_clear(fb);

    switch (ctx->state) {
    case PJ_UI_STATE_STATIC:
        draw_home_static(ctx, fb);
        break;
    case PJ_UI_STATE_TIME_TEMP:
    {
        char battery[12];
        (void)snprintf(battery, sizeof(battery), "%d%% BAT", ctx->battery_percent);
        draw_back(fb);
        draw_centered_text(fb, 44, "SAT 06/06", 2);
        draw_centered_text(fb, 76, "09:41", 2);
        draw_centered_text(fb, 108, "22C / 72F", 2);
        draw_centered_text(fb, 140, battery, 2);
        break;
    }
    case PJ_UI_STATE_HOME:
        draw_back(fb);
        draw_menu(fb, HOME_MENU, sizeof(HOME_MENU) / sizeof(HOME_MENU[0]));
        break;
    case PJ_UI_STATE_NOTES:
        draw_back(fb);
        draw_menu(fb, NOTES_MENU, sizeof(NOTES_MENU) / sizeof(NOTES_MENU[0]));
        break;
    case PJ_UI_STATE_TIME:
        draw_back(fb);
        draw_menu(fb, TIME_MENU, sizeof(TIME_MENU) / sizeof(TIME_MENU[0]));
        break;
    case PJ_UI_STATE_SETTINGS:
        draw_settings(ctx, fb);
        break;
    case PJ_UI_STATE_RECORD:
        draw_record(ctx, fb);
        break;
    case PJ_UI_STATE_LISTEN:
        draw_notes_list(fb, "AUD");
        break;
    case PJ_UI_STATE_READ:
        draw_notes_list(fb, "TXT");
        break;
    case PJ_UI_STATE_ALARM:
        draw_back(fb);
        draw_centered_text(fb, 60, "07:30", 3);
        draw_round_rect(fb, 18, 142, 48, 36);
        draw_round_rect(fb, 76, 142, 48, 36);
        draw_round_rect(fb, 134, 142, 48, 36);
        draw_text(fb, 28, 154, "-30", 1);
        draw_text(fb, 92, 154, "ON", 1);
        draw_text(fb, 144, 154, "+30", 1);
        break;
    case PJ_UI_STATE_STOPWATCH:
        draw_back(fb);
        draw_centered_text(fb, 66, "00:00", 3);
        draw_centered_text(fb, 128, "START RESET", 1);
        break;
    case PJ_UI_STATE_TIMER:
        draw_back(fb);
        draw_centered_text(fb, 66, "05:00", 3);
        draw_round_rect(fb, 18, 142, 48, 36);
        draw_round_rect(fb, 76, 142, 48, 36);
        draw_round_rect(fb, 134, 142, 48, 36);
        draw_text(fb, 34, 154, "-1", 1);
        draw_text(fb, 92, 154, "GO", 1);
        draw_text(fb, 150, 154, "+1", 1);
        break;
    case PJ_UI_STATE_INTERVAL:
        draw_back(fb);
        draw_centered_text(fb, 58, "ROUND 1", 2);
        draw_centered_text(fb, 96, "25:00", 3);
        draw_round_rect(fb, 18, 142, 48, 36);
        draw_round_rect(fb, 76, 142, 48, 36);
        draw_round_rect(fb, 134, 142, 48, 36);
        draw_text(fb, 34, 154, "-1", 1);
        draw_text(fb, 92, 154, "GO", 1);
        draw_text(fb, 150, 154, "+1", 1);
        break;
    case PJ_UI_STATE_CALENDAR:
        draw_back(fb);
        draw_centered_text(fb, 34, "SAT 06/06", 2);
        draw_text(fb, 24, 72, "09 DESIGN", 1);
        draw_text(fb, 24, 104, "11 SYNC", 1);
        draw_text(fb, 24, 136, "15 WALK", 1);
        break;
    case PJ_UI_STATE_TBD:
        draw_back(fb);
        draw_centered_text(fb, 80, STATE_META[ctx->state].title, 2);
        draw_centered_text(fb, 132, "V1", 2);
        break;
    case PJ_UI_STATE_NOTE_DETAIL:
        draw_back(fb);
        draw_centered_text(fb, 34, "SAT 09:41", 2);
        draw_text(fb, 18, 82, "REMEMBER TO TEST", 1);
        draw_text(fb, 18, 104, "THE RECORD FLOW", 1);
        draw_text(fb, 18, 126, "BEFORE HARDWARE", 1);
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
