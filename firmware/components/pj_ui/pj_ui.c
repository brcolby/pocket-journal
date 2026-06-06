#include "pj_ui.h"

#include <ctype.h>
#include <string.h>

typedef struct {
    pj_ui_state_t state;
    const char *name;
    const char *title;
    pj_ui_state_t parent;
} state_meta_t;

typedef struct {
    const char *label;
    pj_ui_state_t next;
} menu_item_t;

static const state_meta_t STATE_META[PJ_UI_STATE_COUNT] = {
    [PJ_UI_STATE_STATIC] = {PJ_UI_STATE_STATIC, "static", "POCKET", PJ_UI_STATE_STATIC},
    [PJ_UI_STATE_TIME_TEMP] = {PJ_UI_STATE_TIME_TEMP, "time_temp", "TIME/TEMP", PJ_UI_STATE_STATIC},
    [PJ_UI_STATE_HOME] = {PJ_UI_STATE_HOME, "home", "HOME", PJ_UI_STATE_STATIC},
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
};

static const menu_item_t HOME_MENU[] = {
    {"NOTES", PJ_UI_STATE_NOTES},
    {"TIME", PJ_UI_STATE_TIME},
    {"SETTINGS", PJ_UI_STATE_SETTINGS},
    {"CALENDAR", PJ_UI_STATE_CALENDAR},
    {"TBD", PJ_UI_STATE_TBD},
};

static const menu_item_t NOTES_MENU[] = {
    {"RECORD", PJ_UI_STATE_RECORD},
    {"LISTEN", PJ_UI_STATE_LISTEN},
    {"READ", PJ_UI_STATE_READ},
};

static const menu_item_t TIME_MENU[] = {
    {"ALARM", PJ_UI_STATE_ALARM},
    {"STOPWATCH", PJ_UI_STATE_STOPWATCH},
    {"TIMER", PJ_UI_STATE_TIMER},
    {"INTERVAL", PJ_UI_STATE_INTERVAL},
};

static const menu_item_t SETTINGS_MENU[] = {
    {"SYNC", PJ_UI_STATE_SYNC},
    {"VOLUME", PJ_UI_STATE_VOLUME},
};

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

