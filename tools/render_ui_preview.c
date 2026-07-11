#include "pj_ui.h"

#include <stdio.h>
#include <stdlib.h>

static void write_pgm(const char *path, const pj_framebuffer_t *fb)
{
    FILE *out = fopen(path, "wb");
    if (out == NULL) {
        perror(path);
        exit(1);
    }
    fprintf(out, "P5\n%d %d\n255\n", PJ_DISPLAY_WIDTH, PJ_DISPLAY_HEIGHT);
    for (int y = 0; y < PJ_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
            fputc(pj_framebuffer_get(fb, x, y) ? 0 : 255, out);
        }
    }
    fclose(out);
}

int main(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t fb;
    pj_ui_init(&ui);
    const char labels[][PJ_UI_NOTE_LABEL_LEN] = {"REC 0941", "REC 1812", "REC 0730", "REC 2105"};
    pj_ui_set_notes(&ui, 4, labels);

    ui.state = PJ_UI_STATE_STATIC;
    pj_ui_render(&ui, &fb);
    write_pgm("/tmp/pj-ui-static.pgm", &fb);

    ui.state = PJ_UI_STATE_TIME_TEMP;
    pj_ui_render(&ui, &fb);
    write_pgm("/tmp/pj-ui-time-temp.pgm", &fb);

    ui.state = PJ_UI_STATE_HOME;
    pj_ui_render(&ui, &fb);
    write_pgm("/tmp/pj-ui-home.pgm", &fb);

    ui.state = PJ_UI_STATE_NOTES;
    pj_ui_render(&ui, &fb);
    write_pgm("/tmp/pj-ui-notes.pgm", &fb);

    ui.state = PJ_UI_STATE_TIME;
    pj_ui_render(&ui, &fb);
    write_pgm("/tmp/pj-ui-time-menu.pgm", &fb);

    ui.state = PJ_UI_STATE_RECORD;
    ui.record_state = PJ_RECORD_ACTIVE;
    pj_ui_render(&ui, &fb);
    write_pgm("/tmp/pj-ui-record.pgm", &fb);

    ui.state = PJ_UI_STATE_LISTEN;
    pj_ui_render(&ui, &fb);
    write_pgm("/tmp/pj-ui-listen.pgm", &fb);

    ui.state = PJ_UI_STATE_ALARM;
    pj_ui_render(&ui, &fb);
    write_pgm("/tmp/pj-ui-alarm.pgm", &fb);

    ui.state = PJ_UI_STATE_STOPWATCH;
    ui.stopwatch_seconds = 3723;
    pj_ui_render(&ui, &fb);
    write_pgm("/tmp/pj-ui-stopwatch.pgm", &fb);

    ui.state = PJ_UI_STATE_SETTINGS;
    pj_ui_render(&ui, &fb);
    write_pgm("/tmp/pj-ui-settings.pgm", &fb);

    return 0;
}
