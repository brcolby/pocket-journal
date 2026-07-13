#pragma once

#include "pj_ui.h"
#include "pj_storage.h"

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
    int flash_mb;
    int psram_mb;
} pj_board_profile_t;

typedef enum {
    PJ_BOARD_SERVICE_DISABLED = 0,
    PJ_BOARD_SERVICE_READY,
    PJ_BOARD_SERVICE_UNAVAILABLE,
    PJ_BOARD_SERVICE_ERROR
} pj_board_service_state_t;

typedef enum {
    PJ_BOARD_EVENT_NONE = 0,
    PJ_BOARD_EVENT_WAKE,
    PJ_BOARD_EVENT_SLEEP,
    PJ_BOARD_EVENT_TOUCH_TAP,
    PJ_BOARD_EVENT_AUX_SHORT,
    PJ_BOARD_EVENT_AUX_LONG,
    PJ_BOARD_EVENT_AUX_DOUBLE
} pj_board_event_type_t;

typedef struct {
    pj_board_event_type_t type;
    int x;
    int y;
} pj_board_event_t;

typedef struct {
    pj_board_service_state_t display;
    pj_board_service_state_t storage;
    pj_board_service_state_t audio;
    pj_board_service_state_t ble_provisioning;
    pj_board_service_state_t wifi;
    pj_board_service_state_t http;
    int battery_percent;
    int temperature_c;
    int humidity_percent;
    int storage_mounted;
    uint64_t storage_total_bytes;
    uint64_t storage_free_bytes;
    pj_storage_health_t storage_health;
    unsigned storage_recovery_count;
    int recording;
    int playback_active;
    int hour;
    int minute;
    int year;
    int month;
    int day;
    int time_set;
    char device_id[32];
    char token[64];
    char ip_addr[48];
    char storage_path[32];
    char last_error[128];
} pj_board_status_t;

pj_board_profile_t pj_board_default_profile(void);
void pj_board_init(const pj_board_profile_t *profile);
void pj_board_start_services(const pj_board_profile_t *profile);
pj_board_status_t pj_board_status(void);
void pj_board_refresh_status(pj_ui_context_t *ui);
void pj_board_refresh_settings(pj_ui_context_t *ui);
int pj_board_store_settings_from_ui(const pj_ui_context_t *ui);
int pj_board_consume_settings_update(pj_ui_context_t *ui);
void pj_board_refresh_static_art(pj_ui_context_t *ui);
int pj_board_consume_static_art_update(pj_ui_context_t *ui);
void pj_board_refresh_home_layout(pj_ui_context_t *ui);
int pj_board_consume_home_layout_update(pj_ui_context_t *ui);
void pj_board_refresh_notes(pj_ui_context_t *ui);
int pj_board_consume_audio_update(pj_ui_context_t *ui);
int pj_board_consume_notes_update(pj_ui_context_t *ui);
int pj_board_tick_time(pj_ui_context_t *ui);
int pj_board_consume_time_update(pj_ui_context_t *ui);
void pj_board_refresh_time_state(pj_ui_context_t *ui);
int pj_board_apply_time_actions(pj_ui_context_t *ui);
int pj_board_update_time_state(pj_ui_context_t *ui);
int pj_board_display_framebuffer(const pj_framebuffer_t *fb, const pj_ui_dirty_region_t *dirty);
int pj_board_poll_event(pj_board_event_t *event);
void pj_board_enter_sleep(void);
int pj_board_record_set_active(int active);
int pj_board_record_toggle(void);
int pj_board_playback_set_active(int active, int note_index);
int pj_board_playback_toggle(void);
int pj_board_playback_toggle_index(int note_index);
int pj_board_wipe_recordings(pj_ui_context_t *ui);
int pj_board_storage_recover(void);
int pj_board_http_start(void);

#ifdef __cplusplus
}
#endif