static const uint8_t *glyph_rows(char c)
{
    static const uint8_t blank[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t question[7] = {14, 17, 1, 2, 4, 0, 4};

    switch ((char)toupper((unsigned char)c)) {
    case 'A': { static const uint8_t r[7] = {14, 17, 17, 31, 17, 17, 17}; return r; }
    case 'B': { static const uint8_t r[7] = {30, 17, 17, 30, 17, 17, 30}; return r; }
    case 'C': { static const uint8_t r[7] = {14, 17, 16, 16, 16, 17, 14}; return r; }
    case 'D': { static const uint8_t r[7] = {30, 17, 17, 17, 17, 17, 30}; return r; }
    case 'E': { static const uint8_t r[7] = {31, 16, 16, 30, 16, 16, 31}; return r; }
    case 'F': { static const uint8_t r[7] = {31, 16, 16, 30, 16, 16, 16}; return r; }
    case 'G': { static const uint8_t r[7] = {14, 17, 16, 23, 17, 17, 15}; return r; }
    case 'H': { static const uint8_t r[7] = {17, 17, 17, 31, 17, 17, 17}; return r; }
    case 'I': { static const uint8_t r[7] = {14, 4, 4, 4, 4, 4, 14}; return r; }
    case 'J': { static const uint8_t r[7] = {1, 1, 1, 1, 17, 17, 14}; return r; }
    case 'K': { static const uint8_t r[7] = {17, 18, 20, 24, 20, 18, 17}; return r; }
    case 'L': { static const uint8_t r[7] = {16, 16, 16, 16, 16, 16, 31}; return r; }
    case 'M': { static const uint8_t r[7] = {17, 27, 21, 21, 17, 17, 17}; return r; }
    case 'N': { static const uint8_t r[7] = {17, 25, 21, 19, 17, 17, 17}; return r; }
    case 'O': { static const uint8_t r[7] = {14, 17, 17, 17, 17, 17, 14}; return r; }
    case 'P': { static const uint8_t r[7] = {30, 17, 17, 30, 16, 16, 16}; return r; }
    case 'Q': { static const uint8_t r[7] = {14, 17, 17, 17, 21, 18, 13}; return r; }
    case 'R': { static const uint8_t r[7] = {30, 17, 17, 30, 20, 18, 17}; return r; }
    case 'S': { static const uint8_t r[7] = {15, 16, 16, 14, 1, 1, 30}; return r; }
    case 'T': { static const uint8_t r[7] = {31, 4, 4, 4, 4, 4, 4}; return r; }
    case 'U': { static const uint8_t r[7] = {17, 17, 17, 17, 17, 17, 14}; return r; }
    case 'V': { static const uint8_t r[7] = {17, 17, 17, 17, 17, 10, 4}; return r; }
    case 'W': { static const uint8_t r[7] = {17, 17, 17, 21, 21, 21, 10}; return r; }
    case 'X': { static const uint8_t r[7] = {17, 17, 10, 4, 10, 17, 17}; return r; }
    case 'Y': { static const uint8_t r[7] = {17, 17, 10, 4, 4, 4, 4}; return r; }
    case 'Z': { static const uint8_t r[7] = {31, 1, 2, 4, 8, 16, 31}; return r; }
    case '0': { static const uint8_t r[7] = {14, 17, 19, 21, 25, 17, 14}; return r; }
    case '1': { static const uint8_t r[7] = {4, 12, 4, 4, 4, 4, 14}; return r; }
    case '2': { static const uint8_t r[7] = {14, 17, 1, 2, 4, 8, 31}; return r; }
    case '3': { static const uint8_t r[7] = {30, 1, 1, 14, 1, 1, 30}; return r; }
    case '4': { static const uint8_t r[7] = {2, 6, 10, 18, 31, 2, 2}; return r; }
    case '5': { static const uint8_t r[7] = {31, 16, 16, 30, 1, 1, 30}; return r; }
    case '6': { static const uint8_t r[7] = {14, 16, 16, 30, 17, 17, 14}; return r; }
    case '7': { static const uint8_t r[7] = {31, 1, 2, 4, 8, 8, 8}; return r; }
    case '8': { static const uint8_t r[7] = {14, 17, 17, 14, 17, 17, 14}; return r; }
    case '9': { static const uint8_t r[7] = {14, 17, 17, 15, 1, 1, 14}; return r; }
    case ':': { static const uint8_t r[7] = {0, 4, 4, 0, 4, 4, 0}; return r; }
    case '/': { static const uint8_t r[7] = {1, 1, 2, 4, 8, 16, 16}; return r; }
    case '-': { static const uint8_t r[7] = {0, 0, 0, 31, 0, 0, 0}; return r; }
    case '.': { static const uint8_t r[7] = {0, 0, 0, 0, 0, 12, 12}; return r; }
    case ' ': return blank;
    default: return question;
    }
}

static void draw_char(pj_framebuffer_t *fb, int x, int y, char c, int scale)
{
    const uint8_t *rows = glyph_rows(c);
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if ((rows[row] >> (4 - col)) & 1u) {
                fill_rect(fb, x + col * scale, y + row * scale, scale, scale);
            }
        }
    }
}

static void draw_text(pj_framebuffer_t *fb, int x, int y, const char *text, int scale)
{
    int cursor = x;
    while (*text != '\0') {
        draw_char(fb, cursor, y, *text, scale);
        cursor += 6 * scale;
        text++;
    }
}

static void draw_centered_text(pj_framebuffer_t *fb, int y, const char *text, int scale)
{
    int width = (int)strlen(text) * 6 * scale - scale;
    int x = (PJ_DISPLAY_WIDTH - width) / 2;
    draw_text(fb, x < 0 ? 0 : x, y, text, scale);
}

static void draw_header(pj_framebuffer_t *fb, const char *title, int show_back)
{
    draw_rect(fb, 0, 0, PJ_DISPLAY_WIDTH, 28);
    draw_centered_text(fb, 8, title, 2);
    if (show_back) {
        draw_text(fb, 6, 8, "<", 2);
    }
}

