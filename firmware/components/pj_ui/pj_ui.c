#include "pj_ui.h"
#include "pj_default_static_art.h"
#include "pj_icon_carbon.h"
#include "pj_glyph_carbon.h"
#include "pj_font_ibm_plex_mono_bold.h"
#include "pj_layout_geometry.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define PJ_UI_MAX_DURATION_SECONDS 86400
#define PJ_UI_NOTES_PER_PAGE 3
#define PJ_UI_NOTE_PAGER_TOP 150
#define PJ_UI_NOTE_MIN_TEXT_SCALE 3
#define PJ_UI_RULE_WIDTH 4
#define PJ_UI_FONT_SCALE_COUNT 4
#define PJ_UI_TIME_CONTROLS_TOP 100
#define PJ_UI_ALARM_TOGGLE_TOP 72
#define PJ_UI_ALARM_CONTROLS_TOP 112

_Static_assert(PJ_DEFAULT_STATIC_ART_WIDTH == PJ_DISPLAY_WIDTH,
               "default static art width must match the display");
_Static_assert(PJ_DEFAULT_STATIC_ART_HEIGHT == PJ_DISPLAY_HEIGHT,
               "default static art height must match the display");
_Static_assert(PJ_DEFAULT_STATIC_ART_BYTES == PJ_FRAMEBUFFER_BYTES,
               "default static art packing must match the framebuffer");

typedef struct {
    pj_ui_state_t state;
    const char *name;
    const char *title;
    pj_ui_state_t parent;
} state_meta_t;

typedef struct {
    const char *label;
    pj_carbon_icon_id_t icon;
    pj_ui_state_t next;
    pj_layout_slot_id_t slot;
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
    [PJ_UI_STATE_NOTE_DETAIL] = {PJ_UI_STATE_NOTE_DETAIL, "note_detail", "Note", PJ_UI_STATE_LISTEN},
};

static const tile_t HOME_TILES[] = {
    {"Time", PJ_CARBON_ICON_TIME, PJ_UI_STATE_TIME,
     PJ_LAYOUT_SLOT_HOME_TIME},
    {"Notes", PJ_CARBON_ICON_DATA_ENRICHMENT, PJ_UI_STATE_NOTES,
     PJ_LAYOUT_SLOT_HOME_NOTES},
    {"Settings", PJ_CARBON_ICON_SERVICE_LEVELS, PJ_UI_STATE_SETTINGS,
     PJ_LAYOUT_SLOT_HOME_SETTINGS},
};

static const tile_t NOTES_TILES[] = {
    {"Record", PJ_CARBON_ICON_WAVEFORM, PJ_UI_STATE_RECORD,
     PJ_LAYOUT_SLOT_NOTES_RECORD},
    {"Listen", PJ_CARBON_ICON_HEARING, PJ_UI_STATE_LISTEN,
     PJ_LAYOUT_SLOT_NOTES_LISTEN},
    {"Read", PJ_CARBON_ICON_VIEW_FILLED, PJ_UI_STATE_READ,
     PJ_LAYOUT_SLOT_NOTES_READ},
};

static const tile_t TIME_TILES[] = {
    {"Alarm", PJ_CARBON_ICON_ALARM, PJ_UI_STATE_ALARM,
     PJ_LAYOUT_SLOT_TIME_ALARM},
    {"Stopwatch", PJ_CARBON_ICON_TIMER, PJ_UI_STATE_STOPWATCH,
     PJ_LAYOUT_SLOT_TIME_STOPWATCH},
    {"Timer", PJ_CARBON_ICON_HOURGLASS, PJ_UI_STATE_TIMER,
     PJ_LAYOUT_SLOT_TIME_TIMER},
    {"Interval", PJ_CARBON_ICON_REPEAT, PJ_UI_STATE_INTERVAL,
     PJ_LAYOUT_SLOT_TIME_INTERVAL},
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
    static const char *names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    if (weekday < 0 || weekday > 6) {
        return "---";
    }
    return names[weekday];
}

static uint32_t next_generation(uint32_t generation);

static void mark_full(pj_ui_context_t *ctx)
{
    ctx->visual_revision = next_generation(ctx->visual_revision);
    ctx->full_refresh_revision = next_generation(ctx->full_refresh_revision);
}

static void visual_changed(pj_ui_context_t *ctx)
{
    ctx->visual_revision = next_generation(ctx->visual_revision);
}

static uint32_t next_generation(uint32_t generation)
{
    generation++;
    return generation == 0 ? 1 : generation;
}

static void interaction_changed(pj_ui_context_t *ctx)
{
    ctx->interaction_generation =
        next_generation(ctx->interaction_generation);
}

static void sync_presentation_changed(pj_ui_context_t *ctx)
{
    ctx->sync_presentation_generation =
        next_generation(ctx->sync_presentation_generation);
    interaction_changed(ctx);
}

static void begin_sync_session(pj_ui_context_t *ctx)
{
    ctx->sync_session_generation = next_generation(ctx->sync_session_generation);
    ctx->sync_inventory_state = PJ_UI_SYNC_INVENTORY_PENDING;
    ctx->sync_pending = 0;
    ctx->sync_transferred = 0;
    ctx->sync_failed = 0;
    ctx->sync_request_pending = 0;
    ctx->sync_inventory_presentation_generation = 0;
    ctx->sync_success_presentation_generation = 0;
    ctx->sync_transfer_requested_generation = 0;
    ctx->sync_preflight_request_pending = 1;
    ctx->sync_transfer_request_pending = 0;
    ctx->sync_success_return_pending = 0;
    (void)snprintf(ctx->sync_phase, sizeof(ctx->sync_phase), "%s", "inventory");
    ctx->sync_error[0] = '\0';
    sync_presentation_changed(ctx);
}

static void set_state(pj_ui_context_t *ctx, pj_ui_state_t state)
{
    if (state >= 0 && state < PJ_UI_STATE_COUNT) {
        pj_ui_state_t previous = ctx->state;
        ctx->state = state;
        if (previous != state) {
            ctx->layout_epoch = next_generation(ctx->layout_epoch);
            interaction_changed(ctx);
        }
        if (state == PJ_UI_STATE_SYNC && previous != PJ_UI_STATE_SYNC) {
            begin_sync_session(ctx);
        } else if (state != PJ_UI_STATE_SYNC && previous == PJ_UI_STATE_SYNC) {
            ctx->sync_preflight_request_pending = 0;
            ctx->sync_transfer_request_pending = 0;
            ctx->sync_success_return_pending = 0;
        }
        mark_full(ctx);
    }
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

static void fill_rect(pj_framebuffer_t *fb, int x, int y, int w, int h)
{
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            fb_set(fb, xx, yy, 1);
        }
    }
}

static void draw_horizontal_rule(pj_framebuffer_t *fb, int y)
{
    fill_rect(fb, 0, y - PJ_UI_RULE_WIDTH / 2,
              PJ_DISPLAY_WIDTH, PJ_UI_RULE_WIDTH);
}

static void draw_vertical_rule(pj_framebuffer_t *fb, int x, int y, int height)
{
    fill_rect(fb, x - PJ_UI_RULE_WIDTH / 2, y,
              PJ_UI_RULE_WIDTH, height);
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
    int column = index < 2 ? 0 : 1;
    int row = index % 2;
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
}

static int font_size_for_scale(int scale)
{
    static const int sizes[PJ_UI_FONT_SCALE_COUNT] = {16, 24, 32, 64};
    if (scale < 1) {
        scale = 1;
    } else if (scale > PJ_UI_FONT_SCALE_COUNT) {
        scale = PJ_UI_FONT_SCALE_COUNT;
    }
    return sizes[scale - 1];
}

