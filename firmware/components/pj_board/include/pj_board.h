#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PJ_BOARD_REV_UNKNOWN = 0,
    PJ_BOARD_REV_WAVESHARE_154_V1,
    PJ_BOARD_REV_WAVESHARE_154_V2,
} pj_board_revision_t;

typedef struct {
    const char *name;
    const char *sku;
    pj_board_revision_t revision;
    int display_width;
    int display_height;
    int max_wifi_networks;
    int requires_tf_card;
} pj_board_profile_t;

pj_board_profile_t pj_board_default_profile(void);
void pj_board_start_services(const pj_board_profile_t *profile);

#ifdef __cplusplus
}
#endif