static void draw_menu(pj_framebuffer_t *fb, const menu_item_t *items, size_t count)
{
    int top = 36;
    int row_h = (PJ_DISPLAY_HEIGHT - top - 8) / (int)count;
    for (size_t i = 0; i < count; i++) {
        int y = top + (int)i * row_h;
        draw_rect(fb, 12, y, 176, row_h - 4);
        draw_text(fb, 24, y + 10, items[i].label, 2);
    }
}

static int menu_hit(const menu_item_t *items, size_t count, int y, pj_ui_state_t *next)
{
    int top = 36;
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
    ctx->state = PJ_UI_STATE_STATIC;
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

int pj_ui_handle_touch(pj_ui_context_t *ctx, int x, int y, pj_touch_kind_t kind)
{
    pj_ui_state_t next = ctx->state;

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
            next = y < 100 ? PJ_UI_STATE_STATIC : PJ_UI_STATE_HOME;
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
            (void)menu_hit(SETTINGS_MENU, sizeof(SETTINGS_MENU) / sizeof(SETTINGS_MENU[0]), y, &next);
            break;
        default:
            break;
        }
    }

    if (next != ctx->state) {
        ctx->state = next;
        return 1;
    }
    return 0;
}

void pj_ui_render(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    fb_clear(fb);

    switch (ctx->state) {
    case PJ_UI_STATE_STATIC:
        draw_rect(fb, 0, 0, PJ_DISPLAY_WIDTH, PJ_DISPLAY_HEIGHT);
        draw_centered_text(fb, 58, "POCKET", 3);
        draw_centered_text(fb, 88, "JOURNAL", 3);
        draw_centered_text(fb, 150, "TAP", 2);
        break;
    case PJ_UI_STATE_TIME_TEMP:
        draw_header(fb, "TIME/TEMP", 1);
        draw_centered_text(fb, 58, "09:41", 4);
        draw_centered_text(fb, 112, "72F", 3);
        draw_centered_text(fb, 162, "TAP HOME", 2);
        break;
    case PJ_UI_STATE_HOME:
        draw_header(fb, "HOME", 1);
        draw_menu(fb, HOME_MENU, sizeof(HOME_MENU) / sizeof(HOME_MENU[0]));
        break;
    case PJ_UI_STATE_NOTES:
        draw_header(fb, "NOTES", 1);
        draw_menu(fb, NOTES_MENU, sizeof(NOTES_MENU) / sizeof(NOTES_MENU[0]));
        break;
    case PJ_UI_STATE_TIME:
        draw_header(fb, "TIME", 1);
        draw_menu(fb, TIME_MENU, sizeof(TIME_MENU) / sizeof(TIME_MENU[0]));
        break;
    case PJ_UI_STATE_SETTINGS:
        draw_header(fb, "SETTINGS", 1);
        draw_menu(fb, SETTINGS_MENU, sizeof(SETTINGS_MENU) / sizeof(SETTINGS_MENU[0]));
        break;
    case PJ_UI_STATE_RECORD:
        draw_header(fb, "RECORD", 1);
        draw_centered_text(fb, 74, "READY", 3);
        draw_centered_text(fb, 130, "TAP TO START", 2);
        break;
    case PJ_UI_STATE_LISTEN:
        draw_header(fb, "LISTEN", 1);
        draw_centered_text(fb, 82, "AUDIO", 3);
        draw_centered_text(fb, 134, "0 FILES", 2);
        break;
    case PJ_UI_STATE_READ:
        draw_header(fb, "READ", 1);
        draw_centered_text(fb, 82, "NOTES", 3);
        draw_centered_text(fb, 134, "0 NEW", 2);
        break;
    case PJ_UI_STATE_ALARM:
    case PJ_UI_STATE_STOPWATCH:
    case PJ_UI_STATE_TIMER:
    case PJ_UI_STATE_INTERVAL:
    case PJ_UI_STATE_SYNC:
    case PJ_UI_STATE_VOLUME:
    case PJ_UI_STATE_CALENDAR:
    case PJ_UI_STATE_TBD:
        draw_header(fb, STATE_META[ctx->state].title, 1);
        draw_centered_text(fb, 80, STATE_META[ctx->state].title, 2);
        draw_centered_text(fb, 132, "V1", 2);
        break;
    default:
        draw_centered_text(fb, 90, "UNKNOWN", 2);
        break;
    }
}