static const pj_asset_glyph_t *glyph_for_char(char c, int scale)
{
    uint32_t codepoint = (uint8_t)c;
    int size = font_size_for_scale(scale);
    const pj_asset_glyph_t *glyph =
        pj_carbon_glyph_lookup_codepoint(codepoint, (uint16_t)size);
    if (glyph == NULL) {
        glyph = pj_ibm_plex_punctuation_lookup(codepoint, (uint16_t)size);
    }
    return glyph;
}

static int glyph_pixel_is_set(const pj_asset_glyph_t *glyph, int x, int y)
{
    uint8_t byte = glyph->data[(size_t)y * glyph->stride + (size_t)x / 8u];
    return (byte & (uint8_t)(0x80u >> (x % 8))) != 0;
}

static void draw_glyph(pj_framebuffer_t *fb, int x, int y,
                       const pj_asset_glyph_t *glyph)
{
    if (glyph == NULL) {
        return;
    }
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
    int width = 0;
    while (*text != '\0') {
        const pj_asset_glyph_t *glyph = glyph_for_char(*text, scale);
        if (glyph != NULL) {
            width += glyph->advance;
        }
        text++;
    }
    return width;
}

static void draw_text(pj_framebuffer_t *fb, int x, int y, const char *text, int scale)
{
    int cursor = x;
    while (*text != '\0') {
        const pj_asset_glyph_t *glyph = glyph_for_char(*text, scale);
        draw_glyph(fb, cursor, y, glyph);
        if (glyph != NULL) {
            cursor += glyph->advance;
        }
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
    int width = text_width(text, scale);
    draw_text(fb, cx - width / 2, cy - font_size_for_scale(scale) / 2,
              text, scale);
}

static void draw_carbon_glyph_center_at(pj_framebuffer_t *fb, int cx, int cy,
                                        pj_carbon_glyph_id_t id, int size)
{
    const pj_asset_glyph_t *glyph =
        pj_carbon_glyph_lookup(id, (uint16_t)size);
    if (glyph != NULL) {
        draw_glyph(fb, cx - glyph->width / 2, cy - glyph->height / 2, glyph);
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
    int line_height = font_size_for_scale(scale);

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
                draw_text(fb, x, y + line_number * (line_height + 5), line, scale);
                return;
            }
            char *space = strrchr(line, ' ');
            if (space != NULL) {
                *space = '\0';
                size_t remaining = strlen(space + 1);
                draw_text(fb, x, y + line_number * (line_height + 5), line, scale);
                memmove(line, space + 1, remaining + 1u);
                used = remaining;
            } else {
                draw_text(fb, x, y + line_number * (line_height + 5), line, scale);
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
        draw_text(fb, x, y + line_number * (line_height + 5), line, scale);
    }
}

static void draw_bitmap_center_at(pj_framebuffer_t *fb,
                                  const pj_asset_bitmap_t *asset,
                                  int cx, int cy)
{
    if (asset == NULL) {
        return;
    }
    int left = cx - asset->width / 2;
    int top = cy - asset->height / 2;
    for (int y = 0; y < asset->height; y++) {
        for (int x = 0; x < asset->width; x++) {
            uint8_t byte =
                asset->data[(size_t)y * asset->stride + (size_t)x / 8u];
            if ((byte & (uint8_t)(0x80u >> (x % 8))) != 0) {
                fb_set(fb, left + x, top + y, 1);
            }
        }
    }
}

static void draw_icon(pj_framebuffer_t *fb, pj_carbon_icon_id_t icon,
                      int cx, int cy, int size)
{
    draw_bitmap_center_at(fb,
                          pj_carbon_icon_lookup(icon, (uint16_t)size),
                          cx, cy);
}

static int layout_coordinate_to_pixel(int16_t coordinate)
{
    return (coordinate + PJ_LAYOUT_COORD_SCALE / 2) /
        PJ_LAYOUT_COORD_SCALE;
}

static const pj_layout_slot_t *layout_slot(pj_layout_id_t layout_id,
                                           pj_layout_slot_id_t slot_id)
{
    const pj_layout_geometry_t *geometry = pj_layout_geometry(layout_id);
    if (geometry == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < geometry->slot_count; index++) {
        if (geometry->slots[index].id == slot_id) {
            return &geometry->slots[index];
        }
    }
    return NULL;
}

static void draw_layout_rules(pj_framebuffer_t *fb, pj_layout_id_t layout_id)
{
    for (int y = 0; y < PJ_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
            if (pj_layout_pixel_is_rule(layout_id, (uint16_t)x, (uint16_t)y)) {
                fb_set(fb, x, y, 1);
            }
        }
    }
}

static void draw_icon_menu(pj_framebuffer_t *fb, pj_layout_id_t layout_id,
                           const tile_t *tiles, size_t count)
{
    draw_layout_rules(fb, layout_id);
    for (size_t index = 0; index < count; index++) {
        const pj_layout_slot_t *slot = layout_slot(layout_id, tiles[index].slot);
        if (slot == NULL) {
            continue;
        }
        draw_icon(fb, tiles[index].icon,
                  layout_coordinate_to_pixel(slot->icon_center.x),
                  layout_coordinate_to_pixel(slot->icon_center.y), 64);
    }
}

static void draw_adjustment_controls(pj_framebuffer_t *fb, int top)
{
    static const pj_carbon_icon_id_t actions[] = {
        PJ_CARBON_ICON_CARET_UP, PJ_CARBON_ICON_CARET_UP,
        PJ_CARBON_ICON_CARET_DOWN, PJ_CARBON_ICON_CARET_DOWN,
    };
    for (int i = 0; i < 4; i++) {
        int column = i % 2;
        int row = i / 2;
        int x = column * PJ_DISPLAY_WIDTH / 2;
        int next_x = (column + 1) * PJ_DISPLAY_WIDTH / 2;
        int y = top + row * (PJ_DISPLAY_HEIGHT - top) / 2;
        int next_y = top + (row + 1) * (PJ_DISPLAY_HEIGHT - top) / 2;
        draw_icon(fb, actions[i], (x + next_x) / 2, (y + next_y) / 2,
                  40);
    }
}

static void draw_icon_controls(pj_framebuffer_t *fb,
                               const pj_carbon_icon_id_t icons[],
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
        draw_icon(fb, icons[i], (x + next_x) / 2, (y + next_y) / 2,
                  40);
    }
}

static void draw_timer_controls(pj_framebuffer_t *fb,
                                pj_carbon_icon_id_t play_icon)
{
    const pj_carbon_icon_id_t icons[] = {
        PJ_CARBON_ICON_CARET_UP, PJ_CARBON_ICON_CARET_DOWN,
        play_icon, PJ_CARBON_ICON_RESET_ALT,
    };
    for (int i = 0; i < 4; i++) {
        ui_rect_t control = timer_control_rect(i);
        draw_icon(fb, icons[i], control.x + control.w / 2,
                  control.y + control.h / 2,
                  40);
    }
}

static int tile_hit(pj_layout_id_t layout_id, const tile_t *tiles,
                    size_t count, int x, int y, pj_ui_state_t *next)
{
    pj_layout_slot_id_t slot =
        pj_layout_hit_test(layout_id, (uint16_t)x, (uint16_t)y);
    for (size_t index = 0; index < count; index++) {
        if (tiles[index].slot == slot) {
            *next = tiles[index].next;
            return 1;
        }
    }
    return 0;
}

