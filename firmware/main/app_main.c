#include "esp_log.h"
#include "pj_board.h"
#include "pj_ui.h"

static const char *TAG = "pocket-journal";

void app_main(void)
{
    pj_board_profile_t profile = pj_board_default_profile();
    ESP_LOGI(TAG, "Pocket Journal booting on board profile %s", profile.name);
    ESP_LOGI(TAG, "Hardware drivers are staged until board V1/V2 revision is verified");

    pj_ui_context_t ui;
    pj_framebuffer_t fb;
    pj_ui_init(&ui);
    pj_ui_render(&ui, &fb);

    ESP_LOGI(TAG, "Initial UI state: %s, framebuffer bytes: %u",
             pj_ui_state_name(pj_ui_current_state(&ui)),
             (unsigned)PJ_FRAMEBUFFER_BYTES);

    pj_board_start_services(&profile);
}

