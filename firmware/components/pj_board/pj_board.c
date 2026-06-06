#include "pj_board.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif

pj_board_profile_t pj_board_default_profile(void)
{
    pj_board_profile_t profile = {
        .name = "waveshare-esp32-s3-touch-epaper-1.54-v2",
        .sku = "34211",
        .revision = PJ_BOARD_REV_WAVESHARE_154_V2,
        .display_width = 200,
        .display_height = 200,
        .max_wifi_networks = 5,
        .requires_tf_card = 1,
    };
    return profile;
}

void pj_board_start_services(const pj_board_profile_t *profile)
{
    (void)profile;
#ifdef ESP_PLATFORM
    static const char *TAG = "pj-board";
    ESP_LOGW(TAG, "Service start is a hardware bring-up boundary");
    ESP_LOGW(TAG, "TODO: enable TF card, display/touch, audio, BLE provisioning, Wi-Fi, mDNS, HTTP API");
#endif
}