void pj_ui_init(pj_ui_context_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = PJ_UI_STATE_STATIC;
    ctx->layout_epoch = 1;
    ctx->interaction_generation = 1;
    ctx->visual_revision = 1;
    ctx->full_refresh_revision = 1;
    ctx->volume = 10;
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
    mark_full(ctx);
}

pj_ui_state_t pj_ui_current_state(const pj_ui_context_t *ctx)
{
    return ctx->state;
}

uint32_t pj_ui_interaction_generation(const pj_ui_context_t *ctx)
{
    return ctx == NULL ? 0 : ctx->interaction_generation;
}

uint32_t pj_ui_layout_epoch(const pj_ui_context_t *ctx)
{
    return ctx == NULL ? 0 : ctx->layout_epoch;
}

uint32_t pj_ui_visual_revision(const pj_ui_context_t *ctx)
{
    return ctx == NULL ? 0 : ctx->visual_revision;
}

uint32_t pj_ui_full_refresh_revision(const pj_ui_context_t *ctx)
{
    return ctx == NULL ? 0 : ctx->full_refresh_revision;
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

void pj_ui_request_full_presentation(pj_ui_context_t *ctx)
{
    if (ctx != NULL) mark_full(ctx);
}

const char *pj_ui_default_font_name(void)
{
    return "Carbon glyphs / IBM Plex Mono Bold punctuation";
}

static int notes_per_page(void)
{
    return PJ_UI_NOTES_PER_PAGE;
}

static void restore_selected_note_list(pj_ui_context_t *ctx, pj_ui_state_t state)
{
    ctx->note_page = ctx->selected_note / notes_per_page();
    set_state(ctx, state);
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
    interaction_changed(ctx);
    visual_changed(ctx);
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
    if (ctx->state == PJ_UI_STATE_LISTEN || ctx->state == PJ_UI_STATE_READ ||
        ctx->state == PJ_UI_STATE_NOTE_DETAIL) {
        interaction_changed(ctx);
        visual_changed(ctx);
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
    if (ctx == NULL) {
        return;
    }
    battery_percent = max_int(0, min_int(100, battery_percent));
    humidity_percent = max_int(-1, min_int(100, humidity_percent));
    if (ctx->battery_percent == battery_percent && ctx->temperature_c == temperature_c &&
        ctx->humidity_percent == humidity_percent) {
        return;
    }
    ctx->battery_percent = battery_percent;
    ctx->temperature_c = temperature_c;
    ctx->humidity_percent = humidity_percent;
    if (ctx->state == PJ_UI_STATE_TIME_TEMP) {
        visual_changed(ctx);
    }
}

void pj_ui_apply_preferences(pj_ui_context_t *ctx,
                             const pj_ui_preferences_t *preferences)
{
    if (ctx == NULL || preferences == NULL) {
        return;
    }
    pj_ui_preferences_t next = *preferences;
    next.volume = max_int(0, min_int(10, next.volume));
    next.dark_mode = next.dark_mode != 0;
    next.alarm_enabled = next.alarm_enabled != 0;
    next.alarm_hour = max_int(0, min_int(23, next.alarm_hour));
    next.alarm_minute = max_int(0, min_int(59, next.alarm_minute));
    next.timer_seconds = max_int(30, min_int(PJ_UI_MAX_DURATION_SECONDS,
                                             next.timer_seconds));
    next.interval_seconds = max_int(60, min_int(PJ_UI_MAX_DURATION_SECONDS,
                                                next.interval_seconds));
    next.clock_24h = next.clock_24h != 0;
    next.temperature_fahrenheit = next.temperature_fahrenheit != 0;
    next.transcript_font_size = max_int(2, min_int(3,
                                                   next.transcript_font_size));
    int changed = ctx->volume != next.volume || ctx->dark_mode != next.dark_mode ||
        ctx->alarm_on != next.alarm_enabled || ctx->alarm_hour != next.alarm_hour ||
        ctx->alarm_minute != next.alarm_minute ||
        ctx->timer_preset_seconds != next.timer_seconds ||
        ctx->interval_preset_seconds != next.interval_seconds ||
        ctx->clock_24h != next.clock_24h ||
        ctx->temperature_fahrenheit != next.temperature_fahrenheit ||
        ctx->transcript_font_size != next.transcript_font_size;
    if (!changed) {
        return;
    }
    int dark_changed = ctx->dark_mode != next.dark_mode;
    int semantics_changed = ctx->state == PJ_UI_STATE_SETTINGS ||
        ctx->state == PJ_UI_STATE_VOLUME || ctx->state == PJ_UI_STATE_ALARM ||
        ctx->state == PJ_UI_STATE_TIMER || ctx->state == PJ_UI_STATE_INTERVAL;
    ctx->volume = next.volume;
    ctx->dark_mode = next.dark_mode;
    ctx->alarm_on = next.alarm_enabled;
    ctx->alarm_hour = next.alarm_hour;
    ctx->alarm_minute = next.alarm_minute;
    ctx->timer_preset_seconds = next.timer_seconds;
    ctx->interval_preset_seconds = next.interval_seconds;
    if (!ctx->timer_running) ctx->timer_seconds = next.timer_seconds;
    if (!ctx->interval_running) ctx->interval_seconds = next.interval_seconds;
    ctx->clock_24h = next.clock_24h;
    ctx->temperature_fahrenheit = next.temperature_fahrenheit;
    ctx->transcript_font_size = next.transcript_font_size;
    if (semantics_changed) {
        interaction_changed(ctx);
    }
    if (dark_changed) mark_full(ctx);
    else visual_changed(ctx);
}

void pj_ui_set_sync_state(pj_ui_context_t *ctx, int pending, int transferred, int online)
{
    if (ctx == NULL) {
        return;
    }
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
        sync_presentation_changed(ctx);
        visual_changed(ctx);
    }
}

static void set_sync_detail(pj_ui_context_t *ctx, const char *phase, int failed,
                            const char *error, int request_pending)
{
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
        sync_presentation_changed(ctx);
        if (strcmp(safe_phase, "succeeded") == 0) {
            ctx->sync_success_presentation_generation =
                ctx->sync_presentation_generation;
        } else {
            ctx->sync_success_presentation_generation = 0;
            ctx->sync_success_return_pending = 0;
        }
        visual_changed(ctx);
    }
}

void pj_ui_set_sync_detail(pj_ui_context_t *ctx, const char *phase, int failed,
                           const char *error, int request_pending)
{
    if (ctx == NULL) {
        return;
    }
    set_sync_detail(ctx, phase, failed, error, request_pending);
}

uint32_t pj_ui_sync_session_generation(const pj_ui_context_t *ctx)
{
    return ctx == NULL ? 0 : ctx->sync_session_generation;
}

uint32_t pj_ui_sync_presentation_generation(const pj_ui_context_t *ctx)
{
    return ctx == NULL ? 0 : ctx->sync_presentation_generation;
}

int pj_ui_consume_sync_preflight_request(pj_ui_context_t *ctx, uint32_t *generation)
{
    if (ctx == NULL || generation == NULL || ctx->state != PJ_UI_STATE_SYNC ||
        !ctx->sync_preflight_request_pending) {
        return 0;
    }
    ctx->sync_preflight_request_pending = 0;
    *generation = ctx->sync_session_generation;
    return 1;
}

