#include "pj_ui.h"

#include <assert.h>
#include <stdio.h>

static int count_black_pixels(const pj_framebuffer_t *fb)
{
    int count = 0;
    for (int y = 0; y < PJ_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
            count += pj_framebuffer_get(fb, x, y);
        }
    }
    return count;
}

static void test_state_graph(void)
{
    pj_ui_context_t ui;
    pj_ui_init(&ui);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_STATIC);

    assert(pj_ui_handle_touch(&ui, 100, 100, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIME_TEMP);

    assert(pj_ui_handle_touch(&ui, 100, 150, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);

    assert(pj_ui_handle_touch(&ui, 100, 45, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_NOTES);

    assert(pj_ui_handle_touch(&ui, 100, 102, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_LISTEN);

    assert(pj_ui_handle_touch(&ui, 8, 8, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_NOTES);

    assert(pj_ui_handle_touch(&ui, 8, 8, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_HOME);

    assert(pj_ui_handle_touch(&ui, 8, 8, PJ_TOUCH_TAP) == 1);
    assert(pj_ui_current_state(&ui) == PJ_UI_STATE_TIME_TEMP);
}

static void test_render_all_states(void)
{
    pj_ui_context_t ui;
    pj_framebuffer_t fb;
    pj_ui_init(&ui);

    for (int state = 0; state < PJ_UI_STATE_COUNT; state++) {
        ui.state = (pj_ui_state_t)state;
        pj_ui_render(&ui, &fb);
        assert(count_black_pixels(&fb) > 0);
        assert(pj_ui_state_name((pj_ui_state_t)state) != 0);
    }
}

int main(void)
{
    test_state_graph();
    test_render_all_states();
    puts("ui tests passed");
    return 0;
}