void pj_ui_set_sync_inventory(pj_ui_context_t *ctx, uint32_t generation,
                              pj_ui_sync_inventory_state_t state, int pending,
                              int transferred, int online)
{
    if (ctx == NULL || ctx->state != PJ_UI_STATE_SYNC || generation == 0 ||
        generation != ctx->sync_session_generation || state < PJ_UI_SYNC_INVENTORY_UNKNOWN ||
        state > PJ_UI_SYNC_INVENTORY_OFFLINE) {
        return;
    }
    if (pending < 0) {
        pending = 0;
    }
    if (transferred < 0) {
        transferred = 0;
    }
    online = online != 0;
    const char *phase = state == PJ_UI_SYNC_INVENTORY_READY ? "ready" :
        state == PJ_UI_SYNC_INVENTORY_OFFLINE ? "offline" :
        state == PJ_UI_SYNC_INVENTORY_PENDING ? "inventory" : "unknown";
    int changed = ctx->sync_inventory_state != state || ctx->sync_pending != pending ||
        ctx->sync_transferred != transferred || ctx->sync_online != online ||
        strcmp(ctx->sync_phase, phase) != 0 || ctx->sync_failed != 0 ||
        ctx->sync_request_pending != 0 || ctx->sync_error[0] != '\0';
    if (!changed) {
        return;
    }
    ctx->sync_inventory_state = state;
    ctx->sync_pending = pending;
    ctx->sync_transferred = transferred;
    ctx->sync_online = online;
    ctx->sync_failed = 0;
    ctx->sync_request_pending = 0;
    (void)snprintf(ctx->sync_phase, sizeof(ctx->sync_phase), "%s", phase);
    ctx->sync_error[0] = '\0';
    sync_presentation_changed(ctx);
    ctx->sync_inventory_presentation_generation =
        ctx->sync_presentation_generation;
    visual_changed(ctx);
}

void pj_ui_set_sync_detail_for_generation(pj_ui_context_t *ctx, uint32_t generation,
                                          const char *phase, int failed,
                                          const char *error, int request_pending)
{
    if (ctx == NULL || ctx->state != PJ_UI_STATE_SYNC || generation == 0 ||
        generation != ctx->sync_session_generation) {
        return;
    }
    set_sync_detail(ctx, phase, failed, error, request_pending);
}

int pj_ui_sync_presentation_committed(pj_ui_context_t *ctx, uint32_t generation)
{
    if (ctx == NULL || ctx->state != PJ_UI_STATE_SYNC || generation == 0 ||
        generation != ctx->sync_presentation_generation) {
        return 0;
    }
    if (ctx->sync_inventory_state == PJ_UI_SYNC_INVENTORY_READY &&
        ctx->sync_inventory_presentation_generation != 0 &&
        strcmp(ctx->sync_phase, "ready") == 0 &&
        ctx->sync_transfer_requested_generation != ctx->sync_session_generation) {
        ctx->sync_transfer_request_pending = 1;
    }
    if (ctx->sync_success_presentation_generation != 0 &&
        strcmp(ctx->sync_phase, "succeeded") == 0) {
        ctx->sync_success_return_pending = 1;
    }
    return 1;
}

int pj_ui_consume_sync_transfer_request(pj_ui_context_t *ctx, uint32_t *generation)
{
    if (ctx == NULL || generation == NULL || ctx->state != PJ_UI_STATE_SYNC ||
        !ctx->sync_transfer_request_pending) {
        return 0;
    }
    ctx->sync_transfer_request_pending = 0;
    ctx->sync_transfer_requested_generation = ctx->sync_session_generation;
    *generation = ctx->sync_session_generation;
    return 1;
}

int pj_ui_consume_sync_success_return(pj_ui_context_t *ctx)
{
    if (ctx == NULL || ctx->state != PJ_UI_STATE_SYNC ||
        !ctx->sync_success_return_pending) {
        return 0;
    }
    ctx->sync_success_return_pending = 0;
    set_state(ctx, pj_ui_parent_state(PJ_UI_STATE_SYNC));
    return 1;
}

void pj_ui_set_time(pj_ui_context_t *ctx, int hour, int minute, int year, int month, int day)
{
    if (ctx->hour == hour && ctx->minute == minute && ctx->year == year && ctx->month == month && ctx->day == day) {
        return;
    }
    ctx->hour = hour;
    ctx->minute = minute;
    ctx->year = year;
    ctx->month = month;
    ctx->day = day;
    ctx->weekday = weekday_from_date(year, month, day);
    if (ctx->state == PJ_UI_STATE_TIME_TEMP) {
        visual_changed(ctx);
    }
}

void pj_ui_set_audio_state(pj_ui_context_t *ctx, int recording, int playback_active)
{
    pj_record_state_t previous_record = ctx->record_state;
    pj_playback_state_t previous_playback = ctx->playback_state;
    pj_record_state_t next_record = recording ?
        (previous_record == PJ_RECORD_STOPPING ? PJ_RECORD_STOPPING : PJ_RECORD_ACTIVE) :
        (previous_record == PJ_RECORD_ARMING ? PJ_RECORD_ARMING : PJ_RECORD_IDLE);
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
            visual_changed(ctx);
        }
        return;
    }
    if (next_playback == PJ_PLAYBACK_IDLE) {
        ctx->playback_exit_pending = 0;
    }
    if (previous_playback != next_playback &&
        (ctx->state == PJ_UI_STATE_NOTE_DETAIL ||
         ctx->state == PJ_UI_STATE_LISTEN)) {
        interaction_changed(ctx);
    }
    if (ctx->state == PJ_UI_STATE_RECORD) {
        visual_changed(ctx);
    } else if (ctx->state == PJ_UI_STATE_NOTE_DETAIL || ctx->state == PJ_UI_STATE_LISTEN) {
        visual_changed(ctx);
    }
}

void pj_ui_set_recording_elapsed(pj_ui_context_t *ctx, uint64_t elapsed_ms)
{
    if (ctx == NULL) {
        return;
    }
    uint64_t elapsed_seconds = elapsed_ms / 1000u;
    int seconds = elapsed_seconds > INT_MAX ? INT_MAX : (int)elapsed_seconds;
    if (ctx->record_state != PJ_RECORD_IDLE && seconds < ctx->recording_seconds) {
        return;
    }
    if (ctx->recording_seconds == seconds) {
        return;
    }
    ctx->recording_seconds = seconds;
    if (ctx->state == PJ_UI_STATE_RECORD) {
        visual_changed(ctx);
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
    int alarm_enabled_changed =
        ctx->alarm_on != (projection->alarm_enabled != 0);
    int alarm_changed = alarm_enabled_changed ||
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
    int current_timer_visible = !ctx->timer_running && ctx->timer_seconds == 0 ?
        ctx->timer_preset_seconds : ctx->timer_seconds;
    int next_timer_visible = !projection->timer_running && next_timer_seconds == 0 ?
        ctx->timer_preset_seconds : next_timer_seconds;
    int current_interval_visible =
        !ctx->interval_running && ctx->interval_seconds == 0 ?
        ctx->interval_preset_seconds : ctx->interval_seconds;
    int next_interval_visible =
        !projection->interval_running && next_interval_seconds == 0 ?
        ctx->interval_preset_seconds : next_interval_seconds;
    int stopwatch_changed = ctx->stopwatch_running != (projection->stopwatch_running != 0) ||
        stopwatch_seconds_changed;
    int timer_changed = ctx->timer_running != (projection->timer_running != 0) ||
        timer_seconds_changed;
    int interval_changed = ctx->interval_running != (projection->interval_running != 0) ||
        interval_value_changed;
    int timer_set_value_changed = !ctx->timer_running &&
        !projection->timer_running &&
        current_timer_visible != next_timer_visible;
    int interval_set_value_changed = !ctx->interval_running &&
        !projection->interval_running &&
        current_interval_visible != next_interval_visible;

    if ((ctx->state == PJ_UI_STATE_ALARM && alarm_changed) ||
        (ctx->state == PJ_UI_STATE_STOPWATCH && stopwatch_running_changed) ||
        (ctx->state == PJ_UI_STATE_TIMER &&
         (timer_running_changed || timer_set_value_changed)) ||
        (ctx->state == PJ_UI_STATE_INTERVAL &&
         (interval_running_changed || interval_set_value_changed))) {
        interaction_changed(ctx);
    }

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
        visual_changed(ctx);
    } else if (ctx->state == PJ_UI_STATE_STOPWATCH && stopwatch_changed) {
        if (stopwatch_seconds_changed) {
            visual_changed(ctx);
        }
        if (stopwatch_running_changed) {
            visual_changed(ctx);
        }
    } else if (ctx->state == PJ_UI_STATE_TIMER && timer_changed) {
        if (timer_seconds_changed) {
            visual_changed(ctx);
        }
        if (timer_running_changed) {
            visual_changed(ctx);
        }
    } else if (ctx->state == PJ_UI_STATE_INTERVAL && interval_changed) {
        if (interval_value_changed) {
            visual_changed(ctx);
        }
        if (interval_running_changed) {
            visual_changed(ctx);
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
         ctx->time_command.type != PJ_UI_TIME_COMMAND_INTERVAL_RESET &&
         ctx->time_command.type != PJ_UI_TIME_COMMAND_INTERVAL_SET)) {
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

static pj_ui_preferences_t current_preferences(const pj_ui_context_t *ctx)
{
    return (pj_ui_preferences_t) {
        .volume = ctx->volume,
        .dark_mode = ctx->dark_mode,
        .alarm_enabled = ctx->alarm_on,
        .alarm_hour = ctx->alarm_hour,
        .alarm_minute = ctx->alarm_minute,
        .timer_seconds = ctx->timer_preset_seconds,
        .interval_seconds = ctx->interval_preset_seconds,
        .clock_24h = ctx->clock_24h,
        .temperature_fahrenheit = ctx->temperature_fahrenheit,
        .transcript_font_size = ctx->transcript_font_size,
    };
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
        break;
    case PJ_UI_STATE_TIMER:
        command = PJ_UI_TIME_COMMAND_TIMER_RESET;
        break;
    case PJ_UI_STATE_INTERVAL:
        command = PJ_UI_TIME_COMMAND_INTERVAL_RESET;
        break;
    default:
        return 0;
    }
    queue_time_command(ctx, command, 0, 0);
    set_state(ctx, PJ_UI_STATE_TIME);
    return 1;
}

static int stop_record_and_return(pj_ui_context_t *ctx)
{
    if (ctx->record_state == PJ_RECORD_ARMING) {
        ctx->record_state = PJ_RECORD_IDLE;
        set_state(ctx, PJ_UI_STATE_HOME);
        return 1;
    }
    if (ctx->record_state != PJ_RECORD_ACTIVE) {
        return 0;
    }
    ctx->record_state = PJ_RECORD_STOPPING;
    set_state(ctx, PJ_UI_STATE_HOME);
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
        return stop_record_and_return(ctx);
    }
    if ((ctx->state == PJ_UI_STATE_LISTEN || ctx->state == PJ_UI_STATE_NOTE_DETAIL) &&
        ctx->playback_state != PJ_PLAYBACK_IDLE) {
        if (ctx->playback_state == PJ_PLAYBACK_STOPPING &&
            ctx->playback_exit_pending) {
            return 0;
        }
        ctx->playback_state = PJ_PLAYBACK_STOPPING;
        ctx->playback_exit_pending = 1;
        if (ctx->state == PJ_UI_STATE_NOTE_DETAIL) {
            restore_selected_note_list(ctx, PJ_UI_STATE_LISTEN);
        } else {
            set_state(ctx, PJ_UI_STATE_NOTES);
        }
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

static int activate_control(pj_ui_context_t *ctx, int control)
{
    switch (ctx->state) {
    case PJ_UI_STATE_ALARM:
        {
        pj_ui_preferences_t preferences = current_preferences(ctx);
        if (control == 0) {
            preferences.alarm_enabled = !preferences.alarm_enabled;
        } else if (control == 1) preferences.alarm_hour = (preferences.alarm_hour + 1) % 24;
        else if (control == 2) preferences.alarm_hour = (preferences.alarm_hour + 23) % 24;
        else if (control == 3) preferences.alarm_minute = (preferences.alarm_minute + 15) % 60;
        else preferences.alarm_minute = (preferences.alarm_minute + 45) % 60;
        pj_ui_apply_preferences(ctx, &preferences);
        return 1;
        }
    case PJ_UI_STATE_STOPWATCH:
        if (ctx->time_command.type != PJ_UI_TIME_COMMAND_NONE) {
            return 0;
        }
        queue_time_command(ctx, control == 1 ?
                           PJ_UI_TIME_COMMAND_STOPWATCH_RESET :
                           (ctx->stopwatch_running ?
                            PJ_UI_TIME_COMMAND_STOPWATCH_PAUSE :
                            PJ_UI_TIME_COMMAND_STOPWATCH_START), 0, 0);
        return 1;
    case PJ_UI_STATE_TIMER:
        if (ctx->time_command.type != PJ_UI_TIME_COMMAND_NONE) {
            return 0;
        }
        if (control == 2) {
            int seconds = ctx->timer_seconds > 0 ?
                ctx->timer_seconds : ctx->timer_preset_seconds;
            queue_time_command(ctx, ctx->timer_running ?
                               PJ_UI_TIME_COMMAND_TIMER_PAUSE :
                               PJ_UI_TIME_COMMAND_TIMER_START,
                               (uint64_t)seconds * 1000u, 0);
        } else if (control == 3) {
            queue_time_command(ctx, PJ_UI_TIME_COMMAND_TIMER_RESET, 0, 0);
        } else {
            int delta = control == 0 ? 30 : -30;
            int base = ctx->timer_seconds > 0 ?
                ctx->timer_seconds : ctx->timer_preset_seconds;
            int preset = max_int(30, min_int(PJ_UI_MAX_DURATION_SECONDS, base + delta));
            queue_time_command(ctx, PJ_UI_TIME_COMMAND_TIMER_SET,
                               (uint64_t)preset * 1000u, 0);
            ctx->timer_seconds = preset;
            ctx->timer_preset_seconds = preset;
            interaction_changed(ctx);
            visual_changed(ctx);
        }
        return 1;
    case PJ_UI_STATE_INTERVAL:
        if (ctx->time_command.type != PJ_UI_TIME_COMMAND_NONE) {
            return 0;
        }
        if (control == 2) {
            int seconds = ctx->interval_seconds > 0 ?
                ctx->interval_seconds : ctx->interval_preset_seconds;
            queue_time_command(ctx, ctx->interval_running ?
                               PJ_UI_TIME_COMMAND_INTERVAL_PAUSE :
                               PJ_UI_TIME_COMMAND_INTERVAL_START,
                               (uint64_t)seconds * 1000u,
                               (uint64_t)ctx->interval_preset_seconds * 1000u);
        } else if (control == 3) {
            queue_time_command(ctx, PJ_UI_TIME_COMMAND_INTERVAL_RESET, 0, 0);
        } else {
            int delta = control == 0 ? 60 : -60;
            int base = ctx->interval_seconds > 0 ?
                ctx->interval_seconds : ctx->interval_preset_seconds;
            int preset = max_int(60, min_int(PJ_UI_MAX_DURATION_SECONDS, base + delta));
            queue_time_command(ctx, PJ_UI_TIME_COMMAND_INTERVAL_SET,
                               (uint64_t)preset * 1000u,
                               (uint64_t)preset * 1000u);
            ctx->interval_seconds = preset;
            ctx->interval_preset_seconds = preset;
            interaction_changed(ctx);
            visual_changed(ctx);
        }
        return 1;
    case PJ_UI_STATE_SETTINGS:
        {
        pj_ui_preferences_t preferences = current_preferences(ctx);
        if (control == 0) {
            set_state(ctx, PJ_UI_STATE_VOLUME);
            return 1;
        }
        if (control == 1) {
            preferences.dark_mode = !preferences.dark_mode;
        } else if (control == 2) {
            preferences.clock_24h = !preferences.clock_24h;
        } else {
            set_state(ctx, PJ_UI_STATE_SYNC);
            return 1;
        }
        pj_ui_apply_preferences(ctx, &preferences);
        return 1;
        }
    case PJ_UI_STATE_VOLUME:
        {
        pj_ui_preferences_t preferences = current_preferences(ctx);
        preferences.volume += control == 0 ? -1 : 1;
        pj_ui_apply_preferences(ctx, &preferences);
        return 1;
        }
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
        interaction_changed(ctx);
        visual_changed(ctx);
        return 1;
    case PJ_UI_STATE_RECORD:
        return stop_record_and_return(ctx);
    case PJ_UI_STATE_ALARM:
        return activate_control(ctx, 0);
    case PJ_UI_STATE_STOPWATCH:
        if (ctx->time_command.type != PJ_UI_TIME_COMMAND_NONE) {
            return 0;
        }
        queue_time_command(ctx, ctx->stopwatch_running ?
                           PJ_UI_TIME_COMMAND_STOPWATCH_PAUSE :
                           PJ_UI_TIME_COMMAND_STOPWATCH_START, 0, 0);
        return 1;
    case PJ_UI_STATE_TIMER: {
        if (ctx->time_command.type != PJ_UI_TIME_COMMAND_NONE) {
            return 0;
        }
        int seconds = ctx->timer_seconds > 0 ?
            ctx->timer_seconds : ctx->timer_preset_seconds;
        queue_time_command(ctx, ctx->timer_running ?
                           PJ_UI_TIME_COMMAND_TIMER_PAUSE :
                           PJ_UI_TIME_COMMAND_TIMER_START,
                           (uint64_t)seconds * 1000u, 0);
        return 1;
    }
    case PJ_UI_STATE_INTERVAL: {
        if (ctx->time_command.type != PJ_UI_TIME_COMMAND_NONE) {
            return 0;
        }
        int seconds = ctx->interval_seconds > 0 ?
            ctx->interval_seconds : ctx->interval_preset_seconds;
        queue_time_command(ctx, ctx->interval_running ?
                           PJ_UI_TIME_COMMAND_INTERVAL_PAUSE :
                           PJ_UI_TIME_COMMAND_INTERVAL_START,
                           (uint64_t)seconds * 1000u,
                           (uint64_t)ctx->interval_preset_seconds * 1000u);
        return 1;
    }
    case PJ_UI_STATE_SYNC:
        if (ctx->sync_inventory_state == PJ_UI_SYNC_INVENTORY_UNKNOWN ||
            ctx->sync_inventory_state == PJ_UI_SYNC_INVENTORY_OFFLINE ||
            ctx->sync_failed > 0) {
            begin_sync_session(ctx);
            visual_changed(ctx);
            return 1;
        }
        return 0;
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
    ctx->record_state = PJ_RECORD_ARMING;
    ctx->recording_seconds = 0;
    set_state(ctx, PJ_UI_STATE_RECORD);
    return 1;
}

int pj_ui_tick(pj_ui_context_t *ctx)
{
    (void)ctx;
    return 0;
}

int pj_ui_handle_touch(pj_ui_context_t *ctx, int x, int y, pj_touch_kind_t kind)
{
    if (ctx == NULL || x < 0 || y < 0 ||
        x >= PJ_DISPLAY_WIDTH || y >= PJ_DISPLAY_HEIGHT) {
        return 0;
    }
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
        if (tile_hit(PJ_LAYOUT_HOME_3_1, HOME_TILES,
                     sizeof(HOME_TILES) / sizeof(HOME_TILES[0]),
                     x, y, &next)) {
            set_state(ctx, next);
            return 1;
        }
        break;
    }
    case PJ_UI_STATE_NOTES:
        if (tile_hit(PJ_LAYOUT_NOTES_3_1M, NOTES_TILES,
                     sizeof(NOTES_TILES) / sizeof(NOTES_TILES[0]),
                     x, y, &next)) {
            if (next == PJ_UI_STATE_RECORD) {
                if (ctx->record_state != PJ_RECORD_IDLE ||
                    ctx->playback_state != PJ_PLAYBACK_IDLE) {
                    return 0;
                }
                ctx->record_state = PJ_RECORD_ARMING;
                ctx->recording_seconds = 0;
            }
            set_state(ctx, next);
            return 1;
        }
        break;
    case PJ_UI_STATE_TIME:
        if (tile_hit(PJ_LAYOUT_TIME_4_1, TIME_TILES,
                     sizeof(TIME_TILES) / sizeof(TIME_TILES[0]),
                     x, y, &next)) {
            set_state(ctx, next);
            return 1;
        }
        break;
    case PJ_UI_STATE_SETTINGS: {
        pj_layout_slot_id_t slot = pj_layout_hit_test(
            PJ_LAYOUT_SETTINGS_4_0M, (uint16_t)x, (uint16_t)y);
        if (slot == PJ_LAYOUT_SLOT_SETTINGS_VOLUME) {
            return activate_control(ctx, 0);
        }
        if (slot == PJ_LAYOUT_SLOT_SETTINGS_THEME) {
            return activate_control(ctx, 1);
        }
        if (slot == PJ_LAYOUT_SLOT_SETTINGS_HOUR_FORMAT) {
            return activate_control(ctx, 2);
        }
        if (slot == PJ_LAYOUT_SLOT_SETTINGS_SYNC) {
            return activate_control(ctx, 3);
        }
        return 0;
    }
    case PJ_UI_STATE_VOLUME:
        if (y < 100) {
            return 0;
        }
        return activate_control(ctx, x < 100 ? 0 : 1);
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
            interaction_changed(ctx);
            visual_changed(ctx);
            return 1;
        }
        break;
    case PJ_UI_STATE_ALARM:
        if (y >= PJ_UI_ALARM_TOGGLE_TOP) {
            if (y < PJ_UI_ALARM_CONTROLS_TOP) {
                return activate_control(ctx, 0);
            } else {
                int row = min_int(1, (y - PJ_UI_ALARM_CONTROLS_TOP) * 2 /
                                  (PJ_DISPLAY_HEIGHT - PJ_UI_ALARM_CONTROLS_TOP));
                int column = x < PJ_DISPLAY_WIDTH / 2 ? 0 : 1;
                int control = column == 0 ? 1 + row : 3 + row;
                return activate_control(ctx, control);
            }
        }
        break;
    case PJ_UI_STATE_STOPWATCH:
        if (y >= PJ_UI_TIME_CONTROLS_TOP) {
            return activate_control(ctx, x < 100 ? 0 : 1);
        }
        break;
    case PJ_UI_STATE_TIMER:
        if (y >= PJ_UI_TIME_CONTROLS_TOP) {
            int row = y < (PJ_UI_TIME_CONTROLS_TOP + PJ_DISPLAY_HEIGHT) / 2 ? 0 : 1;
            int control = x < PJ_DISPLAY_WIDTH / 2 ? row : 2 + row;
            return activate_control(ctx, control);
        }
        break;
    case PJ_UI_STATE_INTERVAL:
        if (y >= PJ_UI_TIME_CONTROLS_TOP) {
            int row = y < (PJ_UI_TIME_CONTROLS_TOP + PJ_DISPLAY_HEIGHT) / 2 ? 0 : 1;
            int control = x < PJ_DISPLAY_WIDTH / 2 ? row : 2 + row;
            return activate_control(ctx, control);
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
    draw_text_center_at(fb, 100, 43, text, 4);
    (void)snprintf(text, sizeof(text), "%s %02d/%02d", weekday_name(ctx->weekday),
                   ctx->month, ctx->day);
    draw_text_center_at(fb, 100, 100, text, 3);
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
    draw_text_center_at(fb, 100, 140, text, 3);
    pj_carbon_icon_id_t battery_icon =
        ctx->battery_percent <= 10 ? PJ_CARBON_ICON_BATTERY_EMPTY :
        ctx->battery_percent <= 39 ? PJ_CARBON_ICON_BATTERY_LOW :
        ctx->battery_percent <= 79 ? PJ_CARBON_ICON_BATTERY_HALF :
        PJ_CARBON_ICON_BATTERY_FULL;
    (void)snprintf(text, sizeof(text), "%d%%", ctx->battery_percent);
    draw_icon(fb, battery_icon, 65, 180, 28);
    draw_text_center_at(fb, 125, 180, text, 3);
}

static void draw_record(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    char elapsed[16];
    (void)snprintf(elapsed, sizeof(elapsed), "%02d:%02d",
                   ctx->recording_seconds / 60, ctx->recording_seconds % 60);
    draw_text_center_at(fb, 100, 100, elapsed, 4);
}

static void audio_note_display_label(char *out, size_t out_size, const char *label)
{
    static const char *const months[] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
    };
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int sequence = 0;
    if (sscanf(label, "REC %4d%2d%2d %2d%2d %d", &year, &month, &day,
               &hour, &minute, &sequence) == 6 &&
        year >= 2000 && year <= 2099 && month >= 1 && month <= 12 &&
        day >= 1 && day <= 31 && hour >= 0 && hour <= 23 &&
        minute >= 0 && minute <= 59) {
        (void)sequence;
        (void)snprintf(out, out_size, "%s%02d%02d:%02d",
                       months[month - 1], day, hour, minute);
        return;
    }
    while (strncmp(label, "REC ", 4) == 0) {
        label += 4;
    }
    (void)snprintf(out, out_size, "%s", label);
}

static int largest_text_scale_that_fits(const char *text, int max_width, int max_height)
{
    int scale = PJ_UI_FONT_SCALE_COUNT;
    while (scale > 1) {
        if (text_width(text, scale) <= max_width &&
            font_size_for_scale(scale) <= max_height) {
            break;
        }
        scale--;
    }
    return scale;
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
        const char *label = ctx->note_labels[note_index][0] != '\0'
            ? ctx->note_labels[note_index] : "RECORDING";
        char audio_label[PJ_UI_NOTE_LABEL_LEN];
        if (strcmp(kind, "AUD") == 0) {
            audio_note_display_label(audio_label, sizeof(audio_label), label);
            label = audio_label;
        }
        int row_height = next_y - y;
        int scale = largest_text_scale_that_fits(
            label, PJ_DISPLAY_WIDTH - 10,
            row_height - 2 * (PJ_UI_RULE_WIDTH + 2));
        scale = max_int(scale, PJ_UI_NOTE_MIN_TEXT_SCALE);
        draw_text_centered_ellipsized(
            fb, 100, (y + next_y) / 2, label, scale, 190);
    }
    draw_horizontal_rule(fb, PJ_UI_NOTE_PAGER_TOP / 3);
    draw_horizontal_rule(fb, 2 * PJ_UI_NOTE_PAGER_TOP / 3);
    draw_horizontal_rule(fb, PJ_UI_NOTE_PAGER_TOP);
    draw_vertical_rule(fb, PJ_DISPLAY_WIDTH / 2, PJ_UI_NOTE_PAGER_TOP,
                       PJ_DISPLAY_HEIGHT - PJ_UI_NOTE_PAGER_TOP);
    draw_icon(fb, PJ_CARBON_ICON_CHEVRON_LEFT, PJ_DISPLAY_WIDTH / 4,
              (PJ_UI_NOTE_PAGER_TOP + PJ_DISPLAY_HEIGHT) / 2, 40);
    draw_icon(fb, PJ_CARBON_ICON_CHEVRON_RIGHT, 3 * PJ_DISPLAY_WIDTH / 4,
              (PJ_UI_NOTE_PAGER_TOP + PJ_DISPLAY_HEIGHT) / 2, 40);
}

static void draw_settings(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    static const struct {
        pj_layout_slot_id_t slot;
        pj_carbon_icon_id_t icon;
    } icons[] = {
        {PJ_LAYOUT_SLOT_SETTINGS_VOLUME, PJ_CARBON_ICON_VOLUME_UP},
        {PJ_LAYOUT_SLOT_SETTINGS_THEME, PJ_CARBON_ICON_ASLEEP_FILLED},
        {PJ_LAYOUT_SLOT_SETTINGS_SYNC, PJ_CARBON_ICON_FETCH_UPLOAD},
    };
    draw_layout_rules(fb, PJ_LAYOUT_SETTINGS_4_0M);
    for (size_t index = 0; index < sizeof(icons) / sizeof(icons[0]); index++) {
        const pj_layout_slot_t *slot = layout_slot(
            PJ_LAYOUT_SETTINGS_4_0M, icons[index].slot);
        if (slot != NULL) {
            draw_icon(fb, icons[index].icon,
                      layout_coordinate_to_pixel(slot->icon_center.x),
                      layout_coordinate_to_pixel(slot->icon_center.y), 64);
        }
    }
    const pj_layout_slot_t *hour_format = layout_slot(
        PJ_LAYOUT_SETTINGS_4_0M, PJ_LAYOUT_SLOT_SETTINGS_HOUR_FORMAT);
    if (hour_format != NULL) {
        draw_carbon_glyph_center_at(
            fb, layout_coordinate_to_pixel(hour_format->icon_center.x),
            layout_coordinate_to_pixel(hour_format->icon_center.y),
            ctx->clock_24h ? PJ_CARBON_GLYPH_SETTINGS_24H :
                             PJ_CARBON_GLYPH_SETTINGS_12H,
            64);
    }
}

static void draw_volume(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    char value[4];
    (void)snprintf(value, sizeof(value), "%d", ctx->volume);
    draw_text_center_at(fb, PJ_DISPLAY_WIDTH / 2, 50, value, 4);
    draw_horizontal_rule(fb, 100);
    draw_vertical_rule(fb, 100, 100, 100);
    const pj_carbon_icon_id_t actions[] = {
        PJ_CARBON_ICON_VOLUME_DOWN, PJ_CARBON_ICON_VOLUME_UP,
    };
    draw_icon_controls(fb, actions, 2, 100, 2);
}

static void draw_sync(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    const char *status = ctx->sync_inventory_state == PJ_UI_SYNC_INVENTORY_PENDING ?
        "CHECK" : ctx->sync_inventory_state == PJ_UI_SYNC_INVENTORY_UNKNOWN ?
        "WAIT" : "IDLE";
    if (strcmp(ctx->sync_phase, "succeeded") == 0) {
        status = "DONE";
    } else if (strcmp(ctx->sync_phase, "offline") == 0) {
        status = "OFFLINE";
    } else if (strcmp(ctx->sync_phase, "failed") == 0 ||
               strcmp(ctx->sync_phase, "auth_failed") == 0 ||
               strcmp(ctx->sync_phase, "protocol_failed") == 0) {
        status = "ERROR";
    } else if (strcmp(ctx->sync_phase, "discovering") == 0 ||
               strcmp(ctx->sync_phase, "requesting") == 0 ||
               strcmp(ctx->sync_phase, "running") == 0) {
        status = "SYNC";
    } else if (ctx->sync_request_pending ||
               strcmp(ctx->sync_phase, "pending") == 0) {
        status = "QUEUED";
    }
    const char *detail = ctx->sync_online ? "ONLINE" : "WAIT";
    if (strcmp(ctx->sync_phase, "auth_failed") == 0) {
        detail = "AUTH";
    } else if (strcmp(ctx->sync_phase, "protocol_failed") == 0) {
        detail = "PROTO";
    } else if (strcmp(ctx->sync_phase, "failed") == 0) {
        detail = "ERROR";
    } else if (strcmp(ctx->sync_phase, "offline") == 0) {
        detail = "NET";
    }

    char counts[32];
    if (ctx->sync_inventory_state == PJ_UI_SYNC_INVENTORY_READY) {
        (void)snprintf(counts, sizeof(counts), "Q%d S%d E%d",
                       ctx->sync_pending, ctx->sync_transferred,
                       ctx->sync_failed);
    } else {
        (void)snprintf(counts, sizeof(counts), "Q? S%d E%d",
                       ctx->sync_transferred, ctx->sync_failed);
    }
    const char *lines[] = {status, counts, detail};
    int scale = PJ_UI_FONT_SCALE_COUNT;
    int band_height = PJ_DISPLAY_HEIGHT / (int)(sizeof(lines) / sizeof(lines[0]));
    while (scale > 1) {
        int fits = font_size_for_scale(scale) <= band_height - 6;
        for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]) && fits; i++) {
            fits = text_width(lines[i], scale) <= PJ_DISPLAY_WIDTH - 12;
        }
        if (fits) {
            break;
        }
        scale--;
    }
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        int top = (int)i * PJ_DISPLAY_HEIGHT / (int)(sizeof(lines) / sizeof(lines[0]));
        int bottom = (int)(i + 1u) * PJ_DISPLAY_HEIGHT /
            (int)(sizeof(lines) / sizeof(lines[0]));
        draw_text_center_at(fb, PJ_DISPLAY_WIDTH / 2, (top + bottom) / 2,
                            lines[i], scale);
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
    int scale = strlen(text) <= 5u ? 4 : 3;
    draw_text_center_at(fb, 100, cy, text, scale);
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
        draw_icon_menu(fb, PJ_LAYOUT_HOME_3_1, HOME_TILES,
                       sizeof(HOME_TILES) / sizeof(HOME_TILES[0]));
        break;
    }
    case PJ_UI_STATE_NOTES:
        draw_icon_menu(fb, PJ_LAYOUT_NOTES_3_1M, NOTES_TILES,
                       sizeof(NOTES_TILES) / sizeof(NOTES_TILES[0]));
        break;
    case PJ_UI_STATE_TIME:
        draw_icon_menu(fb, PJ_LAYOUT_TIME_4_1, TIME_TILES,
                       sizeof(TIME_TILES) / sizeof(TIME_TILES[0]));
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
            draw_text_center_at(fb, 100, 36, text, 4);
        } else {
            int hour = ctx->alarm_hour % 12;
            if (hour == 0) hour = 12;
            (void)snprintf(text, sizeof(text), "%d:%02d", hour, ctx->alarm_minute);
            draw_text_center_at(fb, 78, 36, text, 4);
            draw_text_center_at(fb, 164, 36,
                                ctx->alarm_hour < 12 ? "AM" : "PM", 2);
        }
        draw_icon(fb, ctx->alarm_on ? PJ_CARBON_ICON_TOGGLE_ON :
                                      PJ_CARBON_ICON_TOGGLE_OFF, 100,
                  (PJ_UI_ALARM_TOGGLE_TOP + PJ_UI_ALARM_CONTROLS_TOP) / 2, 40);
        draw_adjustment_controls(fb, PJ_UI_ALARM_CONTROLS_TOP);
        break;
    }
    case PJ_UI_STATE_STOPWATCH:
        format_duration(text, sizeof(text), ctx->stopwatch_seconds);
        draw_duration_value(fb, 50, text);
        { const pj_carbon_icon_id_t actions[] = {
              ctx->stopwatch_running ? PJ_CARBON_ICON_PAUSE_FILLED :
                                       PJ_CARBON_ICON_PLAY_FILLED,
              PJ_CARBON_ICON_RESET_ALT,
          };
          draw_icon_controls(fb, actions, 2, PJ_UI_TIME_CONTROLS_TOP, 2); }
        break;
    case PJ_UI_STATE_TIMER:
        format_duration(text, sizeof(text),
                        !ctx->timer_running && ctx->timer_seconds == 0
                            ? ctx->timer_preset_seconds : ctx->timer_seconds);
        draw_duration_value(fb, 50, text);
        draw_timer_controls(fb, ctx->timer_running ? PJ_CARBON_ICON_PAUSE_FILLED :
                                                  PJ_CARBON_ICON_PLAY_FILLED);
        break;
    case PJ_UI_STATE_INTERVAL:
        (void)snprintf(text, sizeof(text), "%d", ctx->interval_round);
        {
            int round_scale = 3;
            while (round_scale > 1 &&
                   text_width(text, round_scale) > PJ_DISPLAY_WIDTH - 10) {
                round_scale--;
            }
            draw_text_center_at(fb, 100, 20, text, round_scale);
        }
        format_duration(text, sizeof(text),
                        !ctx->interval_running && ctx->interval_seconds == 0
                            ? ctx->interval_preset_seconds : ctx->interval_seconds);
        draw_duration_value(fb, 76, text);
        draw_timer_controls(fb, ctx->interval_running ? PJ_CARBON_ICON_PAUSE_FILLED :
                                                     PJ_CARBON_ICON_PLAY_FILLED);
        break;
    case PJ_UI_STATE_NOTE_DETAIL:
        if (ctx->note_detail_transcript) {
            int scale = ctx->transcript_font_size;
            int max_lines = 192 / (font_size_for_scale(scale) + 5);
            draw_wrapped_text(fb, 4, 4,
                ctx->note_labels[ctx->selected_note][0] != '\0' ? ctx->note_labels[ctx->selected_note] : "Transcript pending",
                scale, 192, max_lines);
        } else {
            draw_icon(fb, ctx->playback_state == PJ_PLAYBACK_IDLE ?
                           PJ_CARBON_ICON_PLAY_FILLED :
                           PJ_CARBON_ICON_PAUSE_FILLED, 100, 100, 144);
        }
        break;
    case PJ_UI_STATE_SYNC:
        draw_sync(ctx, fb);
        break;
    case PJ_UI_STATE_VOLUME:
        draw_volume(ctx, fb);
        break;
        default:
            draw_centered_text(fb, 90, "UNKNOWN", 2);
            break;
    }

    if (ctx->dark_mode && ctx->state != PJ_UI_STATE_STATIC) {
        invert_framebuffer(fb);
    }
}

void pj_ui_compose_frame(const pj_ui_context_t *ctx, pj_framebuffer_t *fb)
{
    if (ctx == NULL || fb == NULL) {
        return;
    }
    render_scene(ctx, fb);
}
