#include "pj_board.h"
#include "pj_alert_audio.h"
#include "pj_audio_level.h"
#include "pj_aux_input.h"
#include "pj_auth.h"
#include "pj_home_layout.h"
#include "pj_note_model.h"
#include "pj_recording.h"
#include "pj_rtc_wake.h"
#include "pj_settings.h"
#include "pj_static_art.h"
#include "pj_storage.h"
#include "pj_time_clock.h"
#include "pj_time_controller.h"
#include "pj_time_sync.h"
#include "pj_transcript_upload.h"
#include "pj_wifi_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#ifdef ESP_PLATFORM
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#endif

#ifdef ESP_PLATFORM
#include "esp_check.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sleep.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/rtc_io.h"
#include "driver/sdmmc_host.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "psa/crypto.h"
#include "sdmmc_cmd.h"
#include "lwip/ip4_addr.h"
#include "mdns.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_sm.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#endif

#define PJ_DEFAULT_TOKEN "dev-token"

#ifdef ESP_PLATFORM
#define PJ_NVS_NAMESPACE "pj"
#define PJ_NVS_WIFI_SSID "wifi_ssid"
#define PJ_NVS_WIFI_PASSWORD "wifi_pass"
#define PJ_NVS_TOKEN "token"
#define PJ_NVS_VOLUME "volume"
#define PJ_NVS_DARK_MODE "dark_mode"
#define PJ_NVS_ALARM_ENABLED "alarm_on"
#define PJ_NVS_ALARM_HOUR "alarm_hr"
#define PJ_NVS_ALARM_MINUTE "alarm_min"
#define PJ_NVS_TIMER_SECONDS "timer_sec"
#define PJ_NVS_INTERVAL_SECONDS "intvl_sec"
#define PJ_NVS_CLOCK_24H "clock_24h"
#define PJ_NVS_TEMP_F "temp_f"
#define PJ_NVS_TEXT_SIZE "text_size"
#define PJ_NVS_SETTINGS_VERSION "set_ver"
#define PJ_NVS_SETTINGS_SLOT_0 "settings_0"
#define PJ_NVS_SETTINGS_SLOT_1 "settings_1"
#define PJ_SETTINGS_VERSION 2
#define PJ_NVS_STATIC_ART_SLOT "art_slot"
#define PJ_NVS_HOME_LAYOUT "home_layout"
#define PJ_NVS_TIME_STATE "time_state"
#define PJ_NVS_WAKE_PLAN "wake_plan"
#define PJ_STATIC_ART_SLOT_COUNT 2
#define PJ_WIFI_SSID_MAX_LEN 32
#define PJ_WIFI_PASSWORD_MAX_LEN 64
#define EPD_SPI_NUM SPI2_HOST
#define PJ_EPD_SPI_CLOCK_HZ (20 * 1000 * 1000)
#define ESP32_I2C_DEV_NUM I2C_NUM_0
#define EPD_DC_PIN GPIO_NUM_10
#define EPD_CS_PIN GPIO_NUM_11
#define EPD_SCK_PIN GPIO_NUM_12
#define EPD_MOSI_PIN GPIO_NUM_13
#define EPD_RST_PIN GPIO_NUM_9
#define EPD_BUSY_PIN GPIO_NUM_8
#define EPD_TP_RST_PIN GPIO_NUM_7
#define EPD_TP_INT_PIN GPIO_NUM_21
#define EPD_PWR_PIN GPIO_NUM_6
#define AUDIO_PWR_PIN GPIO_NUM_42
#define AUDIO_PA_PIN GPIO_NUM_46
#define VBAT_PWR_PIN GPIO_NUM_17
#define VBAT_ADC_CHANNEL ADC_CHANNEL_3
#define BOOT_BUTTON_PIN GPIO_NUM_0
#define RTC_INT_PIN GPIO_NUM_5
#define PWR_BUTTON_PIN GPIO_NUM_18
#define LED_PIN GPIO_NUM_3
#define ESP32_I2C_SDA_PIN GPIO_NUM_47
#define ESP32_I2C_SCL_PIN GPIO_NUM_48
#define I2S_MCLK_PIN GPIO_NUM_14
#define I2S_BCLK_PIN GPIO_NUM_15
#define I2S_WS_PIN GPIO_NUM_38
#define I2S_DIN_PIN GPIO_NUM_16
#define I2S_DOUT_PIN GPIO_NUM_45
#define SD_MISO_D0_PIN GPIO_NUM_40
#define SD_MOSI_CMD_PIN GPIO_NUM_41
#define SD_CLK_PIN GPIO_NUM_39
#define I2C_SHTC3_DEV_ADDRESS 0x70
#define I2C_FT6336_DEV_ADDRESS 0x38
#define I2C_PCF85063_DEV_ADDRESS 0x51
#define I2C_ES8311_DEV_ADDRESS 0x18
#define SHTC3_CMD_WAKEUP 0x3517
#define SHTC3_CMD_SLEEP 0xB098
#define SHTC3_CMD_SOFT_RESET 0x805D
#define SHTC3_CMD_MEAS_T_RH_POLLING 0x7866
#define PJ_ES8311_CLK_REG01 0x01
#define PJ_ES8311_CLK_REG02 0x02
#define PJ_ES8311_SDPIN_REG09 0x09
#define PJ_ES8311_SYSTEM_REG0D 0x0D
#define PJ_ES8311_SYSTEM_REG0E 0x0E
#define PJ_ES8311_SYSTEM_REG12 0x12
#define PJ_ES8311_SYSTEM_REG14 0x14
#define PJ_ES8311_DAC_REG31 0x31
#define PJ_ES8311_DAC_REG32 0x32
#define PJ_ES8311_DAC_REG37 0x37
#define PJ_ES8311_GPIO_REG44 0x44
#define PJ_ES8311_GP_REG45 0x45
#define PJ_AUDIO_SAMPLE_RATE 16000
#define PJ_AUDIO_CHANNELS 1
#define PJ_AUDIO_BITS_PER_SAMPLE 16
#define PJ_AUDIO_FRAME_BYTES 4
#define PJ_AUDIO_MCLK_MULTIPLE I2S_MCLK_MULTIPLE_384
#define PJ_AUDIO_MIC_GAIN_DB 42.0f
#define PJ_AUDIO_RECORD_TASK_STACK 6144
#define PJ_AUDIO_PROCESS_TASK_STACK 6144
#define PJ_AUDIO_PLAYBACK_TASK_STACK 6144
#define PJ_ALERT_AUDIO_TASK_STACK 4096
#define PJ_AUDIO_IO_BUFFER_BYTES 1024
#define PJ_AUDIO_MAX_CONSECUTIVE_READ_ERRORS 10
#define AUDIO_PA_ACTIVE_LEVEL 1
#define AUDIO_PA_IDLE_LEVEL (1 - AUDIO_PA_ACTIVE_LEVEL)
#define PJ_AUDIO_DIAG_TONE_MS 1500
#define PJ_AUDIO_DIAG_TONE_HZ 880
#define PJ_AUDIO_DIAG_TONE_AMPLITUDE 18000
#define PJ_AUDIO_OUTPUT_SILENCE_MS 24
#define PJ_AUDIO_MIC_CHECK_MS 1500
#define PJ_AUDIO_MIC_CHECK_MAX_MS 10000
#define PJ_AUDIO_TONE_DEFAULT_INT -1
#define PJ_TOUCH_EVENT_GUARD_MS 150
#define PJ_TOUCH_POLL_MS 10
#define PJ_TOUCH_EVENT_QUEUE_DEPTH 8
#define PJ_TOUCH_STABLE_SAMPLES 2
#define PJ_TOUCH_MOVE_TOLERANCE 18
#define PJ_HTTP_MAX_URI_HANDLERS 16
#define PJ_AUDIO_DIR "/sdcard/pj/audio"
#define PJ_TRANSCRIPT_DIR "/sdcard/pj/transcripts"
#define PJ_NOTE_DIR "/sdcard/pj/notes"
#define PJ_AUDIO_MAX_INDEXED_FILES 16
#define PJ_SERIAL_COMMAND_TASK_STACK 4096
#define PJ_CONNECTIVITY_TASK_STACK 4096
#define PJ_CONNECTIVITY_POLL_MS 250
#define PJ_WIFI_RECONNECT_SETTLE_MS 250
#define PJ_STORAGE_RESERVE_BYTES (256ULL * 1024ULL)
#define PJ_STORAGE_RECORD_START_BYTES (64ULL * 1024ULL)
#define PJ_STORAGE_CAPACITY_CHECK_BYTES (64U * 1024U)
#endif

static pj_board_status_t g_status;
static pj_settings_t g_settings;
#ifdef ESP_PLATFORM
static pj_settings_store_t g_settings_store;
static int g_settings_store_ready;
static esp_err_t g_settings_io_error = ESP_OK;
#endif
static pj_static_art_t g_static_art;
static pj_home_layout_t g_home_layout;
static int g_static_art_valid;
static int g_static_art_slot = -1;
static int g_display_warning_logged;
static volatile int g_time_update_pending;
static uint32_t g_time_generation;
static volatile int g_settings_update_pending;
static volatile int g_static_art_update_pending;
static volatile int g_home_layout_update_pending;

typedef struct {
    int hour;
    int minute;
    int year;
    int month;
    int day;
    int time_set;
    uint32_t generation;
} board_time_snapshot_t;

static int valid_time_date(int hour, int minute, int year, int month, int day)
{
    return year >= 2024 && year <= 2099 &&
           pj_time_clock_civil_valid(year, month, day, hour, minute, 0);
}

#ifdef ESP_PLATFORM
static httpd_handle_t g_http_server;
static const char *TAG = "pj-board";
static spi_device_handle_t g_epd_spi;
static i2c_master_bus_handle_t g_i2c_bus;
static i2c_master_dev_handle_t g_touch_dev;
static i2c_master_dev_handle_t g_shtc3_dev;
static i2c_master_dev_handle_t g_rtc_dev;
static i2s_chan_handle_t g_i2s_tx_chan;
static i2s_chan_handle_t g_i2s_rx_chan;
static esp_codec_dev_handle_t g_audio_codec;
static esp_codec_dev_handle_t g_audio_playback_codec;
static esp_codec_dev_handle_t g_audio_record_codec;
static const audio_codec_if_t *g_es8311_codec_if;
static gpio_num_t g_i2s_dout_pin = I2S_DOUT_PIN;
static adc_oneshot_unit_handle_t g_adc1_handle;
static adc_cali_handle_t g_adc_cali_handle;
static QueueHandle_t g_board_event_queue;
static SemaphoreHandle_t g_i2c_lock;
static SemaphoreHandle_t g_rtc_sequence_lock;
static SemaphoreHandle_t g_audio_lock;
static SemaphoreHandle_t g_settings_lock;
static SemaphoreHandle_t g_static_art_lock;
static SemaphoreHandle_t g_home_layout_lock;
static sdmmc_card_t *g_sd_card;
static uint8_t g_epd_buffer[PJ_FRAMEBUFFER_BYTES];
static pj_framebuffer_t g_epd_shadow_fb;
static int g_display_ready;
static int g_epd_shadow_valid;
static int g_epd_partial_ready;
static int g_i2c_ready;
static int g_touch_ready;
static int g_rtc_ready;
static int g_audio_ready;
static int g_adc_ready;
static int g_adc_cali_ready;
static int g_network_stack_ready;
static int g_wifi_started;
static esp_netif_t *g_wifi_sta_netif;
static int g_connectivity_task_started;
static int g_connectivity_state_initialized;
static int g_sntp_initialized;
static int g_wifi_control_disconnect_pending;
static portMUX_TYPE g_connectivity_lock = portMUX_INITIALIZER_UNLOCKED;
static pj_wifi_state_t g_wifi_state;
static pj_time_sync_state_t g_time_sync_state;
static int g_mdns_started;
static int g_ble_started;
static uint8_t g_ble_addr_type;
static TaskHandle_t g_ble_provision_task;
static char g_ble_ssid[PJ_WIFI_SSID_MAX_LEN + 1];
static char g_ble_password[PJ_WIFI_PASSWORD_MAX_LEN + 1];
static char g_ble_token[sizeof(g_status.token)];
static char g_ble_state[24] = "idle";
static pj_aux_input_t g_aux_input;
static int g_touch_task_started;
static int g_serial_command_task_started;
static int g_touch_pressed;
static uint16_t g_touch_press_x;
static uint16_t g_touch_press_y;
static TickType_t g_touch_last_event_tick;
static int g_touch_candidate_samples;
static uint16_t g_touch_candidate_x;
static uint16_t g_touch_candidate_y;
static uint8_t g_touch_raw_event;
static volatile int g_record_stop_requested;
static volatile int g_playback_stop_requested;
static volatile int g_audio_state_update_pending;
static volatile int g_notes_update_pending;
static TaskHandle_t g_record_task;
static TaskHandle_t g_audio_process_task;
static TaskHandle_t g_playback_task;
static TaskHandle_t g_alert_audio_task;
static int g_record_audio_owned;
static int g_alert_audio_output_owned;
static char g_active_recording_path[128];
static char g_active_playback_path[128];
static int g_ui_note_audio_indices[PJ_UI_MAX_NOTES];
static int g_ui_note_audio_count;
static int g_ui_note_transcript_view;
static char g_wifi_ssid[PJ_WIFI_SSID_MAX_LEN + 1];
static char g_wifi_password[PJ_WIFI_PASSWORD_MAX_LEN + 1];
static int g_wifi_credentials_stored;
static uint32_t g_record_sequence;
static portMUX_TYPE g_time_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE g_audio_state_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE g_recording_lock = portMUX_INITIALIZER_UNLOCKED;
static pj_recording_t g_recording;
static pj_time_clock_anchor_t g_time_clock_anchor;
static pj_time_controller_t g_time_controller;
static pj_time_controller_diagnostic_t g_time_last_diagnostic;
static int g_time_wall_trusted;
static pj_rtc_wake_plan_t g_rtc_wake_plan;
static int g_rtc_ext1_enabled;
static int g_rtc_wake_hardware_verified;
static int g_timer_wakeup_enabled;
static int g_rtc_wake_restored;
static int g_rtc_wake_metadata_blocked;

typedef struct {
    uint64_t alert_id;
    pj_alert_audio_kind_t kind;
    uint8_t volume;
    uint8_t recording;
    uint8_t deferred;
} alert_audio_intent_t;

static portMUX_TYPE g_alert_audio_intent_lock = portMUX_INITIALIZER_UNLOCKED;
static alert_audio_intent_t g_alert_audio_intent;

static esp_err_t rtc_write_status_time(void);
static uint64_t board_monotonic_ms(void);
static esp_err_t connectivity_runtime_start(void);
static void connectivity_state_init(void);
static void connectivity_state_snapshot(pj_wifi_state_t *wifi,
                                        pj_time_sync_state_t *time_sync);
static int board_time_model_clock(pj_time_clock_t *clock);
static int time_state_initialize(void);
static int time_state_project(pj_ui_context_t *ui);
static int rtc_wake_sync(void);
static pj_rtc_wake_result_t rtc_wake_disarm_board(uint8_t *flags, int force);
static int settings_codec_volume_snapshot(void);
static void alert_audio_project(const pj_time_alert_t *alert,
                                pj_time_conflict_action_t action);
static void alert_audio_set_recording(int recording);
static void alert_audio_set_volume(int codec_volume);

static pj_alert_audio_t g_alert_audio;

static void board_audio_state_set(int recording, int playback_active)
{
    portENTER_CRITICAL(&g_audio_state_lock);
    if (recording >= 0) {
        g_status.recording = recording;
    }
    if (playback_active >= 0) {
        g_status.playback_active = playback_active;
    }
    portEXIT_CRITICAL(&g_audio_state_lock);
}

static int recording_state_start(void)
{
    int started;
    portENTER_CRITICAL(&g_recording_lock);
    started = pj_recording_start(&g_recording, PJ_AUDIO_SAMPLE_RATE,
                                 PJ_AUDIO_CHANNELS, PJ_AUDIO_BITS_PER_SAMPLE);
    g_status.recording_elapsed_ms = pj_recording_elapsed_ms(&g_recording);
    portEXIT_CRITICAL(&g_recording_lock);
    return started;
}

static int recording_state_commit(size_t bytes)
{
    int committed;
    portENTER_CRITICAL(&g_recording_lock);
    committed = pj_recording_commit(&g_recording, bytes);
    g_status.recording_elapsed_ms = pj_recording_elapsed_ms(&g_recording);
    portEXIT_CRITICAL(&g_recording_lock);
    return committed;
}

static void recording_state_request_stop(void)
{
    portENTER_CRITICAL(&g_recording_lock);
    (void)pj_recording_request_stop(&g_recording);
    portEXIT_CRITICAL(&g_recording_lock);
}

static void recording_state_finish_capture(int succeeded)
{
    portENTER_CRITICAL(&g_recording_lock);
    (void)pj_recording_finish_capture(&g_recording, succeeded);
    portEXIT_CRITICAL(&g_recording_lock);
}

static void recording_state_finish_processing(int succeeded)
{
    portENTER_CRITICAL(&g_recording_lock);
    (void)pj_recording_finish_processing(&g_recording, succeeded);
    portEXIT_CRITICAL(&g_recording_lock);
}

static void recording_publish_completion(void)
{
    int succeeded = 0;
    int completed;
    portENTER_CRITICAL(&g_recording_lock);
    completed = pj_recording_take_completion(&g_recording, &succeeded);
    portEXIT_CRITICAL(&g_recording_lock);
    if (!completed) {
        return;
    }
    if (succeeded) {
        g_notes_update_pending = 1;
    } else if (g_status.last_error[0] == '\0') {
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "recording failed before a valid note was published");
    }
}

static pj_time_activity_t board_time_activity(void)
{
    int recording;
    int playback_active;
    portENTER_CRITICAL(&g_audio_state_lock);
    recording = g_status.recording;
    playback_active = g_status.playback_active;
    portEXIT_CRITICAL(&g_audio_state_lock);
    return recording ? PJ_TIME_ACTIVITY_RECORDING :
           playback_active ? PJ_TIME_ACTIVITY_PLAYBACK : PJ_TIME_ACTIVITY_IDLE;
}
typedef struct {
    int pa_level;
    int dout_gpio;
    int audio_power_level;
    int codec_gpio44;
    int codec_gp45;
} audio_tone_diag_opts_t;
typedef struct {
    uint32_t duration_ms;
    int gain_db;
    uint32_t bytes_read;
    uint32_t frames;
    uint32_t peak;
    uint32_t avg_abs;
    uint32_t clipped;
    uint32_t near_zero;
    uint32_t read_errors;
    int input_channel;
} audio_mic_check_result_t;
typedef struct {
    char temporary_path[144];
    char final_path[128];
    uint32_t data_bytes;
    uint32_t raw_peak;
    uint32_t raw_avg;
    uint32_t raw_clipped;
    int input_channel;
} audio_process_args_t;

static esp_err_t audio_play_tone_ms(uint32_t duration_ms, const audio_tone_diag_opts_t *opts);
static esp_err_t audio_mic_check_ms(uint32_t duration_ms, int gain_db, audio_mic_check_result_t *result);
static esp_err_t serial_command_task_start(void);
static int cleanup_storage_artifacts(void);
static int storage_refresh_capacity(void);
static int storage_preflight(uint64_t write_bytes, const char *operation);

static const uint8_t WF_FULL_1IN54[159] = {
    0x80,0x48,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x40,0x48,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x80,0x48,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x40,0x48,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xA,
    0x0,0x0,0x0,0x0,0x0,0x0,0x8,0x1,0x0,0x8,0x1,0x0,0x2,
    0xA,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
    0x0,0x0,0x0,0x0,0x0,0x22,0x22,0x22,0x22,0x22,0x22,0x0,
    0x0,0x0,0x22,0x17,0x41,0x0,0x32,0x20
};

static const uint8_t WF_PARTIAL_1IN54[159] = {
    0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x80,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x40,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x0F,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x01,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x22,0x22,0x22,0x22,0x22,0x22,0x00,0x00,0x00,
    0x02,0x17,0x41,0xB0,0x32,0x28
};
#endif

static const char *service_name(pj_board_service_state_t state)
{
    switch (state) {
    case PJ_BOARD_SERVICE_READY:
        return "ready";
    case PJ_BOARD_SERVICE_UNAVAILABLE:
        return "unavailable";
    case PJ_BOARD_SERVICE_ERROR:
        return "error";
    case PJ_BOARD_SERVICE_DISABLED:
    default:
        return "disabled";
    }
}

#ifdef ESP_PLATFORM
static void audio_pa_set(int enabled)
{
    gpio_set_level(AUDIO_PA_PIN, enabled ? AUDIO_PA_ACTIVE_LEVEL : AUDIO_PA_IDLE_LEVEL);
}

static uint32_t audio_abs_sample(int16_t sample)
{
    int32_t value = sample;
    return value < 0 ? (uint32_t)(-value) : (uint32_t)value;
}

static void audio_update_stats(int16_t sample, uint32_t *peak, uint64_t *sum, uint32_t *count)
{
    uint32_t abs_value = audio_abs_sample(sample);
    if (abs_value > *peak) {
        *peak = abs_value;
    }
    *sum += abs_value;
    (*count)++;
}

static int audio_codec_reg_get(int reg)
{
    int value = 0xFF;
    if (g_audio_playback_codec == NULL) {
        return value;
    }
    if (esp_codec_dev_read_reg(g_audio_playback_codec, reg, &value) != ESP_CODEC_DEV_OK) {
        return 0xFF;
    }
    return value & 0xFF;
}

static esp_err_t audio_codec_reg_set(int reg, int value)
{
    if (g_audio_playback_codec == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int ret = esp_codec_dev_write_reg(g_audio_playback_codec, reg, value & 0xFF);
    return ret == ESP_CODEC_DEV_OK ? ESP_OK : (esp_err_t)ret;
}

static void audio_log_output_regs(const char *reason)
{
    ESP_LOGI(TAG,
             "ES8311 output regs %s: r01=%02x r02=%02x r09=%02x r0d=%02x r0e=%02x "
             "r12=%02x r14=%02x r31=%02x r32=%02x r37=%02x r44=%02x r45=%02x pa=%d pwr=%d dout=%d",
             reason,
             audio_codec_reg_get(PJ_ES8311_CLK_REG01),
             audio_codec_reg_get(PJ_ES8311_CLK_REG02),
             audio_codec_reg_get(PJ_ES8311_SDPIN_REG09),
             audio_codec_reg_get(PJ_ES8311_SYSTEM_REG0D),
             audio_codec_reg_get(PJ_ES8311_SYSTEM_REG0E),
             audio_codec_reg_get(PJ_ES8311_SYSTEM_REG12),
             audio_codec_reg_get(PJ_ES8311_SYSTEM_REG14),
             audio_codec_reg_get(PJ_ES8311_DAC_REG31),
             audio_codec_reg_get(PJ_ES8311_DAC_REG32),
             audio_codec_reg_get(PJ_ES8311_DAC_REG37),
             audio_codec_reg_get(PJ_ES8311_GPIO_REG44),
             audio_codec_reg_get(PJ_ES8311_GP_REG45),
             gpio_get_level(AUDIO_PA_PIN),
             gpio_get_level(AUDIO_PWR_PIN),
             (int)g_i2s_dout_pin);
}

static esp_err_t audio_write_silence_ms(uint32_t duration_ms)
{
    int16_t silence[128 * (PJ_AUDIO_FRAME_BYTES / sizeof(int16_t))] = {0};
    uint32_t frames_remaining = (PJ_AUDIO_SAMPLE_RATE * duration_ms) / 1000U;
    while (frames_remaining > 0) {
        uint32_t frames = frames_remaining < 128U ? frames_remaining : 128U;
        int write_ret = esp_codec_dev_write(g_audio_playback_codec, silence,
                                            (int)(frames * PJ_AUDIO_FRAME_BYTES));
        if (write_ret != ESP_CODEC_DEV_OK) {
            return (esp_err_t)write_ret;
        }
        frames_remaining -= frames;
    }
    return ESP_OK;
}

static void audio_abort_output(const char *reason)
{
    if (g_audio_playback_codec != NULL) {
        (void)esp_codec_dev_set_out_mute(g_audio_playback_codec, true);
    }
    audio_pa_set(0);
    audio_log_output_regs(reason);
}

static esp_err_t audio_prepare_output(const char *reason, int pa_level_override,
                                      int codec_volume)
{
    if (g_audio_playback_codec == NULL || g_es8311_codec_if == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_es8311_codec_if->enable != NULL) {
        int enable_ret = g_es8311_codec_if->enable(g_es8311_codec_if, true);
        if (enable_ret != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "ES8311 enable failed before %s: %s", reason, esp_err_to_name((esp_err_t)enable_ret));
        }
    }

    int vol_ret = esp_codec_dev_set_out_vol(g_audio_playback_codec, codec_volume);
    if (vol_ret != ESP_CODEC_DEV_OK) {
        audio_abort_output("output-volume-failed");
        return (esp_err_t)vol_ret;
    }
    int mute_ret = esp_codec_dev_set_out_mute(g_audio_playback_codec, true);
    if (mute_ret != ESP_CODEC_DEV_OK) {
        audio_abort_output("output-mute-failed");
        return (esp_err_t)mute_ret;
    }

    if (pa_level_override == 0 || pa_level_override == 1) {
        gpio_set_level(AUDIO_PA_PIN, pa_level_override);
    } else {
        audio_pa_set(1);
    }

    esp_err_t silence_err = audio_write_silence_ms(PJ_AUDIO_OUTPUT_SILENCE_MS);
    if (silence_err != ESP_OK) {
        audio_abort_output("output-preroll-failed");
        return silence_err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    mute_ret = esp_codec_dev_set_out_mute(g_audio_playback_codec, false);
    if (mute_ret != ESP_CODEC_DEV_OK) {
        audio_abort_output("output-unmute-failed");
        return (esp_err_t)mute_ret;
    }
    silence_err = audio_write_silence_ms(PJ_AUDIO_OUTPUT_SILENCE_MS);
    if (silence_err != ESP_OK) {
        audio_abort_output("output-post-unmute-failed");
        return silence_err;
    }

    audio_log_output_regs(reason);
    ESP_LOGI(TAG, "Playback output enabled: pa_level=%d volume=%d",
             gpio_get_level(AUDIO_PA_PIN), codec_volume);
    return ESP_OK;
}

static esp_err_t audio_finish_output(const char *reason)
{
    esp_err_t result = ESP_OK;
    if (g_audio_playback_codec != NULL) {
        result = audio_write_silence_ms(PJ_AUDIO_OUTPUT_SILENCE_MS);
        vTaskDelay(pdMS_TO_TICKS(PJ_AUDIO_OUTPUT_SILENCE_MS));
        int mute = esp_codec_dev_set_out_mute(g_audio_playback_codec, true);
        if (result == ESP_OK && mute != ESP_CODEC_DEV_OK) {
            result = (esp_err_t)mute;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    audio_pa_set(0);
    audio_log_output_regs(reason);
    return result;
}

static int alert_audio_prepare(void *context, uint32_t sample_rate, uint8_t channels,
                               uint8_t codec_volume)
{
    (void)context;
    if (sample_rate != PJ_AUDIO_SAMPLE_RATE || channels != 2 ||
        g_audio_lock == NULL || board_time_activity() != PJ_TIME_ACTIVITY_IDLE ||
        xSemaphoreTake(g_audio_lock, pdMS_TO_TICKS(20)) != pdTRUE) {
        return 0;
    }
    if (board_time_activity() != PJ_TIME_ACTIVITY_IDLE) {
        xSemaphoreGive(g_audio_lock);
        return 0;
    }
    g_alert_audio_output_owned = 1;
    esp_err_t err = audio_prepare_output("time-alert", -1, codec_volume);
    if (err != ESP_OK) {
        audio_abort_output("time-alert-prepare-failed");
        g_alert_audio_output_owned = 0;
        xSemaphoreGive(g_audio_lock);
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "time alert audio prepare failed: %s", esp_err_to_name(err));
        ESP_LOGW(TAG, "%s", g_status.last_error);
        return 0;
    }
    return 1;
}

static int alert_audio_write(void *context, const int16_t *samples, size_t frames)
{
    (void)context;
    if (!g_alert_audio_output_owned || samples == NULL || frames == 0 ||
        frames > PJ_ALERT_AUDIO_BLOCK_FRAMES) {
        return 0;
    }
    int result = esp_codec_dev_write(g_audio_playback_codec, (void *)samples,
                                     (int)(frames * PJ_AUDIO_FRAME_BYTES));
    if (result != ESP_CODEC_DEV_OK) {
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "time alert audio write failed: %s", esp_err_to_name((esp_err_t)result));
        ESP_LOGW(TAG, "%s", g_status.last_error);
        return 0;
    }
    return 1;
}

static int alert_audio_finish(void *context)
{
    (void)context;
    if (!g_alert_audio_output_owned) {
        return 1;
    }
    esp_err_t result = audio_finish_output("time-alert-finish");
    g_alert_audio_output_owned = 0;
    xSemaphoreGive(g_audio_lock);
    if (result != ESP_OK) {
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "time alert audio cleanup failed: %s", esp_err_to_name(result));
        ESP_LOGW(TAG, "%s", g_status.last_error);
        return 0;
    }
    return 1;
}

static void alert_audio_notify(void)
{
    if (g_alert_audio_task != NULL) {
        xTaskNotifyGive(g_alert_audio_task);
    }
}

static alert_audio_intent_t alert_audio_intent_snapshot(void)
{
    alert_audio_intent_t intent;
    portENTER_CRITICAL(&g_alert_audio_intent_lock);
    intent = g_alert_audio_intent;
    portEXIT_CRITICAL(&g_alert_audio_intent_lock);
    return intent;
}

static void alert_audio_apply_intent(const alert_audio_intent_t *intent)
{
    pj_alert_audio_set_volume(&g_alert_audio, intent->volume);
    (void)pj_alert_audio_set_recording(&g_alert_audio, intent->recording);

    uint64_t current_id = g_alert_audio.alert_id;
    if (intent->alert_id == 0) {
        if (current_id != 0) {
            (void)pj_alert_audio_stop(&g_alert_audio, current_id);
        }
        return;
    }
    if (current_id != intent->alert_id) {
        (void)pj_alert_audio_transition(&g_alert_audio, current_id,
                                        intent->alert_id, intent->kind,
                                        intent->deferred);
    } else if (intent->deferred || intent->recording) {
        (void)pj_alert_audio_defer(&g_alert_audio, intent->alert_id);
    } else {
        (void)pj_alert_audio_resume(&g_alert_audio, intent->alert_id);
    }
}

static void alert_audio_task(void *arg)
{
    (void)arg;
    int16_t scratch[PJ_ALERT_AUDIO_BLOCK_SAMPLES];
    while (1) {
        alert_audio_intent_t intent = alert_audio_intent_snapshot();
        alert_audio_apply_intent(&intent);
        pj_alert_audio_result_t result = pj_alert_audio_pump(&g_alert_audio, scratch);
        int active = g_alert_audio.state == PJ_ALERT_AUDIO_PLAYING ||
                     g_alert_audio.cleanup_pending;
        if (result < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
        } else if (result == PJ_ALERT_AUDIO_BLOCK_WRITTEN) {
            /* The blocking I2S write provides the producer pacing. */
        } else if (result == PJ_ALERT_AUDIO_SILENT_BLOCK) {
            vTaskDelay(pdMS_TO_TICKS(10));
        } else if (!active) {
            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static esp_err_t alert_audio_start(void)
{
    pj_alert_audio_io_t io = {
        .prepare = alert_audio_prepare,
        .write = alert_audio_write,
        .finish = alert_audio_finish,
    };
    uint8_t volume = (uint8_t)settings_codec_volume_snapshot();
    portENTER_CRITICAL(&g_alert_audio_intent_lock);
    g_alert_audio_intent.volume = volume;
    portEXIT_CRITICAL(&g_alert_audio_intent_lock);
    pj_alert_audio_init(&g_alert_audio, &io, volume);
    if (xTaskCreate(alert_audio_task, "pj-time-alert", PJ_ALERT_AUDIO_TASK_STACK,
                    NULL, 5, &g_alert_audio_task) != pdPASS) {
        g_alert_audio_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static int alert_audio_desired(void)
{
    return alert_audio_intent_snapshot().alert_id != 0;
}

static void alert_audio_set_volume(int codec_volume)
{
    portENTER_CRITICAL(&g_alert_audio_intent_lock);
    g_alert_audio_intent.volume = (uint8_t)codec_volume;
    portEXIT_CRITICAL(&g_alert_audio_intent_lock);
    alert_audio_notify();
}

static void alert_audio_set_recording(int recording)
{
    portENTER_CRITICAL(&g_alert_audio_intent_lock);
    g_alert_audio_intent.recording = recording != 0;
    if (!recording) {
        g_alert_audio_intent.deferred = 0;
    }
    portEXIT_CRITICAL(&g_alert_audio_intent_lock);
    alert_audio_notify();
}

static void alert_audio_project(const pj_time_alert_t *alert,
                                pj_time_conflict_action_t action)
{
    uint64_t alert_id = alert != NULL ? alert->id : 0;
    pj_alert_audio_kind_t kind = 0;
    if (alert != NULL) {
        kind = alert->source == PJ_TIME_ALERT_TIMER ?
            PJ_ALERT_AUDIO_TIMER : alert->source == PJ_TIME_ALERT_INTERVAL ?
            PJ_ALERT_AUDIO_INTERVAL : PJ_ALERT_AUDIO_ALARM;
    }
    portENTER_CRITICAL(&g_alert_audio_intent_lock);
    g_alert_audio_intent.alert_id = alert_id;
    g_alert_audio_intent.kind = kind;
    g_alert_audio_intent.deferred = alert_id != 0 &&
        action == PJ_TIME_VISUAL_DEFER_AUDIO &&
        g_alert_audio_intent.recording;
    portEXIT_CRITICAL(&g_alert_audio_intent_lock);
    alert_audio_notify();
}

static i2s_std_gpio_config_t audio_i2s_gpio_config(gpio_num_t dout_pin)
{
    return (i2s_std_gpio_config_t) {
        .mclk = I2S_MCLK_PIN,
        .bclk = I2S_BCLK_PIN,
        .ws = I2S_WS_PIN,
        .dout = dout_pin,
        .din = I2S_DIN_PIN,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        },
    };
}

static esp_err_t audio_reconfigure_tx_dout(int dout_gpio_override)
{
    if (dout_gpio_override < 0) {
        return ESP_OK;
    }

    gpio_num_t dout_pin = (gpio_num_t)dout_gpio_override;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(dout_pin)) {
        ESP_LOGW(TAG, "Rejected invalid I2S DOUT GPIO override: %d", dout_gpio_override);
        return ESP_ERR_INVALID_ARG;
    }
    if (g_i2s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (dout_pin == g_i2s_dout_pin) {
        return ESP_OK;
    }

    esp_err_t disable_err = i2s_channel_disable(g_i2s_tx_chan);
    if (disable_err != ESP_OK && disable_err != ESP_ERR_INVALID_STATE) {
        return disable_err;
    }

    i2s_std_gpio_config_t gpio_cfg = audio_i2s_gpio_config(dout_pin);
    esp_err_t reconfig_err = i2s_channel_reconfig_std_gpio(g_i2s_tx_chan, &gpio_cfg);
    esp_err_t enable_err = i2s_channel_enable(g_i2s_tx_chan);
    if (reconfig_err != ESP_OK) {
        return reconfig_err;
    }
    if (enable_err != ESP_OK && enable_err != ESP_ERR_INVALID_STATE) {
        return enable_err;
    }

    g_i2s_dout_pin = dout_pin;
    ESP_LOGI(TAG, "I2S TX DOUT reassigned for audio diagnostic: gpio=%d", dout_gpio_override);
    return ESP_OK;
}

static void power_init(void)
{
    gpio_config_t gpio_conf = {
        .pin_bit_mask = (1ULL << EPD_PWR_PIN) | (1ULL << AUDIO_PWR_PIN) | (1ULL << AUDIO_PA_PIN) |
                        (1ULL << VBAT_PWR_PIN) | (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));
    gpio_set_level(EPD_PWR_PIN, 0);
    gpio_set_level(AUDIO_PWR_PIN, 0);
    audio_pa_set(0);
    gpio_set_level(VBAT_PWR_PIN, 1);
    gpio_set_level(LED_PIN, 0);
}

static esp_err_t epd_spi_byte(uint8_t data)
{
    spi_transaction_t transaction = {
        .length = 8,
        .tx_buffer = &data,
    };
    return spi_device_polling_transmit(g_epd_spi, &transaction);
}

static esp_err_t network_stack_init(void)
{
    esp_err_t err;

    if (g_network_stack_ready) {
        return ESP_OK;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    g_network_stack_ready = 1;
    return ESP_OK;
}

static void connectivity_state_init(void)
{
    uint64_t now_ms = board_monotonic_ms();
    portENTER_CRITICAL(&g_connectivity_lock);
    pj_wifi_state_init(&g_wifi_state, g_wifi_credentials_stored, now_ms);
    pj_time_sync_init(&g_time_sync_state, g_time_wall_trusted, now_ms);
    g_connectivity_state_initialized = 1;
    portEXIT_CRITICAL(&g_connectivity_lock);
}

static void connectivity_state_snapshot(pj_wifi_state_t *wifi,
                                        pj_time_sync_state_t *time_sync)
{
    portENTER_CRITICAL(&g_connectivity_lock);
    if (wifi != NULL) {
        *wifi = g_wifi_state;
    }
    if (time_sync != NULL) {
        *time_sync = g_time_sync_state;
    }
    portEXIT_CRITICAL(&g_connectivity_lock);
}

static int format_utc_time(int64_t epoch_s, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0 || !pj_time_sync_epoch_valid(epoch_s)) {
        return 0;
    }
    time_t value = (time_t)epoch_s;
    struct tm utc;
    if (gmtime_r(&value, &utc) == NULL) {
        return 0;
    }
    return strftime(out, out_size, "%Y-%m-%dT%H:%M:%SZ", &utc) != 0;
}

/* LwIP provides this hook as weak. Validate the UTC response before allowing
 * it to replace POSIX system time. Local civil/RTC publication stays gated
 * until the product has an explicit timezone rule. */
void sntp_sync_time(struct timeval *tv)
{
    uint64_t now_ms = board_monotonic_ms();
    if (tv == NULL || tv->tv_usec < 0 || tv->tv_usec >= 1000000 ||
        !pj_time_sync_epoch_valid((int64_t)tv->tv_sec)) {
        portENTER_CRITICAL(&g_connectivity_lock);
        (void)pj_time_sync_on_success(&g_time_sync_state,
                                      tv == NULL ? 0 : (int64_t)tv->tv_sec,
                                      0, 0, now_ms);
        portEXIT_CRITICAL(&g_connectivity_lock);
        esp_sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
        ESP_LOGW(TAG, "SNTP response rejected outside supported UTC range");
        return;
    }

    struct timeval old_time = {0};
    (void)gettimeofday(&old_time, NULL);
    int old_valid = pj_time_sync_epoch_valid((int64_t)old_time.tv_sec);
    int64_t old_epoch_ms = (int64_t)old_time.tv_sec * 1000ll +
                           old_time.tv_usec / 1000;
    if (settimeofday(tv, NULL) != 0) {
        portENTER_CRITICAL(&g_connectivity_lock);
        pj_time_sync_on_system_clock_failed(&g_time_sync_state, now_ms);
        portEXIT_CRITICAL(&g_connectivity_lock);
        esp_sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
        ESP_LOGE(TAG, "SNTP UTC could not be published to POSIX system time");
        return;
    }

    portENTER_CRITICAL(&g_connectivity_lock);
    (void)pj_time_sync_on_success(&g_time_sync_state, (int64_t)tv->tv_sec,
                                  old_valid, old_epoch_ms, now_ms);
    if (g_wifi_state.has_ip) {
        pj_wifi_state_set_last_success_utc(&g_wifi_state,
                                           (int64_t)tv->tv_sec);
    }
    pj_time_sync_correction_t correction = g_time_sync_state.correction;
    portEXIT_CRITICAL(&g_connectivity_lock);
    esp_sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
    ESP_LOGI(TAG,
             "SNTP UTC acquired (%s); local civil/RTC publication awaits timezone policy",
             pj_time_sync_correction_name(correction));
}

static esp_err_t connectivity_sntp_ensure_initialized(void)
{
    if (g_sntp_initialized) {
        return ESP_OK;
    }
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.start = false;
    config.wait_for_sync = false;
    config.smooth_sync = false;
    config.sync_cb = NULL;
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err == ESP_OK) {
        g_sntp_initialized = 1;
    }
    return err;
}

static void connectivity_task(void *arg)
{
    (void)arg;
    while (1) {
        uint64_t now_ms = board_monotonic_ms();
        portENTER_CRITICAL(&g_connectivity_lock);
        pj_wifi_action_t wifi_action = pj_wifi_state_tick(&g_wifi_state, now_ms);
        pj_time_sync_action_t time_action = pj_time_sync_tick(&g_time_sync_state,
                                                               now_ms);
        portEXIT_CRITICAL(&g_connectivity_lock);

        if (g_wifi_started && wifi_action != PJ_WIFI_ACTION_NONE) {
            if (wifi_action == PJ_WIFI_ACTION_RECONNECT) {
                portENTER_CRITICAL(&g_connectivity_lock);
                g_wifi_control_disconnect_pending = 1;
                portEXIT_CRITICAL(&g_connectivity_lock);
                esp_err_t disconnect_err = esp_wifi_disconnect();
                if (disconnect_err == ESP_OK) {
                    vTaskDelay(pdMS_TO_TICKS(PJ_WIFI_RECONNECT_SETTLE_MS));
                } else {
                    portENTER_CRITICAL(&g_connectivity_lock);
                    g_wifi_control_disconnect_pending = 0;
                    portEXIT_CRITICAL(&g_connectivity_lock);
                }
            }
            esp_err_t connect_err = esp_wifi_connect();
            if (connect_err != ESP_OK) {
                portENTER_CRITICAL(&g_connectivity_lock);
                pj_wifi_state_on_connect_request_failed(&g_wifi_state,
                                                        board_monotonic_ms());
                portEXIT_CRITICAL(&g_connectivity_lock);
                ESP_LOGW(TAG, "Wi-Fi connect request failed: %s",
                         esp_err_to_name(connect_err));
            }
        }

        if (time_action == PJ_TIME_SYNC_ACTION_START) {
            esp_err_t start_err = connectivity_sntp_ensure_initialized();
            if (start_err == ESP_OK) {
                /* ESP-IDF restarts the underlying SNTP client here, which is
                 * the supported path for bounded retries and refreshes. */
                start_err = esp_netif_sntp_start();
            }
            if (start_err != ESP_OK) {
                portENTER_CRITICAL(&g_connectivity_lock);
                pj_time_sync_on_start_failed(&g_time_sync_state,
                                             board_monotonic_ms());
                portEXIT_CRITICAL(&g_connectivity_lock);
                ESP_LOGW(TAG, "SNTP start failed: %s",
                         esp_err_to_name(start_err));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(PJ_CONNECTIVITY_POLL_MS));
    }
}

static esp_err_t connectivity_runtime_start(void)
{
    if (!g_connectivity_state_initialized) {
        connectivity_state_init();
    }
    esp_err_t sntp_err = connectivity_sntp_ensure_initialized();
    if (sntp_err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP initialization deferred: %s",
                 esp_err_to_name(sntp_err));
    }
    if (g_connectivity_task_started) {
        return ESP_OK;
    }
    BaseType_t created = xTaskCreate(connectivity_task, "pj-connectivity",
                                     PJ_CONNECTIVITY_TASK_STACK, NULL, 4, NULL);
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    g_connectivity_task_started = 1;
    return ESP_OK;
}

static esp_err_t mdns_start(void)
{
    if (g_mdns_started) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(mdns_init(), TAG, "mDNS init failed");
    ESP_RETURN_ON_ERROR(mdns_hostname_set(g_status.device_id), TAG, "mDNS hostname failed");
    ESP_RETURN_ON_ERROR(mdns_instance_name_set("Pocket Journal"), TAG, "mDNS instance failed");
    mdns_txt_item_t txt[] = {
        {"device_id", g_status.device_id},
        {"path", "/v1/status"},
    };
    ESP_RETURN_ON_ERROR(mdns_service_add(g_status.device_id, "_pocket-journal", "_tcp", 80,
                                         txt, sizeof(txt) / sizeof(txt[0])),
                        TAG, "mDNS service registration failed");
    g_mdns_started = 1;
    ESP_LOGI(TAG, "mDNS advertised: %s.local _pocket-journal._tcp", g_status.device_id);
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        portENTER_CRITICAL(&g_connectivity_lock);
        pj_wifi_state_on_driver_started(&g_wifi_state, board_monotonic_ms());
        portEXIT_CRITICAL(&g_connectivity_lock);
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        portENTER_CRITICAL(&g_connectivity_lock);
        pj_wifi_state_on_associated(&g_wifi_state, board_monotonic_ms());
        portEXIT_CRITICAL(&g_connectivity_lock);
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = event_data;
        unsigned reason = event == NULL ? 0U : (unsigned)event->reason;
        int control_disconnect = 0;
        portENTER_CRITICAL(&g_connectivity_lock);
        if (g_wifi_control_disconnect_pending) {
            /* A DHCP/reprovision reconnect is transport control, not a new
             * connection failure to classify or count. */
            g_wifi_control_disconnect_pending = 0;
            control_disconnect = 1;
        } else {
            pj_wifi_state_on_disconnected(&g_wifi_state, reason,
                                          board_monotonic_ms());
        }
        pj_time_sync_on_network_lost(&g_time_sync_state,
                                     board_monotonic_ms());
        portEXIT_CRITICAL(&g_connectivity_lock);
        g_status.wifi = PJ_BOARD_SERVICE_UNAVAILABLE;
        (void)snprintf(g_status.ip_addr, sizeof(g_status.ip_addr), "0.0.0.0");
        if (!control_disconnect) {
            (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                           "Wi-Fi disconnected (reason %u); retry scheduled",
                           reason);
        }
        return;
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        wifi_ap_record_t ap = {0};
        int rssi_dbm = -127;
        unsigned channel = 0;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            rssi_dbm = ap.rssi;
            channel = ap.primary;
        }
        portENTER_CRITICAL(&g_connectivity_lock);
        pj_wifi_state_on_got_ip(&g_wifi_state, board_monotonic_ms(),
                                rssi_dbm, channel);
        pj_time_sync_on_ip(&g_time_sync_state, board_monotonic_ms());
        portEXIT_CRITICAL(&g_connectivity_lock);
        g_status.wifi = PJ_BOARD_SERVICE_READY;
        if (event != NULL) {
            (void)snprintf(g_status.ip_addr, sizeof(g_status.ip_addr), IPSTR,
                           IP2STR(&event->ip_info.ip));
        }
        g_status.last_error[0] = '\0';
        ESP_LOGI(TAG, "Wi-Fi connected: ip=%s rssi=%d channel=%u",
                 g_status.ip_addr, rssi_dbm, channel);
        esp_err_t mdns_err = mdns_start();
        if (mdns_err != ESP_OK) {
            (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                           "mDNS start failed: %s", esp_err_to_name(mdns_err));
            ESP_LOGW(TAG, "%s", g_status.last_error);
        }
        return;
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) {
        portENTER_CRITICAL(&g_connectivity_lock);
        pj_wifi_state_on_lost_ip(&g_wifi_state, board_monotonic_ms());
        pj_time_sync_on_network_lost(&g_time_sync_state,
                                     board_monotonic_ms());
        portEXIT_CRITICAL(&g_connectivity_lock);
        g_status.wifi = PJ_BOARD_SERVICE_UNAVAILABLE;
        (void)snprintf(g_status.ip_addr, sizeof(g_status.ip_addr), "0.0.0.0");
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "Wi-Fi lost its DHCP lease; retry scheduled");
    }
}

static esp_err_t wifi_apply_config(void)
{
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, g_wifi_ssid, strlen(g_wifi_ssid));
    memcpy(config.sta.password, g_wifi_password, strlen(g_wifi_password));
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    return esp_wifi_set_config(WIFI_IF_STA, &config);
}

static esp_err_t wifi_start_or_reconfigure(void)
{
    if (!g_wifi_credentials_stored) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(network_stack_init(), TAG, "network stack init failed");
    if (!g_wifi_started) {
        g_wifi_sta_netif = esp_netif_create_default_wifi_sta();
        if (g_wifi_sta_netif == NULL) {
            return ESP_ERR_NO_MEM;
        }
        wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "Wi-Fi driver init failed");
        ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                       wifi_event_handler, NULL),
                            TAG, "Wi-Fi event handler registration failed");
        ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                       wifi_event_handler, NULL),
                            TAG, "IP event handler registration failed");
        ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP,
                                                       wifi_event_handler, NULL),
                            TAG, "lost-IP event handler registration failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Wi-Fi RAM storage failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Wi-Fi station mode failed");
        ESP_RETURN_ON_ERROR(wifi_apply_config(), TAG, "Wi-Fi station config failed");
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start failed");
        g_wifi_started = 1;
    } else {
        portENTER_CRITICAL(&g_connectivity_lock);
        g_wifi_control_disconnect_pending = 1;
        portEXIT_CRITICAL(&g_connectivity_lock);
        esp_err_t disconnect_err = esp_wifi_disconnect();
        if (disconnect_err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(PJ_WIFI_RECONNECT_SETTLE_MS));
        } else {
            portENTER_CRITICAL(&g_connectivity_lock);
            g_wifi_control_disconnect_pending = 0;
            portEXIT_CRITICAL(&g_connectivity_lock);
        }
        ESP_RETURN_ON_ERROR(wifi_apply_config(), TAG, "Wi-Fi station reconfiguration failed");
    }
    portENTER_CRITICAL(&g_connectivity_lock);
    pj_wifi_state_set_provisioned(&g_wifi_state, 1, board_monotonic_ms());
    pj_wifi_state_on_driver_started(&g_wifi_state, board_monotonic_ms());
    portEXIT_CRITICAL(&g_connectivity_lock);
    ESP_RETURN_ON_ERROR(connectivity_runtime_start(), TAG,
                        "connectivity task start failed");
    g_status.wifi = PJ_BOARD_SERVICE_UNAVAILABLE;
    (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                   "Wi-Fi connecting");
    return ESP_OK;
}

static void wifi_apply_provisioning_status(void)
{
    if (!g_wifi_credentials_stored) {
        return;
    }
    g_status.ble_provisioning = PJ_BOARD_SERVICE_READY;
    if (g_status.wifi != PJ_BOARD_SERVICE_READY) {
        g_status.wifi = PJ_BOARD_SERVICE_UNAVAILABLE;
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error), "Wi-Fi credentials stored");
    }
}

static int settings_take(TickType_t timeout)
{
    return g_settings_lock == NULL || xSemaphoreTake(g_settings_lock, timeout) == pdTRUE;
}

static void settings_give(void)
{
    if (g_settings_lock != NULL) {
        xSemaphoreGive(g_settings_lock);
    }
}

static int settings_codec_volume_snapshot(void)
{
    int volume = 0;
    if (settings_take(portMAX_DELAY)) {
        volume = pj_settings_codec_volume(g_settings.volume);
        settings_give();
    }
    return volume;
}

static int static_art_take(TickType_t timeout)
{
    return g_static_art_lock == NULL || xSemaphoreTake(g_static_art_lock, timeout) == pdTRUE;
}

static void static_art_give(void)
{
    if (g_static_art_lock != NULL) {
        xSemaphoreGive(g_static_art_lock);
    }
}

static int home_layout_take(TickType_t timeout)
{
    return g_home_layout_lock == NULL || xSemaphoreTake(g_home_layout_lock, timeout) == pdTRUE;
}

static void home_layout_give(void)
{
    if (g_home_layout_lock != NULL) {
        xSemaphoreGive(g_home_layout_lock);
    }
}

static void home_layout_load(void)
{
    pj_home_layout_defaults(&g_home_layout);
    nvs_handle_t nvs;
    if (nvs_open(PJ_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    size_t record_size = 0;
    esp_err_t err = nvs_get_blob(nvs, PJ_NVS_HOME_LAYOUT, NULL, &record_size);
    if (err == ESP_OK && record_size == PJ_HOME_RECORD_BYTES) {
        uint8_t *record = malloc(record_size);
        pj_home_layout_t *loaded = malloc(sizeof(*loaded));
        int read_ok = record != NULL && loaded != NULL &&
                      nvs_get_blob(nvs, PJ_NVS_HOME_LAYOUT, record, &record_size) == ESP_OK;
        if (read_ok && pj_home_layout_decode_or_default(record, record_size, loaded)) {
            ESP_LOGI(TAG, "Home layout loaded from NVS: title=%s slots=%u",
                     loaded->title, (unsigned)loaded->slot_count);
        } else {
            ESP_LOGW(TAG, "Stored home layout failed record validation; using built-in layout");
        }
        if (loaded != NULL) {
            if (!read_ok) {
                pj_home_layout_defaults(loaded);
            }
            g_home_layout = *loaded;
        }
        free(loaded);
        free(record);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Stored home layout has invalid size; using built-in layout");
    }
    nvs_close(nvs);
}

static esp_err_t home_layout_save(const pj_home_layout_t *layout)
{
    uint8_t *record = malloc(PJ_HOME_RECORD_BYTES);
    if (record == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (pj_home_layout_encode_record(layout, record, PJ_HOME_RECORD_BYTES) != PJ_HOME_RECORD_BYTES) {
        free(record);
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PJ_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, PJ_NVS_HOME_LAYOUT, record, PJ_HOME_RECORD_BYTES);
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    free(record);
    return err;
}

static esp_err_t home_layout_replace(const pj_home_layout_t *layout)
{
    pj_home_layout_t canonical;
    if (!pj_home_layout_canonical_copy(&canonical, layout) || !home_layout_take(portMAX_DELAY)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = home_layout_save(&canonical);
    if (err == ESP_OK) {
        g_home_layout = canonical;
        g_home_layout_update_pending = 1;
    }
    home_layout_give();
    return err;
}

static void static_art_slot_path(int slot, char *path, size_t path_size)
{
    (void)snprintf(path, path_size, "%s/static-art-%d.bin", g_status.storage_path, slot);
}

static int static_art_read_slot(int slot, pj_static_art_t *art)
{
    if (slot < 0 || slot >= PJ_STATIC_ART_SLOT_COUNT || art == NULL) {
        return 0;
    }
    char path[64];
    static_art_slot_path(slot, path, sizeof(path));
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    uint8_t *record = malloc(PJ_STATIC_ART_RECORD_BYTES);
    if (record == NULL) {
        fclose(file);
        return 0;
    }
    size_t got = fread(record, 1, PJ_STATIC_ART_RECORD_BYTES, file);
    int extra = fgetc(file);
    fclose(file);
    int valid = got == PJ_STATIC_ART_RECORD_BYTES && extra == EOF &&
                pj_static_art_decode_record(record, got, art);
    free(record);
    return valid;
}

static void static_art_load(void)
{
    int preferred_slot = -1;
    nvs_handle_t nvs;
    if (nvs_open(PJ_NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t stored_slot = 0;
        if (nvs_get_u8(nvs, PJ_NVS_STATIC_ART_SLOT, &stored_slot) == ESP_OK &&
            stored_slot < PJ_STATIC_ART_SLOT_COUNT) {
            preferred_slot = stored_slot;
        }
        nvs_close(nvs);
    }

    int candidates[PJ_STATIC_ART_SLOT_COUNT] = {
        preferred_slot,
        preferred_slot == 0 ? 1 : 0,
    };
    if (preferred_slot < 0) {
        candidates[0] = 0;
        candidates[1] = 1;
    }
    for (int i = 0; i < PJ_STATIC_ART_SLOT_COUNT; i++) {
        if (static_art_read_slot(candidates[i], &g_static_art)) {
            g_static_art_slot = candidates[i];
            g_static_art_valid = 1;
            ESP_LOGI(TAG, "Static art loaded from microSD slot %d", g_static_art_slot);
            return;
        }
    }
    if (preferred_slot >= 0) {
        ESP_LOGW(TAG, "Stored static art slots failed record validation");
    }
}

static esp_err_t static_art_save(const pj_static_art_t *art, int *saved_slot)
{
    if (g_status.storage != PJ_BOARD_SERVICE_READY) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!storage_preflight(PJ_STATIC_ART_RECORD_BYTES, "static art")) {
        return g_status.storage_health == PJ_STORAGE_HEALTH_FULL ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
    uint8_t *record = malloc(PJ_STATIC_ART_RECORD_BYTES);
    if (record == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (pj_static_art_encode_record(art, record, PJ_STATIC_ART_RECORD_BYTES) == 0) {
        free(record);
        return ESP_ERR_INVALID_ARG;
    }
    int next_slot = g_static_art_slot == 0 ? 1 : 0;
    char path[64];
    static_art_slot_path(next_slot, path, sizeof(path));
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        free(record);
        return ESP_FAIL;
    }
    size_t written = fwrite(record, 1, PJ_STATIC_ART_RECORD_BYTES, file);
    int write_failed = written != PJ_STATIC_ART_RECORD_BYTES;
    if (fflush(file) != 0) {
        write_failed = 1;
    }
    if (fsync(fileno(file)) != 0) {
        write_failed = 1;
    }
    if (fclose(file) != 0) {
        write_failed = 1;
    }
    if (write_failed) {
        remove(path);
        free(record);
        return ESP_FAIL;
    }

    pj_static_art_t *verified = malloc(sizeof(*verified));
    if (verified == NULL) {
        free(record);
        return ESP_ERR_NO_MEM;
    }
    if (!static_art_read_slot(next_slot, verified) ||
        memcmp(verified->pixels, art->pixels, sizeof(verified->pixels)) != 0) {
        free(verified);
        free(record);
        return ESP_ERR_INVALID_CRC;
    }
    free(verified);

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PJ_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, PJ_NVS_STATIC_ART_SLOT, (uint8_t)next_slot);
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err == ESP_OK && saved_slot != NULL) {
        *saved_slot = next_slot;
    }
    free(record);
    return err;
}

static esp_err_t nvs_read_optional_i32(nvs_handle_t nvs, const char *key,
                                       int *value, int *found)
{
    int32_t stored = 0;
    esp_err_t err = nvs_get_i32(nvs, key, &stored);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *found = 0;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    *value = (int)stored;
    *found = 1;
    return ESP_OK;
}

static const char *settings_slot_key(unsigned slot)
{
    return slot == 0 ? PJ_NVS_SETTINGS_SLOT_0 : PJ_NVS_SETTINGS_SLOT_1;
}

static pj_settings_io_result_t settings_read_slot(
    void *context, unsigned slot, uint8_t *record, size_t *record_size)
{
    (void)context;
    if (slot >= PJ_SETTINGS_SLOT_COUNT || record == NULL || record_size == NULL) {
        g_settings_io_error = ESP_ERR_INVALID_ARG;
        return PJ_SETTINGS_IO_ERROR;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PJ_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return PJ_SETTINGS_IO_NOT_FOUND;
    }
    if (err != ESP_OK) {
        g_settings_io_error = err;
        return PJ_SETTINGS_IO_ERROR;
    }

    size_t stored_size = 0;
    err = nvs_get_blob(nvs, settings_slot_key(slot), NULL, &stored_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs);
        return PJ_SETTINGS_IO_NOT_FOUND;
    }
    if (err != ESP_OK || stored_size > *record_size) {
        nvs_close(nvs);
        g_settings_io_error = err == ESP_OK ? ESP_ERR_INVALID_SIZE : err;
        return PJ_SETTINGS_IO_ERROR;
    }
    err = nvs_get_blob(nvs, settings_slot_key(slot), record, &stored_size);
    nvs_close(nvs);
    if (err != ESP_OK) {
        g_settings_io_error = err;
        return PJ_SETTINGS_IO_ERROR;
    }
    *record_size = stored_size;
    return PJ_SETTINGS_IO_OK;
}

static pj_settings_io_result_t settings_write_slot(
    void *context, unsigned slot, const uint8_t *record, size_t record_size)
{
    (void)context;
    if (slot >= PJ_SETTINGS_SLOT_COUNT || record == NULL ||
        record_size != PJ_SETTINGS_RECORD_BYTES) {
        g_settings_io_error = ESP_ERR_INVALID_ARG;
        return PJ_SETTINGS_IO_ERROR;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PJ_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, settings_slot_key(slot), record, record_size);
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err != ESP_OK) {
        g_settings_io_error = err;
        return PJ_SETTINGS_IO_ERROR;
    }
    g_settings_io_error = ESP_OK;
    return PJ_SETTINGS_IO_OK;
}

static esp_err_t settings_load_legacy(pj_settings_t *loaded)
{
    pj_settings_defaults(loaded);
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PJ_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    int value = 0;
    int found = 0;
    int found_any = 0;
#define READ_LEGACY(key, field) do { \
        err = nvs_read_optional_i32(nvs, (key), &value, &found); \
        if (err != ESP_OK) { \
            goto done; \
        } \
        if (found) { \
            (field) = value; \
            found_any = 1; \
        } \
    } while (0)
    READ_LEGACY(PJ_NVS_VOLUME, loaded->volume);
    READ_LEGACY(PJ_NVS_DARK_MODE, loaded->dark_mode);
    READ_LEGACY(PJ_NVS_ALARM_ENABLED, loaded->alarm_enabled);
    READ_LEGACY(PJ_NVS_ALARM_HOUR, loaded->alarm_hour);
    READ_LEGACY(PJ_NVS_ALARM_MINUTE, loaded->alarm_minute);
    READ_LEGACY(PJ_NVS_TIMER_SECONDS, loaded->timer_seconds);
    READ_LEGACY(PJ_NVS_INTERVAL_SECONDS, loaded->interval_seconds);
    int interval_loaded = found;
    READ_LEGACY(PJ_NVS_CLOCK_24H, loaded->clock_24h);
    READ_LEGACY(PJ_NVS_TEMP_F, loaded->temperature_fahrenheit);
    READ_LEGACY(PJ_NVS_TEXT_SIZE, loaded->transcript_font_size);
    int settings_version = 0;
    READ_LEGACY(PJ_NVS_SETTINGS_VERSION, settings_version);
#undef READ_LEGACY
    if (settings_version < PJ_SETTINGS_VERSION && interval_loaded &&
        loaded->interval_seconds == 1500) {
        loaded->interval_seconds = 90;
    }
    if (!found_any) {
        err = ESP_ERR_NVS_NOT_FOUND;
    } else if (!pj_settings_valid(loaded)) {
        err = ESP_ERR_INVALID_STATE;
    }
done:
    nvs_close(nvs);
    return err;
}

static esp_err_t settings_load(void)
{
    pj_settings_store_init(&g_settings_store, NULL, settings_read_slot,
                           settings_write_slot);
    g_settings_store_ready = 0;
    g_settings_io_error = ESP_OK;

    pj_settings_t loaded;
    pj_settings_load_result_t result = pj_settings_store_load(
        &g_settings_store, &loaded);
    if (result == PJ_SETTINGS_LOAD_ERROR) {
        return g_settings_io_error == ESP_OK ? ESP_ERR_INVALID_CRC :
                                               g_settings_io_error;
    }
    if (result == PJ_SETTINGS_LOAD_NOT_FOUND) {
        esp_err_t legacy_err = settings_load_legacy(&loaded);
        if (legacy_err == ESP_ERR_NVS_NOT_FOUND) {
            pj_settings_defaults(&loaded);
        } else if (legacy_err != ESP_OK) {
            return legacy_err;
        }
        g_settings_store_ready = 1;
        if (!pj_settings_store_save(&g_settings_store, &loaded)) {
            ESP_LOGW(TAG, "Settings migration save failed: %s",
                     esp_err_to_name(g_settings_io_error));
        }
    } else {
        g_settings_store_ready = !g_settings_store.degraded;
    }

    g_settings = loaded;
    ESP_LOGI(TAG, "Settings loaded: volume=%d theme=%s generation=%" PRIu32,
             loaded.volume, loaded.dark_mode ? "dark" : "light",
             g_settings_store.generation);
    return g_settings_store.degraded ?
        (g_settings_io_error == ESP_OK ? ESP_FAIL : g_settings_io_error) :
        ESP_OK;
}

static esp_err_t settings_save(const pj_settings_t *settings)
{
    if (!g_settings_store_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    g_settings_io_error = ESP_OK;
    return pj_settings_store_save(&g_settings_store, settings) ? ESP_OK :
        (g_settings_io_error == ESP_OK ? ESP_FAIL : g_settings_io_error);
}

static pj_time_controller_load_result_t time_controller_load_record(
    void *context, uint8_t record[PJ_TIME_STATE_RECORD_BYTES])
{
    (void)context;
    size_t record_size = PJ_TIME_STATE_RECORD_BYTES;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PJ_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return PJ_TIME_CONTROLLER_LOAD_NOT_FOUND;
    }
    if (err != ESP_OK) {
        return PJ_TIME_CONTROLLER_LOAD_IO_ERROR;
    }
    err = nvs_get_blob(nvs, PJ_NVS_TIME_STATE, record, &record_size);
    nvs_close(nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return PJ_TIME_CONTROLLER_LOAD_NOT_FOUND;
    }
    if (err != ESP_OK) {
        return PJ_TIME_CONTROLLER_LOAD_IO_ERROR;
    }
    if (record_size != PJ_TIME_STATE_RECORD_BYTES) {
        return PJ_TIME_CONTROLLER_LOAD_CORRUPT;
    }
    return PJ_TIME_CONTROLLER_LOAD_VALID;
}

static pj_time_controller_save_result_t time_controller_save_record(
    void *context, const uint8_t record[PJ_TIME_STATE_RECORD_BYTES])
{
    (void)context;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PJ_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, PJ_NVS_TIME_STATE, record,
                           PJ_TIME_STATE_RECORD_BYTES);
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err == ESP_OK) {
        return PJ_TIME_CONTROLLER_SAVE_OK;
    }
    return err == ESP_ERR_NVS_NOT_ENOUGH_SPACE
        ? PJ_TIME_CONTROLLER_SAVE_PERMANENT_ERROR
        : PJ_TIME_CONTROLLER_SAVE_TRANSIENT_ERROR;
}

static int rtc_wake_plan_save(void *context, const pj_rtc_wake_plan_t *plan)
{
    (void)context;
    if (!pj_rtc_wake_plan_valid(plan)) {
        return 0;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PJ_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, PJ_NVS_WAKE_PLAN, plan, sizeof(*plan));
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err == ESP_OK) {
        g_rtc_wake_plan = *plan;
        return 1;
    }
    return 0;
}

static int rtc_wake_plan_preserve(void *context, const pj_rtc_wake_plan_t *plan)
{
    (void)context;
    return pj_rtc_wake_plan_valid(plan);
}

typedef enum {
    RTC_WAKE_LOAD_IO_ERROR = -2,
    RTC_WAKE_LOAD_INVALID = -1,
    RTC_WAKE_LOAD_NOT_FOUND = 0,
    RTC_WAKE_LOAD_VALID = 1,
} rtc_wake_load_result_t;

static rtc_wake_load_result_t rtc_wake_plan_load(pj_rtc_wake_plan_t *plan)
{
    memset(plan, 0, sizeof(*plan));
    plan->version = PJ_RTC_WAKE_PLAN_VERSION;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PJ_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return RTC_WAKE_LOAD_NOT_FOUND;
    }
    if (err != ESP_OK) {
        return RTC_WAKE_LOAD_IO_ERROR;
    }
    size_t size = sizeof(*plan);
    err = nvs_get_blob(nvs, PJ_NVS_WAKE_PLAN, plan, &size);
    nvs_close(nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(plan, 0, sizeof(*plan));
        plan->version = PJ_RTC_WAKE_PLAN_VERSION;
        return RTC_WAKE_LOAD_NOT_FOUND;
    }
    if (err != ESP_OK) {
        return RTC_WAKE_LOAD_IO_ERROR;
    }
    return size == sizeof(*plan) && pj_rtc_wake_plan_valid(plan) ?
        RTC_WAKE_LOAD_VALID : RTC_WAKE_LOAD_INVALID;
}

static void settings_apply_codec_volume(int codec_volume)
{
    if (g_audio_playback_codec == NULL) {
        return;
    }
    int locked = g_audio_lock != NULL &&
                 xSemaphoreTake(g_audio_lock, pdMS_TO_TICKS(100)) == pdTRUE;
    if (g_audio_lock != NULL && !locked) {
        ESP_LOGI(TAG, "Codec volume update deferred until the next audio start");
        return;
    }
    int result = esp_codec_dev_set_out_vol(g_audio_playback_codec, codec_volume);
    if (locked) {
        xSemaphoreGive(g_audio_lock);
    }
    if (result != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Codec volume update failed: %s", esp_err_to_name((esp_err_t)result));
    }
}

static void wifi_load_provisioning(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PJ_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return;
    }

    size_t ssid_len = sizeof(g_wifi_ssid);
    size_t password_len = sizeof(g_wifi_password);
    char token[sizeof(g_status.token)];
    size_t token_len = sizeof(token);
    int have_ssid = nvs_get_str(nvs, PJ_NVS_WIFI_SSID, g_wifi_ssid, &ssid_len) == ESP_OK && g_wifi_ssid[0] != '\0';
    int have_password = nvs_get_str(nvs, PJ_NVS_WIFI_PASSWORD, g_wifi_password, &password_len) == ESP_OK;
    if (nvs_get_str(nvs, PJ_NVS_TOKEN, token, &token_len) == ESP_OK && token[0] != '\0') {
        (void)snprintf(g_status.token, sizeof(g_status.token), "%s", token);
    }
    nvs_close(nvs);

    if (have_ssid && have_password) {
        g_wifi_credentials_stored = 1;
        wifi_apply_provisioning_status();
        ESP_LOGI(TAG, "Wi-Fi credentials loaded from NVS");
    }
}

static esp_err_t wifi_save_provisioning(const char *ssid, const char *password, const char *token)
{
    if (ssid == NULL || ssid[0] == '\0' || password == NULL || token == NULL || token[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(ssid) > PJ_WIFI_SSID_MAX_LEN ||
        strlen(password) > PJ_WIFI_PASSWORD_MAX_LEN ||
        strlen(token) >= sizeof(g_status.token)) {
        return ESP_ERR_INVALID_SIZE;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PJ_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs, PJ_NVS_WIFI_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, PJ_NVS_WIFI_PASSWORD, password);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, PJ_NVS_TOKEN, token);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        return err;
    }

    (void)snprintf(g_wifi_ssid, sizeof(g_wifi_ssid), "%s", ssid);
    (void)snprintf(g_wifi_password, sizeof(g_wifi_password), "%s", password);
    (void)snprintf(g_status.token, sizeof(g_status.token), "%s", token);
    g_wifi_credentials_stored = 1;
    wifi_apply_provisioning_status();
    ESP_LOGI(TAG, "Wi-Fi credentials stored from partner provisioning");
    esp_err_t connect_err = wifi_start_or_reconfigure();
    return connect_err == ESP_ERR_INVALID_STATE ? ESP_OK : connect_err;
}

enum {
    PJ_BLE_FIELD_SSID = 1,
    PJ_BLE_FIELD_PASSWORD,
    PJ_BLE_FIELD_TOKEN,
    PJ_BLE_FIELD_COMMIT,
    PJ_BLE_FIELD_STATUS,
};

typedef struct {
    char ssid[PJ_WIFI_SSID_MAX_LEN + 1];
    char password[PJ_WIFI_PASSWORD_MAX_LEN + 1];
    char token[sizeof(g_status.token)];
} ble_provision_args_t;

static const ble_uuid128_t g_ble_service_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x7e);
static const ble_uuid128_t g_ble_ssid_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x7e);
static const ble_uuid128_t g_ble_password_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x7e);
static const ble_uuid128_t g_ble_token_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x04, 0x00, 0x40, 0x7e);
static const ble_uuid128_t g_ble_commit_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x05, 0x00, 0x40, 0x7e);
static const ble_uuid128_t g_ble_status_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x06, 0x00, 0x40, 0x7e);

static void ble_provision_task(void *arg)
{
    ble_provision_args_t *args = arg;
    esp_err_t err = wifi_save_provisioning(args->ssid, args->password, args->token);
    if (err == ESP_OK) {
        (void)snprintf(g_ble_state, sizeof(g_ble_state), "stored");
        g_status.ble_provisioning = PJ_BOARD_SERVICE_READY;
    } else {
        (void)snprintf(g_ble_state, sizeof(g_ble_state), "error-%s", esp_err_to_name(err));
        g_status.ble_provisioning = PJ_BOARD_SERVICE_ERROR;
    }
    memset(args, 0, sizeof(*args));
    free(args);
    g_ble_provision_task = NULL;
    vTaskDelete(NULL);
}

static int ble_write_string(struct ble_gatt_access_ctxt *ctxt, char *out, size_t out_size)
{
    uint16_t length = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, out, (uint16_t)(out_size - 1U), &length) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    out[length] = '\0';
    return 0;
}

static int ble_connection_encrypted(uint16_t conn_handle)
{
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn_handle, &desc) != 0) {
        return 0;
    }
    return desc.sec_state.encrypted && desc.sec_state.key_size >= 16;
}

static int ble_provision_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr_handle;
    intptr_t field = (intptr_t)arg;
    if (field == PJ_BLE_FIELD_STATUS && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char json[128];
        int length = snprintf(json, sizeof(json),
                              "{\"device_id\":\"%s\",\"state\":\"%s\",\"wifi\":\"%s\"}",
                              g_status.device_id, g_ble_state, service_name(g_status.wifi));
        return os_mbuf_append(ctxt->om, json, (uint16_t)length) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    if (!ble_connection_encrypted(conn_handle)) {
        (void)snprintf(g_ble_state, sizeof(g_ble_state), "pairing-required");
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    if (field == PJ_BLE_FIELD_SSID) {
        return ble_write_string(ctxt, g_ble_ssid, sizeof(g_ble_ssid));
    }
    if (field == PJ_BLE_FIELD_PASSWORD) {
        return ble_write_string(ctxt, g_ble_password, sizeof(g_ble_password));
    }
    if (field == PJ_BLE_FIELD_TOKEN) {
        return ble_write_string(ctxt, g_ble_token, sizeof(g_ble_token));
    }
    if (field != PJ_BLE_FIELD_COMMIT || OS_MBUF_PKTLEN(ctxt->om) != 1 ||
        ctxt->om->om_data[0] != 1 || g_ble_ssid[0] == '\0' || g_ble_token[0] == '\0') {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (g_ble_provision_task != NULL) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    ble_provision_args_t *args = malloc(sizeof(*args));
    if (args == NULL) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    (void)snprintf(args->ssid, sizeof(args->ssid), "%s", g_ble_ssid);
    (void)snprintf(args->password, sizeof(args->password), "%s", g_ble_password);
    (void)snprintf(args->token, sizeof(args->token), "%s", g_ble_token);
    (void)snprintf(g_ble_state, sizeof(g_ble_state), "applying");
    if (xTaskCreate(ble_provision_task, "pj-ble-provision", 4096, args, 4,
                    &g_ble_provision_task) != pdPASS) {
        free(args);
        g_ble_provision_task = NULL;
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    memset(g_ble_password, 0, sizeof(g_ble_password));
    memset(g_ble_token, 0, sizeof(g_ble_token));
    return 0;
}

static const struct ble_gatt_svc_def g_ble_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &g_ble_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {.uuid = &g_ble_ssid_uuid.u, .access_cb = ble_provision_access,
             .arg = (void *)(intptr_t)PJ_BLE_FIELD_SSID,
             .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC},
            {.uuid = &g_ble_password_uuid.u, .access_cb = ble_provision_access,
             .arg = (void *)(intptr_t)PJ_BLE_FIELD_PASSWORD,
             .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC},
            {.uuid = &g_ble_token_uuid.u, .access_cb = ble_provision_access,
             .arg = (void *)(intptr_t)PJ_BLE_FIELD_TOKEN,
             .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC},
            {.uuid = &g_ble_commit_uuid.u, .access_cb = ble_provision_access,
             .arg = (void *)(intptr_t)PJ_BLE_FIELD_COMMIT,
             .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC},
            {.uuid = &g_ble_status_uuid.u, .access_cb = ble_provision_access,
             .arg = (void *)(intptr_t)PJ_BLE_FIELD_STATUS, .flags = BLE_GATT_CHR_F_READ},
            {0},
        },
    },
    {0},
};

static int ble_gap_event(struct ble_gap_event *event, void *arg);

static void ble_advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    const char *name = ble_svc_gap_device_name();
    fields.uuids128 = (ble_uuid128_t *)&g_ble_service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE advertising fields failed: %d", rc);
        return;
    }
    struct ble_hs_adv_fields response = {0};
    response.name = (uint8_t *)name;
    response.name_len = strlen(name);
    response.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE scan response fields failed: %d", rc);
        return;
    }
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(g_ble_addr_type, NULL, BLE_HS_FOREVER, &params,
                           ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE advertising start failed: %d", rc);
        return;
    }
    (void)snprintf(g_ble_state, sizeof(g_ble_state), "advertising");
    g_status.ble_provisioning = PJ_BOARD_SERVICE_READY;
    ESP_LOGI(TAG, "BLE provisioning advertising as %s", name);
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_CONNECT) {
        if (event->connect.status != 0) {
            ble_advertise();
        } else {
            (void)snprintf(g_ble_state, sizeof(g_ble_state), "pairing-required");
        }
    } else if (event->type == BLE_GAP_EVENT_ENC_CHANGE) {
        struct ble_gap_conn_desc desc;
        if (event->enc_change.status == 0 &&
            ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0 &&
            desc.sec_state.encrypted) {
            (void)snprintf(g_ble_state, sizeof(g_ble_state), "paired");
        } else {
            (void)snprintf(g_ble_state, sizeof(g_ble_state), "pairing-required");
        }
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT ||
               event->type == BLE_GAP_EVENT_ADV_COMPLETE) {
        ble_advertise();
    }
    return 0;
}

static void ble_on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0 || ble_hs_id_infer_auto(0, &g_ble_addr_type) != 0) {
        g_status.ble_provisioning = PJ_BOARD_SERVICE_ERROR;
        (void)snprintf(g_ble_state, sizeof(g_ble_state), "address-error");
        return;
    }
    ble_advertise();
}

static void ble_host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    vTaskDelete(NULL);
}

static esp_err_t ble_provisioning_start(void)
{
    if (g_ble_started) {
        return ESP_OK;
    }
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        return err;
    }
    ble_svc_gap_init();
    ble_svc_gatt_init();
    char name[20];
    (void)snprintf(name, sizeof(name), "PJ-%.6s", g_status.device_id + 3);
    for (char *cursor = name; *cursor != '\0'; cursor++) {
        *cursor = (char)toupper((unsigned char)*cursor);
    }
    if (ble_svc_gap_device_name_set(name) != 0 ||
        ble_gatts_count_cfg(g_ble_services) != 0 ||
        ble_gatts_add_svcs(g_ble_services) != 0) {
        return ESP_FAIL;
    }
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_sc_only = 1;
    if (xTaskCreate(ble_host_task, "pj-nimble", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    g_ble_started = 1;
    g_status.ble_provisioning = PJ_BOARD_SERVICE_READY;
    return ESP_OK;
}

static esp_err_t epd_write_bytes(const uint8_t *data, int len)
{
    spi_transaction_t transaction = {
        .length = (size_t)len * 8u,
        .tx_buffer = data,
    };
    gpio_set_level(EPD_DC_PIN, 1);
    gpio_set_level(EPD_CS_PIN, 0);
    esp_err_t err = spi_device_polling_transmit(g_epd_spi, &transaction);
    gpio_set_level(EPD_CS_PIN, 1);
    return err;
}

static esp_err_t epd_send_command(uint8_t command)
{
    gpio_set_level(EPD_DC_PIN, 0);
    gpio_set_level(EPD_CS_PIN, 0);
    esp_err_t err = epd_spi_byte(command);
    gpio_set_level(EPD_CS_PIN, 1);
    return err;
}

static esp_err_t epd_send_data(uint8_t data)
{
    gpio_set_level(EPD_DC_PIN, 1);
    gpio_set_level(EPD_CS_PIN, 0);
    esp_err_t err = epd_spi_byte(data);
    gpio_set_level(EPD_CS_PIN, 1);
    return err;
}

static void epd_wait_busy(void)
{
    int guard = 0;
    while (gpio_get_level(EPD_BUSY_PIN) == 1 && guard < 2000) {
        vTaskDelay(pdMS_TO_TICKS(5));
        guard++;
    }
}

static void epd_set_windows(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x44));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data((x_start >> 3) & 0xFF));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data((x_end >> 3) & 0xFF));

    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x45));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(y_start & 0xFF));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data((y_start >> 8) & 0xFF));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(y_end & 0xFF));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data((y_end >> 8) & 0xFF));
}

static void epd_set_cursor(uint16_t x_start, uint16_t y_start)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x4E));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(x_start & 0xFF));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x4F));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(y_start & 0xFF));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data((y_start >> 8) & 0xFF));
    epd_wait_busy();
}

static void epd_set_lut(const uint8_t *lut)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x32));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_write_bytes(lut, 153));
    epd_wait_busy();

    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x3F));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(lut[153]));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x03));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(lut[154]));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x04));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(lut[155]));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(lut[156]));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(lut[157]));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x2C));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(lut[158]));
}

static void epd_turn_on_display(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x22));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0xC7));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x20));
    epd_wait_busy();
}

static void epd_turn_on_display_part(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x22));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0xCF));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x20));
    epd_wait_busy();
}

static void epd_prepare_partial(void)
{
    if (g_epd_partial_ready) {
        return;
    }

    ESP_LOGI(TAG, "Display partial mode init");
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x11));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x01));
    epd_set_lut(WF_PARTIAL_1IN54);
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x37));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x00));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x00));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x00));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x00));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x00));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x40));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x00));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x00));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x00));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x00));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x3C));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x80));
    g_epd_partial_ready = 1;
}

static esp_err_t display_init(void)
{
    g_epd_shadow_valid = 0;
    g_epd_partial_ready = 0;

    gpio_config_t output_conf = {
        .pin_bit_mask = (1ULL << EPD_RST_PIN) | (1ULL << EPD_DC_PIN) | (1ULL << EPD_CS_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&output_conf), TAG, "display gpio output config failed");

    gpio_config_t input_conf = {
        .pin_bit_mask = (1ULL << EPD_BUSY_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&input_conf), TAG, "display busy config failed");

    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = EPD_MOSI_PIN,
        .sclk_io_num = EPD_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = PJ_FRAMEBUFFER_BYTES,
    };
    spi_device_interface_config_t devcfg = {
        .spics_io_num = -1,
        .clock_speed_hz = PJ_EPD_SPI_CLOCK_HZ,
        .mode = 0,
        .queue_size = 4,
    };
    esp_err_t err = spi_bus_initialize(EPD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "display spi bus init failed");
    }
    ESP_RETURN_ON_ERROR(spi_bus_add_device(EPD_SPI_NUM, &devcfg, &g_epd_spi), TAG, "display spi add device failed");

    gpio_set_level(EPD_CS_PIN, 1);
    gpio_set_level(EPD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(EPD_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(EPD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    epd_wait_busy();
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x12));
    epd_wait_busy();
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x01));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0xC7));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x00));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x01));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x11));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x01));
    epd_set_windows(0, PJ_DISPLAY_WIDTH - 1, PJ_DISPLAY_HEIGHT - 1, 0);
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x3C));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x01));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x18));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x80));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x22));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0xB1));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x20));
    epd_set_cursor(0, PJ_DISPLAY_HEIGHT - 1);
    epd_wait_busy();
    epd_set_lut(WF_FULL_1IN54);

    g_display_ready = 1;
    return ESP_OK;
}

static void framebuffer_to_epd(const pj_framebuffer_t *fb)
{
    memset(g_epd_buffer, 0xFF, sizeof(g_epd_buffer));
    for (int y = 0; y < PJ_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < PJ_DISPLAY_WIDTH; x++) {
            if (pj_framebuffer_get(fb, x, y)) {
                size_t index = (size_t)y * (PJ_DISPLAY_WIDTH / 8) + (size_t)(x >> 3);
                g_epd_buffer[index] &= (uint8_t)~(1u << (7 - (x & 7)));
            }
        }
    }
}

static int framebuffer_region_to_epd(const pj_framebuffer_t *fb, const pj_ui_dirty_region_t *dirty,
                                     int *x0_out, int *y0_out, int *x1_out, int *y1_out)
{
    int x0 = dirty->x & ~7;
    int y0 = dirty->y;
    int x1 = dirty->x + dirty->width - 1;
    int y1 = dirty->y + dirty->height - 1;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 >= PJ_DISPLAY_WIDTH) {
        x1 = PJ_DISPLAY_WIDTH - 1;
    }
    if (y1 >= PJ_DISPLAY_HEIGHT) {
        y1 = PJ_DISPLAY_HEIGHT - 1;
    }
    x1 |= 7;
    if (x1 >= PJ_DISPLAY_WIDTH) {
        x1 = PJ_DISPLAY_WIDTH - 1;
    }
    if (x1 < x0 || y1 < y0) {
        return 0;
    }

    int bytes_per_row = ((x1 - x0 + 1) + 7) / 8;
    int height = y1 - y0 + 1;
    memset(g_epd_buffer, 0xFF, (size_t)bytes_per_row * (size_t)height);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (pj_framebuffer_get(fb, x, y)) {
                size_t row = (size_t)(y - y0);
                size_t col = (size_t)((x - x0) >> 3);
                g_epd_buffer[row * (size_t)bytes_per_row + col] &= (uint8_t)~(1u << (7 - ((x - x0) & 7)));
            }
        }
    }

    *x0_out = x0;
    *y0_out = y0;
    *x1_out = x1;
    *y1_out = y1;
    return bytes_per_row * height;
}

static int storage_refresh_capacity(void)
{
    if (!g_status.storage_mounted) {
        g_status.storage_total_bytes = 0;
        g_status.storage_free_bytes = 0;
        g_status.storage_health = PJ_STORAGE_HEALTH_UNMOUNTED;
        return 0;
    }
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    esp_err_t err = esp_vfs_fat_info(g_status.storage_path, &total_bytes, &free_bytes);
    if (err != ESP_OK) {
        g_status.storage_health = PJ_STORAGE_HEALTH_IO_ERROR;
        g_status.storage = PJ_BOARD_SERVICE_ERROR;
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "microSD capacity check failed: %s; use storage recovery",
                       esp_err_to_name(err));
        return 0;
    }
    g_status.storage_total_bytes = total_bytes;
    g_status.storage_free_bytes = free_bytes;
    g_status.storage_health = pj_storage_capacity_health(1, 1, total_bytes, free_bytes,
                                                         PJ_STORAGE_RESERVE_BYTES);
    g_status.storage = PJ_BOARD_SERVICE_READY;
    return 1;
}

static int storage_preflight(uint64_t write_bytes, const char *operation)
{
    if (!storage_refresh_capacity()) {
        return 0;
    }
    if (!pj_storage_can_write(g_status.storage_free_bytes, write_bytes,
                              PJ_STORAGE_RESERVE_BYTES)) {
        g_status.storage_health = PJ_STORAGE_HEALTH_FULL;
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "microSD has insufficient free space for %s; delete or sync notes",
                       operation);
        ESP_LOGW(TAG, "%s (free=%llu reserve=%llu requested=%llu)", g_status.last_error,
                 (unsigned long long)g_status.storage_free_bytes,
                 (unsigned long long)PJ_STORAGE_RESERVE_BYTES,
                 (unsigned long long)write_bytes);
        return 0;
    }
    return 1;
}

static int ensure_storage_directory(const char *path)
{
    return mkdir(path, 0775) == 0 || errno == EEXIST;
}

static esp_err_t storage_init(void)
{
    g_status.storage_mounted = 0;
    g_status.storage_total_bytes = 0;
    g_status.storage_free_bytes = 0;
    g_status.storage_health = PJ_STORAGE_HEALTH_UNMOUNTED;
    gpio_set_pull_mode(SD_MISO_D0_PIN, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(SD_MOSI_CMD_PIN, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(SD_CLK_PIN, GPIO_PULLUP_ONLY);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 2 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = SD_CLK_PIN;
    slot_config.cmd = SD_MOSI_CMD_PIN;
    slot_config.d0 = SD_MISO_D0_PIN;

    esp_err_t err = esp_vfs_fat_sdmmc_mount(g_status.storage_path, &host, &slot_config, &mount_config, &g_sd_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "microSD mount at %dkHz failed: %s; retrying at identification frequency",
                 host.max_freq_khz, esp_err_to_name(err));
        host.max_freq_khz = SDMMC_FREQ_PROBING;
        g_sd_card = NULL;
        err = esp_vfs_fat_sdmmc_mount(g_status.storage_path, &host, &slot_config, &mount_config, &g_sd_card);
    }
    if (err == ESP_OK && g_sd_card == NULL) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        g_status.storage_mounted = 1;
        sdmmc_card_print_info(stdout, g_sd_card);
        if (!ensure_storage_directory("/sdcard/pj") ||
            !ensure_storage_directory(PJ_AUDIO_DIR) ||
            !ensure_storage_directory(PJ_TRANSCRIPT_DIR) ||
            !ensure_storage_directory(PJ_NOTE_DIR)) {
            err = ESP_FAIL;
        } else if (!storage_refresh_capacity()) {
            err = ESP_FAIL;
        } else {
            int recovered = cleanup_storage_artifacts();
            if (recovered > 0) {
                g_status.storage_recovery_count += (unsigned)recovered;
                ESP_LOGW(TAG, "Recovered %d interrupted storage artifact(s)", recovered);
                (void)storage_refresh_capacity();
            }
        }
    }
    if (err != ESP_OK) {
        if (g_sd_card != NULL) {
            esp_err_t unmount_err = esp_vfs_fat_sdcard_unmount(g_status.storage_path, g_sd_card);
            if (unmount_err != ESP_OK) {
                ESP_LOGW(TAG, "microSD cleanup unmount failed after initialization error: %s",
                         esp_err_to_name(unmount_err));
            }
            g_sd_card = NULL;
        }
        g_status.storage_mounted = 0;
        g_status.storage_total_bytes = 0;
        g_status.storage_free_bytes = 0;
        g_status.storage_health = PJ_STORAGE_HEALTH_IO_ERROR;
    }
    return err;
}

static int is_audio_filename(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL) {
        return 0;
    }
    return strcasecmp(dot, ".wav") == 0;
}

static int is_transcript_filename(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL) {
        return 0;
    }
    return strcasecmp(dot, ".json") == 0;
}

static void label_from_filename(char *out, size_t out_size, const char *filename)
{
    if (out_size == 0) {
        return;
    }
    size_t used = 0;
    for (const char *cursor = filename; *cursor != '\0' && *cursor != '.' && used + 1 < out_size; cursor++) {
        char ch = *cursor == '_' || *cursor == '-' ? ' ' : *cursor;
        out[used++] = (char)toupper((unsigned char)ch);
    }
    out[used] = '\0';
}

typedef struct {
    char filename[96];
    char label[PJ_UI_NOTE_LABEL_LEN];
    char transcript_label[PJ_UI_NOTE_LABEL_LEN];
    long size_bytes;
    uint32_t data_bytes;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits;
    pj_note_metadata_t note;
} pj_audio_entry_t;

static void note_sidecar_path(char *out, size_t out_size, const char *filename)
{
    (void)snprintf(out, out_size, PJ_NOTE_DIR "/%s.json", filename);
}

static void transcript_path_for_audio(char *out, size_t out_size, const char *filename)
{
    (void)snprintf(out, out_size, PJ_TRANSCRIPT_DIR "/%s.json", filename);
}

static cJSON *json_read_file(const char *path, size_t max_bytes)
{
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= 0 || (size_t)st.st_size > max_bytes) {
        return NULL;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    char *body = malloc((size_t)st.st_size + 1U);
    if (body == NULL) {
        fclose(file);
        return NULL;
    }
    size_t read = fread(body, 1, (size_t)st.st_size, file);
    fclose(file);
    body[read] = '\0';
    cJSON *json = read == (size_t)st.st_size ? cJSON_Parse(body) : NULL;
    free(body);
    return json;
}

static esp_err_t json_write_file_atomic(const char *path, const char *body, size_t body_size)
{
    char temporary_path[256];
    char backup_path[256];
    if (path == NULL || body == NULL || body_size == 0 ||
        snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", path) >= (int)sizeof(temporary_path) ||
        snprintf(backup_path, sizeof(backup_path), "%s.bak", path) >= (int)sizeof(backup_path)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (g_status.storage != PJ_BOARD_SERVICE_READY) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!storage_preflight(body_size, "JSON update")) {
        return g_status.storage_health == PJ_STORAGE_HEALTH_FULL ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
    remove(temporary_path);
    FILE *file = fopen(temporary_path, "wb");
    if (file == NULL) {
        return ESP_FAIL;
    }
    int written = fwrite(body, 1, body_size, file) == body_size;
    int flushed = fflush(file) == 0 && fsync(fileno(file)) == 0;
    int closed = fclose(file) == 0;
    if (!written || !flushed || !closed) {
        remove(temporary_path);
        return ESP_FAIL;
    }
    struct stat existing;
    int had_existing = stat(path, &existing) == 0;
    remove(backup_path);
    if (had_existing && rename(path, backup_path) != 0) {
        remove(temporary_path);
        return ESP_FAIL;
    }
    if (rename(temporary_path, path) != 0) {
        if (had_existing) {
            (void)rename(backup_path, path);
        }
        remove(temporary_path);
        return ESP_FAIL;
    }
    if (had_existing) {
        remove(backup_path);
    }
    (void)storage_refresh_capacity();
    return ESP_OK;
}

static int note_metadata_write(const pj_note_metadata_t *note)
{
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return 0;
    }
    cJSON_AddStringToObject(json, "filename", note->filename);
    cJSON_AddStringToObject(json, "created_at", note->created_at);
    cJSON_AddNumberToObject(json, "duration_ms", note->duration_ms);
    cJSON_AddBoolToObject(json, "synced", note->synced != 0);
    cJSON_AddStringToObject(json, "transcript_path", note->transcript_path);
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (body == NULL) {
        return 0;
    }
    char path[256];
    note_sidecar_path(path, sizeof(path), note->filename);
    int written = json_write_file_atomic(path, body, strlen(body)) == ESP_OK;
    cJSON_free(body);
    return written;
}

static int note_metadata_load(pj_note_metadata_t *note)
{
    char path[256];
    note_sidecar_path(path, sizeof(path), note->filename);
    cJSON *json = json_read_file(path, 2048);
    if (json == NULL) {
        return 0;
    }
    cJSON *created_at = cJSON_GetObjectItemCaseSensitive(json, "created_at");
    cJSON *duration_ms = cJSON_GetObjectItemCaseSensitive(json, "duration_ms");
    cJSON *synced = cJSON_GetObjectItemCaseSensitive(json, "synced");
    cJSON *transcript_path = cJSON_GetObjectItemCaseSensitive(json, "transcript_path");
    if (cJSON_IsString(created_at) && created_at->valuestring != NULL) {
        (void)snprintf(note->created_at, sizeof(note->created_at), "%s", created_at->valuestring);
    }
    if (cJSON_IsNumber(duration_ms) && duration_ms->valuedouble >= 0) {
        note->duration_ms = (uint32_t)duration_ms->valuedouble;
    }
    note->synced = cJSON_IsTrue(synced);
    if (cJSON_IsString(transcript_path) && transcript_path->valuestring != NULL) {
        (void)snprintf(note->transcript_path, sizeof(note->transcript_path), "%s",
                       transcript_path->valuestring);
    }
    cJSON_Delete(json);
    return 1;
}

static int transcript_label_for_audio(const char *filename, char *label, size_t label_size,
                                      char *path, size_t path_size)
{
    transcript_path_for_audio(path, path_size, filename);
    cJSON *json = json_read_file(path, 8192);
    if (json == NULL) {
        return 0;
    }
    cJSON *text = cJSON_GetObjectItemCaseSensitive(json, "text");
    if (!cJSON_IsString(text) || text->valuestring == NULL || text->valuestring[0] == '\0') {
        cJSON_Delete(json);
        return 0;
    }
    size_t used = 0;
    int pending_space = 0;
    for (const char *cursor = text->valuestring; *cursor != '\0' && used + 1 < label_size; cursor++) {
        unsigned char ch = (unsigned char)*cursor;
        if (isspace(ch)) {
            pending_space = used > 0;
            continue;
        }
        if (pending_space && used + 1 < label_size) {
            label[used++] = ' ';
        }
        label[used++] = (char)ch;
        pending_space = 0;
    }
    label[used] = '\0';
    cJSON_Delete(json);
    return used > 0;
}

static void write_recording_metadata(const char *final_path, uint32_t data_bytes)
{
    const char *filename = strrchr(final_path, '/');
    filename = filename == NULL ? final_path : filename + 1;
    pj_note_metadata_t note;
    if (!pj_note_metadata_from_audio(&note, filename, data_bytes, PJ_AUDIO_SAMPLE_RATE,
                                     PJ_AUDIO_CHANNELS, PJ_AUDIO_BITS_PER_SAMPLE) ||
        !note_metadata_write(&note)) {
        ESP_LOGW(TAG, "Recording retained without metadata sidecar: %s", final_path);
    }
}

static int recording_file_valid(const char *path, uint32_t expected_data_bytes)
{
    struct stat st;
    uint8_t header[44];
    if (path == NULL || stat(path, &st) != 0 || st.st_size < (off_t)sizeof(header)) {
        return 0;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    size_t read = fread(header, 1, sizeof(header), file);
    int close_ok = fclose(file) == 0;
    pj_storage_wav_info_t wav;
    return close_ok && read == sizeof(header) &&
           pj_storage_wav_validate(header, sizeof(header), (uint64_t)st.st_size,
                                   PJ_AUDIO_SAMPLE_RATE, PJ_AUDIO_BITS_PER_SAMPLE,
                                   &wav) &&
           wav.channels == PJ_AUDIO_CHANNELS &&
           wav.data_bytes == expected_data_bytes;
}

static int recording_publish_file(const char *temporary_path, const char *final_path,
                                  uint32_t data_bytes)
{
    if (!recording_file_valid(temporary_path, data_bytes)) {
        ESP_LOGE(TAG, "Recording rejected before publish: %s", temporary_path);
        remove(temporary_path);
        return 0;
    }
    if (rename(temporary_path, final_path) != 0) {
        ESP_LOGE(TAG, "Recording publish rename failed: %s -> %s errno=%d",
                 temporary_path, final_path, errno);
        remove(temporary_path);
        return 0;
    }
    if (!recording_file_valid(final_path, data_bytes)) {
        ESP_LOGE(TAG, "Recording rejected after publish: %s", final_path);
        remove(final_path);
        return 0;
    }
    write_recording_metadata(final_path, data_bytes);
    return 1;
}

static int probe_audio_entry(const char *filename, pj_audio_entry_t *entry)
{
    char path[160];
    struct stat st;
    uint8_t header[44];
    (void)snprintf(path, sizeof(path), PJ_AUDIO_DIR "/%s", filename);
    if (stat(path, &st) != 0 || st.st_size < (off_t)sizeof(header)) {
        return 0;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    size_t read = fread(header, 1, sizeof(header), file);
    fclose(file);
    pj_storage_wav_info_t wav;
    if (read != sizeof(header) ||
        !pj_storage_wav_validate(header, sizeof(header), (uint64_t)st.st_size,
                                 PJ_AUDIO_SAMPLE_RATE, PJ_AUDIO_BITS_PER_SAMPLE, &wav)) {
        ESP_LOGW(TAG, "Ignoring invalid or interrupted WAV: %s size=%ld", filename,
                 (long)st.st_size);
        return 0;
    }
    memset(entry, 0, sizeof(*entry));
    (void)snprintf(entry->filename, sizeof(entry->filename), "%s", filename);
    label_from_filename(entry->label, sizeof(entry->label), filename);
    entry->size_bytes = (long)st.st_size;
    entry->data_bytes = wav.data_bytes;
    entry->sample_rate = wav.sample_rate;
    entry->channels = wav.channels;
    entry->bits = wav.bits_per_sample;
    if (!pj_note_metadata_from_audio(&entry->note, filename, wav.data_bytes, wav.sample_rate,
                                     wav.channels, wav.bits_per_sample)) {
        return 0;
    }
    pj_note_metadata_t derived = entry->note;
    int metadata_loaded = note_metadata_load(&entry->note);
    pj_note_metadata_t loaded = entry->note;
    (void)snprintf(entry->note.filename, sizeof(entry->note.filename), "%s", derived.filename);
    entry->note.duration_ms = derived.duration_ms;
    if (entry->note.created_at[0] == '\0') {
        (void)snprintf(entry->note.created_at, sizeof(entry->note.created_at), "%s", derived.created_at);
    }
    char transcript_path[PJ_NOTE_TRANSCRIPT_PATH_LEN];
    if (transcript_label_for_audio(filename, entry->transcript_label,
                                   sizeof(entry->transcript_label), transcript_path,
                                   sizeof(transcript_path))) {
        entry->note.synced = 1;
        (void)snprintf(entry->note.transcript_path, sizeof(entry->note.transcript_path), "%s",
                       transcript_path);
    } else {
        entry->note.synced = 0;
        entry->note.transcript_path[0] = '\0';
    }
    if ((!metadata_loaded || memcmp(&loaded, &entry->note, sizeof(entry->note)) != 0) &&
        !note_metadata_write(&entry->note)) {
        ESP_LOGW(TAG, "Note metadata write failed: %s", filename);
    }
    return 1;
}

static int audio_entry_compare_newest_first(const void *left, const void *right)
{
    const pj_audio_entry_t *a = (const pj_audio_entry_t *)left;
    const pj_audio_entry_t *b = (const pj_audio_entry_t *)right;
    return strcmp(b->filename, a->filename);
}

static int collect_audio_entries(pj_audio_entry_t *entries, int capacity)
{
    if (entries == NULL || capacity <= 0) {
        return 0;
    }
    DIR *dir = opendir(PJ_AUDIO_DIR);
    if (dir == NULL) {
        return 0;
    }
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < capacity) {
        if (entry->d_name[0] == '.' || !is_audio_filename(entry->d_name)) {
            continue;
        }
        if (probe_audio_entry(entry->d_name, &entries[count])) {
            count++;
        }
    }
    closedir(dir);
    qsort(entries, (size_t)count, sizeof(entries[0]), audio_entry_compare_newest_first);
    return count;
}

static void collect_sync_counts(int *pending, int *transferred)
{
    *pending = 0;
    *transferred = 0;
    pj_audio_entry_t *entries = calloc(PJ_AUDIO_MAX_INDEXED_FILES, sizeof(entries[0]));
    if (entries == NULL) {
        return;
    }
    int count = collect_audio_entries(entries, PJ_AUDIO_MAX_INDEXED_FILES);
    for (int i = 0; i < count; i++) {
        if (entries[i].note.synced) {
            (*transferred)++;
        } else {
            (*pending)++;
        }
    }
    free(entries);
}

static void refresh_ui_sync_state(pj_ui_context_t *ui)
{
    int pending = 0;
    int transferred = 0;
    collect_sync_counts(&pending, &transferred);
    pj_ui_set_sync_state(ui, pending, transferred, g_status.wifi == PJ_BOARD_SERVICE_READY);
}

static int delete_dir_entries(const char *dir_path, int (*matches)(const char *name))
{
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        return 0;
    }
    int deleted = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' || !matches(entry->d_name)) {
            continue;
        }
        char path[384];
        size_t dir_len = strlen(dir_path);
        size_t name_len = strlen(entry->d_name);
        if (dir_len + 1 + name_len + 1 > sizeof(path)) {
            ESP_LOGW(TAG, "Delete path too long: %s/%s", dir_path, entry->d_name);
            continue;
        }
        memcpy(path, dir_path, dir_len);
        path[dir_len] = '/';
        memcpy(path + dir_len + 1, entry->d_name, name_len + 1);
        if (remove(path) == 0) {
            deleted++;
        } else {
            ESP_LOGW(TAG, "Failed to delete %s", path);
        }
    }
    closedir(dir);
    return deleted;
}

static int recover_dir_artifacts(const char *dir_path)
{
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        return 0;
    }
    int recovered = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t name_len = strlen(entry->d_name);
        if (entry->d_name[0] == '.' || name_len < 5U) {
            continue;
        }
        char artifact_path[384];
        char target_path[384];
        int artifact_len = snprintf(artifact_path, sizeof(artifact_path), "%s/%s",
                                    dir_path, entry->d_name);
        if (artifact_len < 0 || artifact_len >= (int)sizeof(artifact_path)) {
            continue;
        }
        size_t suffix_len = 4U;
        if (name_len - suffix_len + strlen(dir_path) + 2U > sizeof(target_path)) {
            continue;
        }
        (void)snprintf(target_path, sizeof(target_path), "%s/%.*s", dir_path,
                       (int)(name_len - suffix_len), entry->d_name);
        struct stat target_stat;
        int target_exists = stat(target_path, &target_stat) == 0;
        pj_storage_recovery_action_t action =
            pj_storage_recovery_action(entry->d_name, target_exists);
        if ((action == PJ_STORAGE_RECOVERY_DELETE_TEMP ||
             action == PJ_STORAGE_RECOVERY_DELETE_BACKUP) && remove(artifact_path) == 0) {
            recovered++;
        } else if (action == PJ_STORAGE_RECOVERY_RESTORE_BACKUP &&
                   rename(artifact_path, target_path) == 0) {
            recovered++;
        }
    }
    closedir(dir);
    return recovered;
}

static int cleanup_storage_artifacts(void)
{
    return recover_dir_artifacts(PJ_AUDIO_DIR) +
           recover_dir_artifacts(PJ_TRANSCRIPT_DIR) +
           recover_dir_artifacts(PJ_NOTE_DIR);
}

static int audio_filename_for_index(int target_index, char *out, size_t out_size)
{
    pj_audio_entry_t *entries = calloc(PJ_AUDIO_MAX_INDEXED_FILES, sizeof(entries[0]));
    if (entries == NULL) {
        return 0;
    }
    int count = collect_audio_entries(entries, PJ_AUDIO_MAX_INDEXED_FILES);
    int audio_index = target_index;
    if (target_index >= 0 && target_index < g_ui_note_audio_count) {
        audio_index = g_ui_note_audio_indices[target_index];
    }
    if (audio_index < 0 || audio_index >= count) {
        free(entries);
        return 0;
    }
    (void)snprintf(out, out_size, "%s", entries[audio_index].filename);
    ESP_LOGI(TAG, "Playback UI index %d resolved to audio index %d: %s data_bytes=%u size=%ld",
             target_index, audio_index, entries[audio_index].filename,
             (unsigned)entries[audio_index].data_bytes, entries[audio_index].size_bytes);
    free(entries);
    return 1;
}

static void next_recording_path(char *out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }
    for (int attempt = 0; attempt < 1000; attempt++) {
        uint32_t sequence = g_record_sequence++;
        (void)snprintf(out, out_size,
                       PJ_AUDIO_DIR "/rec-%04d%02d%02d-%02d%02d-%03u.wav",
                       g_status.year, g_status.month, g_status.day,
                       g_status.hour, g_status.minute,
                       (unsigned)(sequence % 1000u));
        struct stat st;
        if (stat(out, &st) != 0) {
            return;
        }
    }
    (void)snprintf(out, out_size,
                   PJ_AUDIO_DIR "/rec-%04d%02d%02d-%02d%02d-%lu.wav",
                   g_status.year, g_status.month, g_status.day,
                   g_status.hour, g_status.minute,
                   (unsigned long)xTaskGetTickCount());
}

static void refresh_ui_notes_from_sd(pj_ui_context_t *ui)
{
    char labels[PJ_UI_MAX_NOTES][PJ_UI_NOTE_LABEL_LEN];
    pj_audio_entry_t *entries = calloc(PJ_AUDIO_MAX_INDEXED_FILES, sizeof(entries[0]));
    memset(labels, 0, sizeof(labels));
    if (entries == NULL) {
        g_ui_note_audio_count = 0;
        pj_ui_set_notes(ui, 0, labels);
        return;
    }
    int count = collect_audio_entries(entries, PJ_AUDIO_MAX_INDEXED_FILES);
    pj_ui_state_t state = pj_ui_current_state(ui);
    if (state == PJ_UI_STATE_READ) {
        g_ui_note_transcript_view = 1;
    } else if (state == PJ_UI_STATE_LISTEN) {
        g_ui_note_transcript_view = 0;
    }
    int transcript_view = g_ui_note_transcript_view;
    int displayed = 0;
    for (int i = 0; i < count && displayed < PJ_UI_MAX_NOTES; i++) {
        if (transcript_view && (!entries[i].note.synced || entries[i].transcript_label[0] == '\0')) {
            continue;
        }
        const char *label = transcript_view ? entries[i].transcript_label : entries[i].label;
        (void)snprintf(labels[displayed], sizeof(labels[displayed]), "%s", label);
        g_ui_note_audio_indices[displayed] = i;
        displayed++;
    }
    g_ui_note_audio_count = displayed;
    free(entries);
    ESP_LOGI(TAG, "%s note list refreshed: visible=%d playable=%d",
             transcript_view ? "Transcript" : "Audio", displayed, count);
    pj_ui_set_notes(ui, displayed, labels);
}

static int i2c_take(TickType_t timeout)
{
    return g_i2c_lock == NULL || xSemaphoreTake(g_i2c_lock, timeout) == pdTRUE;
}

static void i2c_give(void)
{
    if (g_i2c_lock != NULL) {
        xSemaphoreGive(g_i2c_lock);
    }
}

static esp_err_t i2c_init(void)
{
    if (g_i2c_lock == NULL) {
        g_i2c_lock = xSemaphoreCreateMutex();
        if (g_i2c_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = ESP32_I2C_DEV_NUM,
        .scl_io_num = ESP32_I2C_SCL_PIN,
        .sda_io_num = ESP32_I2C_SDA_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &g_i2c_bus);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    g_i2c_ready = 1;
    return ESP_OK;
}

static esp_err_t i2c_add_device(uint8_t address, i2c_master_dev_handle_t *handle)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(g_i2c_bus, &dev_cfg, handle);
}

static esp_err_t touch_init(void)
{
    gpio_config_t rst_conf = {
        .pin_bit_mask = (1ULL << EPD_TP_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&rst_conf), TAG, "touch reset gpio config failed");
    gpio_config_t int_conf = {
        .pin_bit_mask = (1ULL << EPD_TP_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int_conf), TAG, "touch int gpio config failed");
    gpio_set_level(EPD_TP_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(EPD_TP_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(EPD_TP_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(i2c_add_device(I2C_FT6336_DEV_ADDRESS, &g_touch_dev), TAG, "touch i2c add failed");
    g_touch_ready = 1;
    return ESP_OK;
}

static esp_err_t shtc3_write_cmd(uint16_t command)
{
    uint8_t data[2] = {(uint8_t)(command >> 8), (uint8_t)(command & 0xFF)};
    return i2c_master_transmit(g_shtc3_dev, data, sizeof(data), 1000);
}

static uint8_t shtc3_crc(const uint8_t *data, int len)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t shtc3_init(void)
{
    ESP_RETURN_ON_ERROR(i2c_add_device(I2C_SHTC3_DEV_ADDRESS, &g_shtc3_dev), TAG, "shtc3 i2c add failed");
    ESP_RETURN_ON_ERROR(shtc3_write_cmd(SHTC3_CMD_WAKEUP), TAG, "shtc3 wakeup failed");
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_RETURN_ON_ERROR(shtc3_write_cmd(SHTC3_CMD_SOFT_RESET), TAG, "shtc3 reset failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    return shtc3_write_cmd(SHTC3_CMD_SLEEP);
}

static void shtc3_refresh_status(void)
{
    if (g_shtc3_dev == NULL) {
        return;
    }
    if (!i2c_take(pdMS_TO_TICKS(100))) {
        return;
    }
    uint8_t bytes[6] = {0};
    int humidity_valid = 0;
    int awake = shtc3_write_cmd(SHTC3_CMD_WAKEUP) == ESP_OK;
    if (awake) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (awake && shtc3_write_cmd(SHTC3_CMD_MEAS_T_RH_POLLING) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (i2c_master_receive(g_shtc3_dev, bytes, sizeof(bytes), 1000) == ESP_OK) {
            if (shtc3_crc(bytes, 2) == bytes[2]) {
                uint16_t raw_temp = ((uint16_t)bytes[0] << 8) | bytes[1];
                float temp = 175.0f * (float)raw_temp / 65536.0f - 45.0f - 4.0f;
                g_status.temperature_c = (int)(temp + (temp >= 0 ? 0.5f : -0.5f));
            }
            if (shtc3_crc(bytes + 3, 2) == bytes[5]) {
                uint16_t raw_humidity = ((uint16_t)bytes[3] << 8) | bytes[4];
                int humidity = (int)(100.0f * (float)raw_humidity / 65536.0f + 0.5f);
                g_status.humidity_percent = humidity < 0 ? 0 : humidity > 100 ? 100 : humidity;
                humidity_valid = 1;
            }
        }
    }
    if (!humidity_valid) {
        g_status.humidity_percent = -1;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(shtc3_write_cmd(SHTC3_CMD_SLEEP));
    i2c_give();
}

static void button_init(void)
{
    gpio_config_t button_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_PIN) | (1ULL << PWR_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&button_conf));
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    pj_aux_input_init(&g_aux_input, gpio_get_level(BOOT_BUTTON_PIN), now_ms);
}

static int touch_read(uint16_t *x, uint16_t *y)
{
    if (!g_touch_ready || g_touch_dev == NULL) {
        return 0;
    }
    if (!i2c_take(pdMS_TO_TICKS(2))) {
        return 0;
    }
    int active = 0;
    uint8_t reg = 0x02;
    uint8_t points = 0;
    if (i2c_master_transmit_receive(g_touch_dev, &reg, 1, &points, 1, 20) != ESP_OK ||
        (points & 0x0F) == 0 || (points & 0x0F) > 2) {
        goto done;
    }
    reg = 0x03;
    uint8_t buf[4] = {0};
    if (i2c_master_transmit_receive(g_touch_dev, &reg, 1, buf, sizeof(buf), 20) != ESP_OK) {
        goto done;
    }
    uint8_t event = buf[0] >> 6;
    if (event == 3) {
        goto done;
    }
    g_touch_raw_event = event;
    *x = ((((uint16_t)buf[0]) & 0x0F) << 8) | buf[1];
    *y = ((((uint16_t)buf[2]) & 0x0F) << 8) | buf[3];
    if (*x >= PJ_DISPLAY_WIDTH) {
        *x = PJ_DISPLAY_WIDTH - 1;
    }
    if (*y >= PJ_DISPLAY_HEIGHT) {
        *y = PJ_DISPLAY_HEIGHT - 1;
    }
    active = 1;
done:
    i2c_give();
    return active;
}

static int touch_poll_filtered_event(pj_board_event_t *event, TickType_t now)
{
    uint16_t x = 0;
    uint16_t y = 0;
    int touch_active = touch_read(&x, &y);
    if (touch_active) {
        if (g_touch_candidate_samples == 0 ||
            abs((int)x - (int)g_touch_candidate_x) > PJ_TOUCH_MOVE_TOLERANCE ||
            abs((int)y - (int)g_touch_candidate_y) > PJ_TOUCH_MOVE_TOLERANCE) {
            g_touch_candidate_samples = 1;
            g_touch_candidate_x = x;
            g_touch_candidate_y = y;
        } else {
            if (g_touch_candidate_samples < PJ_TOUCH_STABLE_SAMPLES) {
                g_touch_candidate_samples++;
            }
            g_touch_candidate_x = x;
            g_touch_candidate_y = y;
        }
        if (!g_touch_pressed && g_touch_candidate_samples >= PJ_TOUCH_STABLE_SAMPLES) {
            uint32_t since_last_ms = (uint32_t)((now - g_touch_last_event_tick) * portTICK_PERIOD_MS);
            if (since_last_ms >= PJ_TOUCH_EVENT_GUARD_MS) {
                g_touch_pressed = 1;
                g_touch_press_x = x;
                g_touch_press_y = y;
                g_touch_last_event_tick = now;
                event->type = PJ_BOARD_EVENT_TOUCH_TAP;
                event->x = (int)g_touch_press_x;
                event->y = (int)g_touch_press_y;
                g_display_warning_logged = 1;
                ESP_LOGI(TAG, "Touch tap x=%u y=%u stable=%d", (unsigned)event->x, (unsigned)event->y,
                         g_touch_candidate_samples);
                return 1;
            }
        } else if (g_touch_pressed) {
            g_touch_press_x = x;
            g_touch_press_y = y;
        }
    } else if (g_touch_pressed) {
        g_touch_pressed = 0;
        g_touch_candidate_samples = 0;
    } else {
        g_touch_candidate_samples = 0;
    }
    return 0;
}

static void touch_queue_event(const pj_board_event_t *event)
{
    if (g_board_event_queue == NULL) {
        return;
    }
    if (xQueueSend(g_board_event_queue, event, 0) != pdTRUE) {
        pj_board_event_t dropped;
        (void)xQueueReceive(g_board_event_queue, &dropped, 0);
        (void)xQueueSend(g_board_event_queue, event, 0);
    }
}

static void touch_poll_task(void *arg)
{
    (void)arg;
    while (1) {
        pj_board_event_t event = {
            .type = PJ_BOARD_EVENT_NONE,
            .x = 0,
            .y = 0,
        };
        if (touch_poll_filtered_event(&event, xTaskGetTickCount())) {
            touch_queue_event(&event);
        }
        vTaskDelay(pdMS_TO_TICKS(PJ_TOUCH_POLL_MS));
    }
}

static esp_err_t touch_task_start(void)
{
    if (!g_touch_ready || g_touch_task_started) {
        return ESP_OK;
    }
    if (g_board_event_queue == NULL) {
        g_board_event_queue = xQueueCreate(PJ_TOUCH_EVENT_QUEUE_DEPTH, sizeof(pj_board_event_t));
        if (g_board_event_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    BaseType_t created = xTaskCreate(touch_poll_task, "pj-touch", 3072, NULL, 6, NULL);
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    g_touch_task_started = 1;
    ESP_LOGI(TAG, "FT6336 touch polling at %dms", PJ_TOUCH_POLL_MS);
    return ESP_OK;
}

static uint8_t bcd_to_u8(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10u) + (value & 0x0Fu));
}

static uint8_t u8_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10u) << 4) | (value % 10u));
}

static int board_weekday_from_date(int year, int month, int day)
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

static esp_err_t battery_adc_init(void)
{
    if (g_adc_ready) {
        return ESP_OK;
    }
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&init_config, &g_adc1_handle), TAG, "battery adc unit init failed");

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(g_adc1_handle, VBAT_ADC_CHANNEL, &channel_config),
                        TAG, "battery adc channel config failed");

    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = VBAT_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_config, &g_adc_cali_handle) == ESP_OK) {
        g_adc_cali_ready = 1;
    } else {
        ESP_LOGW(TAG, "battery adc calibration unavailable; using raw estimate");
    }
    g_adc_ready = 1;
    return ESP_OK;
}

static void battery_refresh_status(void)
{
    if (!g_adc_ready || g_adc1_handle == NULL) {
        return;
    }
    int raw = 0;
    if (adc_oneshot_read(g_adc1_handle, VBAT_ADC_CHANNEL, &raw) != ESP_OK) {
        return;
    }
    int mv = (raw * 3300) / 4095;
    if (g_adc_cali_ready) {
        (void)adc_cali_raw_to_voltage(g_adc_cali_handle, raw, &mv);
    }
    float volts = 0.001f * (float)mv * 2.0f;
    int percent = 0;
    if (volts >= 4.12f) {
        percent = 100;
    } else if (volts > 3.0f) {
        percent = (int)(((volts - 3.0f) / (4.12f - 3.0f)) * 100.0f + 0.5f);
    }
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }
    g_status.battery_percent = percent;
}

static esp_err_t rtc_init(void)
{
    if (g_rtc_ready) {
        return ESP_OK;
    }
    if (g_rtc_sequence_lock == NULL) {
        g_rtc_sequence_lock = xSemaphoreCreateMutex();
        if (g_rtc_sequence_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_RETURN_ON_ERROR(i2c_add_device(I2C_PCF85063_DEV_ADDRESS, &g_rtc_dev), TAG, "rtc i2c add failed");
    gpio_config_t interrupt = {
        .pin_bit_mask = 1ull << RTC_INT_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&interrupt), TAG, "rtc interrupt gpio failed");
    g_rtc_ready = 1;
    return ESP_OK;
}

static int rtc_sequence_take(TickType_t timeout)
{
    return g_rtc_sequence_lock != NULL &&
           xSemaphoreTake(g_rtc_sequence_lock, timeout) == pdTRUE;
}

static void rtc_sequence_give(void)
{
    if (g_rtc_sequence_lock != NULL) {
        xSemaphoreGive(g_rtc_sequence_lock);
    }
}

static int rtc_wake_read(void *context, uint8_t reg, uint8_t *data, size_t size)
{
    (void)context;
    if (!g_rtc_ready || g_rtc_dev == NULL || data == NULL || size == 0 ||
        !i2c_take(pdMS_TO_TICKS(50))) {
        return 0;
    }
    esp_err_t err = i2c_master_transmit_receive(g_rtc_dev, &reg, 1, data, size, 100);
    i2c_give();
    return err == ESP_OK;
}

static int rtc_wake_write(void *context, uint8_t reg, const uint8_t *data, size_t size)
{
    (void)context;
    if (!g_rtc_ready || g_rtc_dev == NULL || data == NULL || size == 0 || size > 7 ||
        !i2c_take(pdMS_TO_TICKS(50))) {
        return 0;
    }
    uint8_t message[8] = {reg};
    memcpy(message + 1, data, size);
    esp_err_t err = i2c_master_transmit(g_rtc_dev, message, size + 1, 100);
    i2c_give();
    return err == ESP_OK;
}

static pj_rtc_wake_io_t rtc_wake_io(void)
{
    return (pj_rtc_wake_io_t) {
        .read = rtc_wake_read,
        .write = rtc_wake_write,
        .persist = rtc_wake_plan_save,
    };
}

static uint64_t board_monotonic_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000u;
}

static board_time_snapshot_t board_time_snapshot(void)
{
    board_time_snapshot_t snapshot;
    portENTER_CRITICAL(&g_time_lock);
    snapshot = (board_time_snapshot_t) {
        .hour = g_status.hour,
        .minute = g_status.minute,
        .year = g_status.year,
        .month = g_status.month,
        .day = g_status.day,
        .time_set = g_status.time_set,
        .generation = g_time_generation,
    };
    portEXIT_CRITICAL(&g_time_lock);
    return snapshot;
}

static int board_time_publish(int hour, int minute, int year, int month, int day,
                              int second, uint64_t monotonic_ms, int trusted)
{
    pj_time_clock_anchor_t anchor;
    if (!pj_time_clock_anchor_set(&anchor, year, month, day, hour, minute, second,
                                  monotonic_ms)) {
        return 0;
    }
    portENTER_CRITICAL(&g_time_lock);
    g_status.hour = hour;
    g_status.minute = minute;
    g_status.year = year;
    g_status.month = month;
    g_status.day = day;
    g_status.time_set = 1;
    g_time_clock_anchor = anchor;
    g_time_wall_trusted = trusted != 0;
    g_time_generation++;
    portEXIT_CRITICAL(&g_time_lock);
    return 1;
}

static int board_time_model_clock_for_boot(uint32_t boot_id,
                                           pj_time_clock_t *clock)
{
    pj_time_clock_anchor_t anchor;
    portENTER_CRITICAL(&g_time_lock);
    anchor = g_time_clock_anchor;
    portEXIT_CRITICAL(&g_time_lock);
    return pj_time_clock_snapshot(&anchor, boot_id, board_monotonic_ms(), clock);
}

static int board_time_model_clock(pj_time_clock_t *clock)
{
    return board_time_model_clock_for_boot(g_time_controller.boot_id, clock);
}

static void board_time_mark_pending(void)
{
    portENTER_CRITICAL(&g_time_lock);
    g_time_update_pending = 1;
    portEXIT_CRITICAL(&g_time_lock);
}

static int board_time_take_pending(board_time_snapshot_t *snapshot)
{
    int pending = 0;
    portENTER_CRITICAL(&g_time_lock);
    if (g_time_update_pending && g_status.time_set) {
        *snapshot = (board_time_snapshot_t) {
            .hour = g_status.hour,
            .minute = g_status.minute,
            .year = g_status.year,
            .month = g_status.month,
            .day = g_status.day,
            .time_set = 1,
            .generation = g_time_generation,
        };
        g_time_update_pending = 0;
        pending = 1;
    }
    portEXIT_CRITICAL(&g_time_lock);
    return pending;
}

static int rtc_read_status_time(void)
{
    if (!g_rtc_ready || g_rtc_dev == NULL) {
        return 0;
    }
    if (!rtc_sequence_take(pdMS_TO_TICKS(100))) {
        return 0;
    }
    if (!i2c_take(pdMS_TO_TICKS(20))) {
        rtc_sequence_give();
        return 0;
    }
    uint8_t reg = 0x04;
    uint8_t data[7] = {0};
    esp_err_t err = i2c_master_transmit_receive(g_rtc_dev, &reg, 1, data, sizeof(data), 100);
    i2c_give();
    rtc_sequence_give();
    if (err != ESP_OK || (data[0] & 0x80) != 0) {
        return 0;
    }
    int second = bcd_to_u8(data[0] & 0x7F);
    int minute = bcd_to_u8(data[1] & 0x7F);
    int hour = bcd_to_u8(data[2] & 0x3F);
    int day = bcd_to_u8(data[3] & 0x3F);
    int month = bcd_to_u8(data[5] & 0x1F);
    int year = 2000 + bcd_to_u8(data[6]);
    if (!valid_time_date(hour, minute, year, month, day) ||
        second < 0 || second > 59) {
        return 0;
    }
    return board_time_publish(hour, minute, year, month, day, second,
                              board_monotonic_ms(), 1);
}

static esp_err_t rtc_write_civil_time(int hour, int minute, int year,
                                      int month, int day, int second)
{
    if (!g_rtc_ready || g_rtc_dev == NULL ||
        !valid_time_date(hour, minute, year, month, day) ||
        second < 0 || second > 59) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t data[8] = {
        0x04,
        u8_to_bcd((uint8_t)second),
        u8_to_bcd((uint8_t)minute),
        u8_to_bcd((uint8_t)hour),
        u8_to_bcd((uint8_t)day),
        u8_to_bcd((uint8_t)board_weekday_from_date(year, month, day)),
        u8_to_bcd((uint8_t)month),
        u8_to_bcd((uint8_t)(year - 2000)),
    };
    if (!rtc_sequence_take(pdMS_TO_TICKS(200))) {
        return ESP_ERR_TIMEOUT;
    }
    if (!i2c_take(pdMS_TO_TICKS(50))) {
        rtc_sequence_give();
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_transmit(g_rtc_dev, data, sizeof(data), 100);
    if (err == ESP_OK) {
        uint8_t ctrl[2] = {0x00, 0x00};
        err = i2c_master_transmit(g_rtc_dev, ctrl, sizeof(ctrl), 100);
    }
    i2c_give();
    rtc_sequence_give();
    return err;
}

static esp_err_t rtc_write_status_time(void)
{
    board_time_snapshot_t time = board_time_snapshot();
    if (!time.time_set) {
        return ESP_ERR_INVALID_STATE;
    }
    return rtc_write_civil_time(time.hour, time.minute, time.year,
                                time.month, time.day, 0);
}

static uint32_t time_controller_next_boot_id(void *context)
{
    (void)context;
    return esp_random();
}

static int time_controller_clock(void *context, uint32_t boot_id,
                                 pj_time_clock_t *clock)
{
    (void)context;
    return board_time_model_clock_for_boot(boot_id, clock);
}

static int time_controller_alarm_settings(
    void *context, pj_time_controller_alarm_settings_t *settings)
{
    (void)context;
    if (settings == NULL || !settings_take(portMAX_DELAY)) {
        return 0;
    }
    *settings = (pj_time_controller_alarm_settings_t) {
        .enabled = g_settings.alarm_enabled,
        .hour = g_settings.alarm_hour,
        .minute = g_settings.alarm_minute,
    };
    settings_give();
    return 1;
}

static int time_controller_wall_time_trusted(void *context)
{
    (void)context;
    int trusted;
    portENTER_CRITICAL(&g_time_lock);
    trusted = g_time_wall_trusted;
    portEXIT_CRITICAL(&g_time_lock);
    return trusted;
}

static pj_time_activity_t time_controller_activity(void *context)
{
    (void)context;
    return board_time_activity();
}

static pj_time_controller_wake_result_t time_controller_schedule_wake(
    void *context, const pj_time_wake_deadline_t *deadline)
{
    (void)context;
    if (!g_rtc_ready) {
        return PJ_TIME_CONTROLLER_WAKE_UNAVAILABLE;
    }
    if (!g_rtc_wake_restored) {
        return PJ_TIME_CONTROLLER_WAKE_UNAVAILABLE;
    }
    if (g_rtc_wake_metadata_blocked) {
        return PJ_TIME_CONTROLLER_WAKE_ERROR;
    }
    if (deadline == NULL) {
        return rtc_wake_disarm_board(NULL, 0) == PJ_RTC_WAKE_OK
            ? PJ_TIME_CONTROLLER_WAKE_OK
            : PJ_TIME_CONTROLLER_WAKE_ERROR;
    }
    return rtc_wake_sync() >= 0 ? PJ_TIME_CONTROLLER_WAKE_OK
                                : PJ_TIME_CONTROLLER_WAKE_ERROR;
}

static void time_controller_project_media(void *context,
                                          const pj_time_alert_t *alert,
                                          pj_time_conflict_action_t action)
{
    (void)context;
    if (alert != NULL && action == PJ_TIME_PREEMPT_PLAYBACK) {
        (void)pj_board_playback_set_active(0, 0);
    }
    alert_audio_project(alert, action);
}

static void time_controller_publish_status(
    void *context, const pj_time_controller_result_t *result)
{
    (void)context;
    if (result->diagnostic == g_time_last_diagnostic) {
        return;
    }
    g_time_last_diagnostic = result->diagnostic;
    const char *message = NULL;
    switch (result->diagnostic) {
    case PJ_TIME_CONTROLLER_DIAGNOSTIC_LOAD_CORRUPT:
        message = "time state was corrupt; defaults restored";
        break;
    case PJ_TIME_CONTROLLER_DIAGNOSTIC_LOAD_INCOMPATIBLE:
        message = "time state version unsupported; defaults restored";
        break;
    case PJ_TIME_CONTROLLER_DIAGNOSTIC_LOAD_IO_ERROR:
        message = "time state read failed; persistence left untouched";
        break;
    case PJ_TIME_CONTROLLER_DIAGNOSTIC_SAVE_TRANSIENT:
    case PJ_TIME_CONTROLLER_DIAGNOSTIC_SAVE_PERMANENT:
        message = "time state save failed; retry pending";
        break;
    case PJ_TIME_CONTROLLER_DIAGNOSTIC_WAKE_ERROR:
        message = "time alert wake scheduling failed";
        break;
    case PJ_TIME_CONTROLLER_DIAGNOSTIC_TIME_UNCERTAIN:
        message = "time recovery uncertain; open a time app and acknowledge TIME?";
        break;
    case PJ_TIME_CONTROLLER_DIAGNOSTIC_CLOCK_ERROR:
        message = "time controller clock unavailable";
        break;
    case PJ_TIME_CONTROLLER_DIAGNOSTIC_SETTINGS_ERROR:
        message = "time controller settings unavailable";
        break;
    case PJ_TIME_CONTROLLER_DIAGNOSTIC_BOOT_ID_ERROR:
        message = "time controller boot identity unavailable";
        break;
    case PJ_TIME_CONTROLLER_DIAGNOSTIC_NONE:
    default:
        break;
    }
    if (message != NULL) {
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error), "%s",
                       message);
        ESP_LOGW(TAG, "%s", g_status.last_error);
    }
}

static int time_state_initialize(void)
{
    pj_time_controller_io_t io = {
        .load = time_controller_load_record,
        .save = time_controller_save_record,
        .next_boot_id = time_controller_next_boot_id,
        .clock = time_controller_clock,
        .alarm_settings = time_controller_alarm_settings,
        .wall_time_trusted = time_controller_wall_time_trusted,
        .activity = time_controller_activity,
        .schedule_wake = time_controller_schedule_wake,
        .project_media = time_controller_project_media,
        .publish_status = time_controller_publish_status,
    };
    pj_time_controller_result_t result;
    int ready = pj_time_controller_init(&g_time_controller, &io, &result);
    if (ready) {
        ESP_LOGI(TAG, "Time controller initialized with boot id %u",
                 (unsigned)g_time_controller.boot_id);
    }
    return ready;
}

static const char *rtc_wake_result_name(pj_rtc_wake_result_t result)
{
    switch (result) {
    case PJ_RTC_WAKE_OK: return "ok";
    case PJ_RTC_WAKE_ERR_READ_CONTROL1: return "control1 read";
    case PJ_RTC_WAKE_ERR_SANITIZE_CONTROL1: return "control1 sanitize";
    case PJ_RTC_WAKE_ERR_READ_CONTROL: return "control read";
    case PJ_RTC_WAKE_ERR_DISABLE_CONTROL: return "interrupt disable";
    case PJ_RTC_WAKE_ERR_CLEAR_TIMER: return "timer clear";
    case PJ_RTC_WAKE_ERR_DISABLE_TIMER: return "timer disable";
    case PJ_RTC_WAKE_ERR_DISABLE_ALARM: return "alarm disable";
    case PJ_RTC_WAKE_ERR_WRITE_ALARM: return "alarm write";
    case PJ_RTC_WAKE_ERR_READBACK_ALARM: return "alarm readback";
    case PJ_RTC_WAKE_ERR_PERSIST: return "metadata persist";
    case PJ_RTC_WAKE_ERR_ENABLE_CONTROL: return "interrupt enable";
    case PJ_RTC_WAKE_ERR_VERIFY_CONTROL: return "control verify";
    case PJ_RTC_WAKE_ERR_INVALID:
    default: return "invalid plan";
    }
}

static pj_rtc_wake_result_t rtc_wake_disarm_board(uint8_t *flags, int force)
{
    g_rtc_wake_hardware_verified = 0;
    if (!g_rtc_ready || (!force && g_rtc_wake_plan.state != PJ_RTC_WAKE_ARMED)) {
        return PJ_RTC_WAKE_OK;
    }
    if (!rtc_sequence_take(pdMS_TO_TICKS(500))) {
        return PJ_RTC_WAKE_ERR_DISABLE_CONTROL;
    }
    pj_rtc_wake_io_t io = rtc_wake_io();
    pj_rtc_wake_result_t result = pj_rtc_wake_disarm(&io, flags);
    rtc_sequence_give();
    if (g_rtc_ext1_enabled) {
        (void)esp_sleep_disable_ext1_wakeup_io(1ull << RTC_INT_PIN);
        g_rtc_ext1_enabled = 0;
    }
    return result;
}

static int rtc_wake_sync(void)
{
    const pj_time_state_t *state = pj_time_controller_state(&g_time_controller);
    if (!g_rtc_ready || state == NULL || g_rtc_wake_metadata_blocked) {
        return -1;
    }
    if (!g_time_controller.wall_time_trusted) {
        (void)rtc_wake_disarm_board(NULL, 0);
        return -1;
    }
    pj_time_clock_t clock;
    if (!board_time_model_clock(&clock)) {
        return -1;
    }
    pj_rtc_wake_plan_t plan;
    if (!pj_rtc_wake_plan(state, &clock, &plan)) {
        return -1;
    }
    if (plan.state != PJ_RTC_WAKE_ARMED) {
        pj_rtc_wake_result_t result = rtc_wake_disarm_board(NULL, 0);
        if (result != PJ_RTC_WAKE_OK) {
            ESP_LOGW(TAG, "RTC wake disarm failed at %s", rtc_wake_result_name(result));
        }
        return plan.state == PJ_RTC_WAKE_DUE ? 0 : 1;
    }
    if (g_rtc_wake_plan.state == PJ_RTC_WAKE_ARMED &&
        g_rtc_wake_plan.token == plan.token && g_rtc_wake_hardware_verified &&
        g_rtc_ext1_enabled) {
        return gpio_get_level(RTC_INT_PIN) == 0 ? 0 : 1;
    }
    if (!rtc_sequence_take(pdMS_TO_TICKS(500))) {
        return -1;
    }
    pj_rtc_wake_io_t io = rtc_wake_io();
    g_rtc_wake_hardware_verified = 0;
    pj_rtc_wake_result_t result = pj_rtc_wake_program(&plan, &io);
    rtc_sequence_give();
    if (result != PJ_RTC_WAKE_OK) {
        (void)rtc_wake_disarm_board(NULL, 1);
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "RTC wake setup failed at %s", rtc_wake_result_name(result));
        ESP_LOGW(TAG, "%s", g_status.last_error);
        return -1;
    }
    pj_time_clock_t verify_clock;
    pj_rtc_wake_plan_t verify_plan;
    if (!board_time_model_clock(&verify_clock) ||
        !pj_rtc_wake_plan(state, &verify_clock, &verify_plan) ||
        verify_plan.state != PJ_RTC_WAKE_ARMED || verify_plan.token != plan.token) {
        (void)rtc_wake_disarm_board(NULL, 1);
        ESP_LOGW(TAG, "RTC wake target changed during setup; sleep deferred");
        return 0;
    }
    if (gpio_get_level(RTC_INT_PIN) == 0) {
        (void)rtc_wake_disarm_board(NULL, 1);
        ESP_LOGW(TAG, "RTC wake setup raced a due alarm; sleep deferred");
        return 0;
    }
    esp_err_t err = esp_sleep_enable_ext1_wakeup_io(1ull << RTC_INT_PIN,
                                                    ESP_EXT1_WAKEUP_ANY_LOW);
    if (err != ESP_OK) {
        (void)rtc_wake_disarm_board(NULL, 1);
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "RTC GPIO5 wake setup failed: %s", esp_err_to_name(err));
        ESP_LOGW(TAG, "%s", g_status.last_error);
        return -1;
    }
    g_rtc_ext1_enabled = 1;
    g_rtc_wake_hardware_verified = 1;
    ESP_LOGI(TAG, "RTC wake armed token=%08x target_day=%ld second=%lu%s",
             (unsigned)plan.token, (long)plan.target_local_day,
             (unsigned long)plan.target_local_second,
             plan.checkpoint ? " checkpoint" : "");
    return 1;
}

static void rtc_wake_restore(void)
{
    pj_rtc_wake_plan_t persisted;
    rtc_wake_load_result_t load_result = rtc_wake_plan_load(&persisted);
    if (load_result == RTC_WAKE_LOAD_IO_ERROR) {
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "RTC wake metadata read failed; persistence left untouched");
        ESP_LOGW(TAG, "%s", g_status.last_error);
        if (rtc_sequence_take(pdMS_TO_TICKS(500))) {
            pj_rtc_wake_io_t io = rtc_wake_io();
            io.persist = rtc_wake_plan_preserve;
            (void)pj_rtc_wake_disarm(&io, NULL);
            rtc_sequence_give();
        }
        g_rtc_wake_hardware_verified = 0;
        g_rtc_wake_metadata_blocked = 1;
        g_rtc_wake_restored = 1;
        return;
    }
    if (load_result == RTC_WAKE_LOAD_INVALID) {
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "RTC wake metadata was invalid; clearing hardware alarm");
        ESP_LOGW(TAG, "%s", g_status.last_error);
        memset(&persisted, 0, sizeof(persisted));
        persisted.version = PJ_RTC_WAKE_PLAN_VERSION;
    }
    g_rtc_wake_plan = persisted;
    g_rtc_wake_metadata_blocked = 0;
    g_rtc_wake_hardware_verified = 0;
    uint8_t flags = 0;
    pj_rtc_wake_result_t result = rtc_wake_disarm_board(&flags, 1);
    if (result != PJ_RTC_WAKE_OK) {
        ESP_LOGW(TAG, "RTC wake restore clear failed at %s", rtc_wake_result_name(result));
    } else if (flags != 0) {
        ESP_LOGI(TAG, "Recovered RTC wake flags=0x%02x token=%08x; model remains authoritative",
                 flags, (unsigned)persisted.token);
    }
    g_rtc_wake_restored = 1;
    (void)rtc_wake_sync();
}

static int time_state_project(pj_ui_context_t *ui)
{
    const pj_time_state_t *state = pj_time_controller_state(&g_time_controller);
    if (state == NULL || ui == NULL) {
        return 0;
    }
    pj_settings_t settings;
    if (!settings_take(portMAX_DELAY)) {
        return 0;
    }
    settings = g_settings;
    settings_give();
    pj_ui_time_projection_t projection = {
        .alarm_enabled = state->alarm_enabled,
        .alarm_hour = state->alarm_hour,
        .alarm_minute = state->alarm_minute,
        .stopwatch_running = state->stopwatch_running,
        .stopwatch_elapsed_ms = pj_time_stopwatch_elapsed(state),
        .timer_running = state->timer.running,
        .timer_remaining_ms = state->timer.remaining_ms != 0 ?
            state->timer.remaining_ms : (uint64_t)settings.timer_seconds * 1000u,
        .interval_running = state->interval.running,
        .interval_remaining_ms = state->interval.remaining_ms != 0 ?
            state->interval.remaining_ms : (uint64_t)settings.interval_seconds * 1000u,
        .interval_phase = state->interval_phase,
        .recovery_time_uncertain = state->recovery_time_uncertain,
    };
    const pj_time_alert_t *alert = pj_time_active_alert(state);
    pj_time_conflict_action_t action = PJ_TIME_PRESENT;
    if (alert != NULL) {
        projection.active_alert = *alert;
        pj_time_activity_t activity = board_time_activity();
        action = pj_time_alert_conflict_action((pj_time_alert_source_t)alert->source,
                                               activity);
        projection.alert_audio_deferred = action == PJ_TIME_VISUAL_DEFER_AUDIO;
    }
    pj_ui_set_time_projection(ui, &projection);
    return 1;
}

static int wav_write_header(FILE *file, uint32_t data_bytes, uint16_t channels, uint32_t sample_rate)
{
    uint8_t header[PJ_STORAGE_WAV_HEADER_BYTES];
    if (!pj_storage_wav_encode_header(header, sizeof(header), data_bytes,
                                      sample_rate, channels,
                                      PJ_AUDIO_BITS_PER_SAMPLE)) {
        return 0;
    }
    long pos = ftell(file);
    if (pos < 0 || fseek(file, 0, SEEK_SET) != 0) {
        return 0;
    }
    size_t written = fwrite(header, 1, sizeof(header), file);
    if (pos < (long)sizeof(header)) {
        pos = (long)sizeof(header);
    }
    return written == sizeof(header) && fseek(file, pos, SEEK_SET) == 0;
}

static int wav_normalize_pcm16(FILE *file, uint32_t data_bytes, uint32_t robust_peak,
                               uint32_t absolute_peak, uint32_t raw_avg,
                               uint32_t *gain_q16, uint32_t *normalized_peak, uint32_t *normalized_avg)
{
    *gain_q16 = pj_audio_normalization_gain_with_headroom_q16(
        robust_peak, absolute_peak, raw_avg);
    *normalized_peak = absolute_peak;
    *normalized_avg = raw_avg;
    if (*gain_q16 == PJ_AUDIO_GAIN_UNITY_Q16) {
        return 1;
    }

    int16_t samples[PJ_AUDIO_IO_BUFFER_BYTES / sizeof(int16_t)];
    uint32_t offset = 0;
    uint64_t normalized_abs_sum = 0;
    uint32_t normalized_samples = 0;
    *normalized_peak = 0;
    while (offset < data_bytes) {
        uint32_t remaining = data_bytes - offset;
        size_t bytes = remaining < sizeof(samples) ? remaining : sizeof(samples);
        size_t count = bytes / sizeof(int16_t);
        long position = 44L + (long)offset;
        if (fseek(file, position, SEEK_SET) != 0 || fread(samples, sizeof(int16_t), count, file) != count) {
            return 0;
        }

        uint32_t block_peak = 0;
        uint32_t block_avg = 0;
        pj_audio_apply_gain(samples, count, *gain_q16, &block_peak, &block_avg);
        if (fseek(file, position, SEEK_SET) != 0 || fwrite(samples, sizeof(int16_t), count, file) != count) {
            return 0;
        }
        if (block_peak > *normalized_peak) {
            *normalized_peak = block_peak;
        }
        normalized_abs_sum += (uint64_t)block_avg * count;
        normalized_samples += (uint32_t)count;
        offset += (uint32_t)(count * sizeof(int16_t));
        taskYIELD();
    }
    *normalized_avg = normalized_samples > 0 ? (uint32_t)(normalized_abs_sum / normalized_samples) : 0;
    return fseek(file, 44L + (long)data_bytes, SEEK_SET) == 0;
}

static int wav_filter_pcm16(FILE *file, uint32_t data_bytes, uint32_t *peak, uint32_t *robust_peak,
                            uint32_t *avg_abs, uint32_t *clipped)
{
    int16_t samples[(PJ_AUDIO_IO_BUFFER_BYTES / sizeof(int16_t)) + 1U];
    uint32_t histogram[PJ_AUDIO_LEVEL_HISTOGRAM_BINS] = {0};
    pj_audio_filter_state_t filter;
    pj_audio_filter_init(&filter);
    uint32_t total_samples = data_bytes / sizeof(int16_t);
    uint32_t offset_samples = 0;
    uint64_t abs_sum = 0;
    *peak = 0;
    *avg_abs = 0;
    *clipped = 0;

    while (offset_samples < total_samples) {
        uint32_t remaining = total_samples - offset_samples;
        size_t count = remaining < (PJ_AUDIO_IO_BUFFER_BYTES / sizeof(int16_t)) ?
                       remaining : (PJ_AUDIO_IO_BUFFER_BYTES / sizeof(int16_t));
        size_t read_count = count + (remaining > count ? 1U : 0U);
        long position = 44L + (long)(offset_samples * sizeof(int16_t));
        if (fseek(file, position, SEEK_SET) != 0 ||
            fread(samples, sizeof(int16_t), read_count, file) != read_count) {
            return 0;
        }

        pj_audio_filter_block(&filter, samples, count,
                              read_count > count ? samples[count] : 0,
                              read_count > count, offset_samples, total_samples);
        for (size_t i = 0; i < count; i++) {
            uint32_t magnitude = audio_abs_sample(samples[i]);
            if (magnitude > *peak) {
                *peak = magnitude;
            }
            abs_sum += magnitude;
            pj_audio_histogram_add(histogram, samples[i]);
            if (magnitude >= 32000U) {
                (*clipped)++;
            }
        }
        if (fseek(file, position, SEEK_SET) != 0 ||
            fwrite(samples, sizeof(int16_t), count, file) != count) {
            return 0;
        }
        offset_samples += (uint32_t)count;
        taskYIELD();
    }

    *avg_abs = total_samples > 0 ? (uint32_t)(abs_sum / total_samples) : 0;
    *robust_peak = pj_audio_histogram_percentile(
        histogram, total_samples, PJ_AUDIO_ROBUST_PEAK_PERMILLE);
    return fseek(file, 44L + (long)data_bytes, SEEK_SET) == 0;
}

static int audio_process_recording(audio_process_args_t *args)
{
    FILE *file = fopen(args->temporary_path, "rb+");
    if (file == NULL) {
        ESP_LOGE(TAG, "Audio processing open failed: %s", args->temporary_path);
        return 0;
    }

    uint32_t filtered_peak = 0;
    uint32_t robust_peak = 0;
    uint32_t filtered_avg = 0;
    uint32_t filtered_clipped = 0;
    uint32_t gain_q16 = PJ_AUDIO_GAIN_UNITY_Q16;
    uint32_t normalized_peak = 0;
    uint32_t normalized_avg = 0;
    int filter_ok = wav_filter_pcm16(file, args->data_bytes, &filtered_peak, &robust_peak,
                                     &filtered_avg, &filtered_clipped);
    int normalize_ok = filter_ok && fflush(file) == 0 &&
                       wav_normalize_pcm16(file, args->data_bytes, robust_peak, filtered_peak, filtered_avg,
                                           &gain_q16, &normalized_peak, &normalized_avg);
    int flush_ok = fflush(file) == 0 && fsync(fileno(file)) == 0;
    int close_ok = fclose(file) == 0;
    int ready = filter_ok && normalize_ok && flush_ok && close_ok &&
                recording_publish_file(args->temporary_path, args->final_path,
                                       args->data_bytes);
    if (!ready) {
        ESP_LOGE(TAG, "Audio processing failed: %s", args->temporary_path);
        remove(args->temporary_path);
        return 0;
    }

    ESP_LOGI(TAG, "Audio processing complete: %s input_channel=%d raw_peak=%u raw_avg_abs=%u raw_clipped=%u "
             "filtered_peak=%u robust_peak=%u filtered_avg_abs=%u filtered_clipped=%u "
             "normalize_gain_x100=%u peak=%u avg_abs=%u",
             args->final_path, args->input_channel, (unsigned)args->raw_peak, (unsigned)args->raw_avg,
             (unsigned)args->raw_clipped, (unsigned)filtered_peak, (unsigned)robust_peak,
             (unsigned)filtered_avg, (unsigned)filtered_clipped,
             (unsigned)(((uint64_t)gain_q16 * 100U) / PJ_AUDIO_GAIN_UNITY_Q16),
             (unsigned)normalized_peak, (unsigned)normalized_avg);
    return 1;
}

static void audio_process_task(void *arg)
{
    audio_process_args_t *args = arg;
    int note_ready = audio_process_recording(args);
    free(args);
    if (!note_ready) {
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "recording post-processing or publication failed");
    }
    recording_state_finish_processing(note_ready);
    recording_publish_completion();
    g_audio_process_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t audio_i2s_init(void)
{
    if (g_i2s_rx_chan != NULL && g_i2s_tx_chan != NULL) {
        return ESP_OK;
    }
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &g_i2s_tx_chan, &g_i2s_rx_chan), TAG, "i2s channel init failed");
    i2s_std_gpio_config_t gpio_cfg = audio_i2s_gpio_config(g_i2s_dout_pin);
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(PJ_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = gpio_cfg,
    };
    std_cfg.clk_cfg.mclk_multiple = PJ_AUDIO_MCLK_MULTIPLE;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(g_i2s_tx_chan, &std_cfg), TAG, "i2s tx std init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(g_i2s_rx_chan, &std_cfg), TAG, "i2s rx std init failed");
    return ESP_OK;
}

static esp_err_t audio_codec_init(void)
{
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = ESP32_I2C_DEV_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = g_i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(ctrl_if != NULL, ESP_FAIL, TAG, "audio codec i2c ctrl init failed");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = g_i2s_rx_chan,
        .tx_handle = g_i2s_tx_chan,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(data_if != NULL, ESP_FAIL, TAG, "audio codec i2s data init failed");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(gpio_if != NULL, ESP_FAIL, TAG, "audio codec gpio init failed");

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = AUDIO_PA_PIN,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = {
            .pa_gain = 6.0f,
        },
        .mclk_div = PJ_AUDIO_MCLK_MULTIPLE,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_cfg);
    ESP_RETURN_ON_FALSE(codec_if != NULL, ESP_FAIL, TAG, "es8311 codec iface init failed");
    g_es8311_codec_if = codec_if;

    esp_codec_dev_cfg_t playback_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    g_audio_playback_codec = esp_codec_dev_new(&playback_cfg);
    ESP_RETURN_ON_FALSE(g_audio_playback_codec != NULL, ESP_FAIL, TAG, "audio playback codec device init failed");

    esp_codec_dev_cfg_t record_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    g_audio_record_codec = esp_codec_dev_new(&record_cfg);
    ESP_RETURN_ON_FALSE(g_audio_record_codec != NULL, ESP_FAIL, TAG, "audio record codec device init failed");
    g_audio_codec = g_audio_playback_codec;

    esp_codec_dev_sample_info_t sample_cfg = {
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .channel = 2,
        .channel_mask = 0x03,
        .sample_rate = PJ_AUDIO_SAMPLE_RATE,
        .mclk_multiple = PJ_AUDIO_MCLK_MULTIPLE,
    };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(g_audio_playback_codec, &sample_cfg) == ESP_CODEC_DEV_OK,
                        ESP_FAIL, TAG, "audio playback codec open failed");
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(g_audio_record_codec, &sample_cfg) == ESP_CODEC_DEV_OK,
                        ESP_FAIL, TAG, "audio record codec open failed");
    ESP_RETURN_ON_FALSE(esp_codec_dev_set_out_vol(g_audio_playback_codec,
                                                  pj_settings_codec_volume(g_settings.volume)) == ESP_CODEC_DEV_OK,
                        ESP_FAIL, TAG, "audio codec volume set failed");
    ESP_RETURN_ON_FALSE(esp_codec_dev_set_in_gain(g_audio_record_codec, PJ_AUDIO_MIC_GAIN_DB) == ESP_CODEC_DEV_OK,
                        ESP_FAIL, TAG, "audio codec input gain set failed");
    ESP_RETURN_ON_FALSE(esp_codec_dev_set_out_mute(g_audio_playback_codec, true) == ESP_CODEC_DEV_OK,
                        ESP_FAIL, TAG, "audio codec output mute failed");
    audio_pa_set(0);
    return ESP_OK;
}

static esp_err_t audio_init(void)
{
    if (g_audio_ready) {
        return ESP_OK;
    }
    if (g_audio_lock == NULL) {
        g_audio_lock = xSemaphoreCreateMutex();
        if (g_audio_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    gpio_config_t pa_conf = {
        .pin_bit_mask = (1ULL << AUDIO_PA_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pa_conf), TAG, "audio pa gpio config failed");
    audio_pa_set(0);

    ESP_RETURN_ON_ERROR(audio_i2s_init(), TAG, "audio i2s init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(g_i2s_tx_chan), TAG, "i2s tx enable for codec init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(g_i2s_rx_chan), TAG, "i2s rx enable for codec init failed");
    ESP_RETURN_ON_ERROR(audio_codec_init(), TAG, "es8311 codec init failed");
    g_audio_ready = 1;
    ESP_LOGI(TAG, "ES8311 audio initialized via esp_codec_dev");
    return ESP_OK;
}

static void record_task_exit(void)
{
    if (g_record_audio_owned) {
        g_record_audio_owned = 0;
        xSemaphoreGive(g_audio_lock);
    }
    recording_publish_completion();
    g_record_task = NULL;
    board_audio_state_set(0, -1);
    alert_audio_set_recording(0);
    g_audio_state_update_pending = 1;
    vTaskDelete(NULL);
}

static void playback_task_exit(void)
{
    g_playback_task = NULL;
    board_audio_state_set(-1, 0);
    alert_audio_notify();
    g_audio_state_update_pending = 1;
    vTaskDelete(NULL);
}

static void record_task(void *arg)
{
    (void)arg;
    if (g_audio_lock == NULL || xSemaphoreTake(g_audio_lock, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Recording could not acquire audio ownership");
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "recording could not acquire audio ownership");
        recording_state_finish_capture(0);
        record_task_exit();
        return;
    }
    g_record_audio_owned = 1;
    int16_t *stereo = malloc(PJ_AUDIO_IO_BUFFER_BYTES);
    int16_t *mono = malloc((PJ_AUDIO_IO_BUFFER_BYTES / PJ_AUDIO_FRAME_BYTES) * sizeof(int16_t));
    uint32_t data_bytes = 0;
    uint32_t next_capacity_check = PJ_STORAGE_CAPACITY_CHECK_BYTES;
    uint32_t read_errors = 0;
    uint32_t consecutive_read_errors = 0;
    uint32_t peak = 0;
    uint64_t abs_sum = 0;
    int input_channel = -1;
    uint32_t sample_count = 0;
    uint32_t clipped_samples = 0;
    char temporary_path[144];
    (void)snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", g_active_recording_path);
    remove(temporary_path);
    if (stereo == NULL || mono == NULL) {
        ESP_LOGE(TAG, "Recording buffer allocation failed");
        free(stereo);
        free(mono);
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "recording buffer allocation failed");
        recording_state_finish_capture(0);
        record_task_exit();
        return;
    }
    FILE *file = fopen(temporary_path, "wb+");
    if (file == NULL) {
        ESP_LOGE(TAG, "Recording open failed: %s", temporary_path);
        free(stereo);
        free(mono);
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "recording file open failed");
        recording_state_finish_capture(0);
        record_task_exit();
        return;
    }
    if (!wav_write_header(file, 0, PJ_AUDIO_CHANNELS, PJ_AUDIO_SAMPLE_RATE)) {
        ESP_LOGE(TAG, "Recording initial WAV header failed: %s", temporary_path);
        fclose(file);
        remove(temporary_path);
        free(stereo);
        free(mono);
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "recording WAV header creation failed");
        recording_state_finish_capture(0);
        record_task_exit();
        return;
    }
    int gain_ret = esp_codec_dev_set_in_gain(g_audio_record_codec, PJ_AUDIO_MIC_GAIN_DB);
    if (gain_ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Recording input gain setup failed: %s", esp_err_to_name((esp_err_t)gain_ret));
        fclose(file);
        remove(temporary_path);
        free(stereo);
        free(mono);
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "recording input gain setup failed");
        recording_state_finish_capture(0);
        record_task_exit();
        return;
    }
    int capture_failed = 0;
    while (!g_record_stop_requested) {
        int err = esp_codec_dev_read(g_audio_record_codec, stereo, PJ_AUDIO_IO_BUFFER_BYTES);
        if (err != ESP_CODEC_DEV_OK) {
            read_errors++;
            consecutive_read_errors++;
            if (read_errors <= 3) {
                ESP_LOGW(TAG, "Recording codec read failed: %s", esp_err_to_name(err));
            }
            if (consecutive_read_errors >= PJ_AUDIO_MAX_CONSECUTIVE_READ_ERRORS) {
                ESP_LOGE(TAG, "Recording stopped after %u consecutive read errors",
                         (unsigned)consecutive_read_errors);
                capture_failed = 1;
                break;
            }
            continue;
        }
        consecutive_read_errors = 0;
        size_t frames = PJ_AUDIO_IO_BUFFER_BYTES / PJ_AUDIO_FRAME_BYTES;
        if (input_channel < 0) {
            uint64_t left_abs_sum = 0;
            uint64_t right_abs_sum = 0;
            for (size_t i = 0; i < frames; i++) {
                left_abs_sum += audio_abs_sample(stereo[i * 2]);
                right_abs_sum += audio_abs_sample(stereo[i * 2 + 1]);
            }
            input_channel = pj_audio_select_channel(left_abs_sum, right_abs_sum);
            ESP_LOGI(TAG, "Recording selected fixed I2S input channel %d: left_abs=%llu right_abs=%llu",
                     input_channel, (unsigned long long)left_abs_sum, (unsigned long long)right_abs_sum);
        }
        for (size_t i = 0; i < frames; i++) {
            mono[i] = stereo[i * 2 + (size_t)input_channel];
            audio_update_stats(mono[i], &peak, &abs_sum, &sample_count);
            if (audio_abs_sample(mono[i]) >= 32000U) {
                clipped_samples++;
            }
        }
        if (data_bytes >= next_capacity_check) {
            if (!storage_preflight(PJ_STORAGE_CAPACITY_CHECK_BYTES + PJ_AUDIO_IO_BUFFER_BYTES,
                                   "recording")) {
                ESP_LOGE(TAG, "Recording stopped before consuming the storage reserve");
                capture_failed = 1;
                break;
            }
            next_capacity_check += PJ_STORAGE_CAPACITY_CHECK_BYTES;
        }
        size_t written = fwrite(mono, sizeof(int16_t), frames, file);
        size_t written_bytes = written * sizeof(int16_t);
        data_bytes += (uint32_t)written_bytes;
        if (written_bytes > 0 && !recording_state_commit(written_bytes)) {
            ESP_LOGE(TAG, "Recording progress rejected invalid PCM write size=%u",
                     (unsigned)written_bytes);
            capture_failed = 1;
            break;
        }
        if (written != frames) {
            ESP_LOGE(TAG, "Recording write failed: expected=%u samples wrote=%u",
                     (unsigned)frames, (unsigned)written);
            capture_failed = 1;
            break;
        }
    }
    g_record_audio_owned = 0;
    xSemaphoreGive(g_audio_lock);
    uint32_t raw_avg = sample_count > 0 ? (uint32_t)(abs_sum / sample_count) : 0;
    int header_ok = wav_write_header(file, data_bytes, PJ_AUDIO_CHANNELS, PJ_AUDIO_SAMPLE_RATE);
    int flush_ok = fflush(file) == 0 && fsync(fileno(file)) == 0;
    int close_ok = fclose(file) == 0;
    int finalized = header_ok && flush_ok && close_ok && !capture_failed && data_bytes > 0;
    recording_state_finish_capture(finalized);
    free(stereo);
    free(mono);
    if (!finalized) {
        ESP_LOGE(TAG, "Recording finalization failed or produced no audio: %s", temporary_path);
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "recording capture or WAV finalization failed");
        remove(temporary_path);
        record_task_exit();
        return;
    }

    struct stat st;
    long file_size = stat(temporary_path, &st) == 0 ? (long)st.st_size : -1;
    ESP_LOGI(TAG, "Recording capture stopped: %s bytes=%u file_size=%ld read_errors=%u "
             "raw_peak=%u raw_avg_abs=%u raw_clipped=%u; processing asynchronously",
             g_active_recording_path, (unsigned)data_bytes, file_size, (unsigned)read_errors,
             (unsigned)peak, (unsigned)raw_avg, (unsigned)clipped_samples);

    audio_process_args_t *process_args = malloc(sizeof(*process_args));
    int note_ready = 0;
    if (process_args != NULL) {
        (void)snprintf(process_args->temporary_path, sizeof(process_args->temporary_path), "%s", temporary_path);
        (void)snprintf(process_args->final_path, sizeof(process_args->final_path), "%s", g_active_recording_path);
        process_args->data_bytes = data_bytes;
        process_args->raw_peak = peak;
        process_args->raw_avg = raw_avg;
        process_args->raw_clipped = clipped_samples;
        process_args->input_channel = input_channel;
        BaseType_t created = xTaskCreate(audio_process_task, "pj-audio-process",
                                        PJ_AUDIO_PROCESS_TASK_STACK, process_args, 1,
                                        &g_audio_process_task);
        if (created != pdPASS) {
            g_audio_process_task = NULL;
            ESP_LOGW(TAG, "Audio processing task start failed; preserving raw recording");
            note_ready = recording_publish_file(temporary_path,
                                                g_active_recording_path,
                                                data_bytes);
            recording_state_finish_processing(note_ready);
            free(process_args);
        }
    } else {
        ESP_LOGW(TAG, "Audio processing task allocation failed; preserving raw recording");
        note_ready = recording_publish_file(temporary_path,
                                            g_active_recording_path,
                                            data_bytes);
        recording_state_finish_processing(note_ready);
    }
    if (g_audio_process_task == NULL && !note_ready) {
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "recording post-processing or publication failed");
    }
    record_task_exit();
}

static int wav_read_header(FILE *file, uint16_t *channels, uint32_t *sample_rate, uint16_t *bits, uint32_t *data_bytes)
{
    uint8_t header[44];
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, "RIFF", 4) != 0 ||
        memcmp(&header[8], "WAVE", 4) != 0 ||
        memcmp(&header[12], "fmt ", 4) != 0 ||
        memcmp(&header[36], "data", 4) != 0) {
        return 0;
    }
    *channels = (uint16_t)(header[22] | (header[23] << 8));
    *sample_rate = (uint32_t)header[24] | ((uint32_t)header[25] << 8) | ((uint32_t)header[26] << 16) | ((uint32_t)header[27] << 24);
    *bits = (uint16_t)(header[34] | (header[35] << 8));
    *data_bytes = (uint32_t)header[40] | ((uint32_t)header[41] << 8) | ((uint32_t)header[42] << 16) | ((uint32_t)header[43] << 24);
    return 1;
}

static void playback_task(void *arg)
{
    (void)arg;
    int16_t *mono = malloc(PJ_AUDIO_IO_BUFFER_BYTES);
    int16_t *stereo = malloc(PJ_AUDIO_IO_BUFFER_BYTES * 2);
    if (mono == NULL || stereo == NULL) {
        ESP_LOGE(TAG, "Playback buffer allocation failed");
        free(mono);
        free(stereo);
        playback_task_exit();
        return;
    }
    FILE *file = fopen(g_active_playback_path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Playback open failed: %s", g_active_playback_path);
        free(mono);
        free(stereo);
        playback_task_exit();
        return;
    }
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t sample_rate = 0;
    uint32_t data_bytes = 0;
    uint32_t peak = 0;
    uint64_t abs_sum = 0;
    uint32_t sample_count = 0;
    if (!wav_read_header(file, &channels, &sample_rate, &bits, &data_bytes) ||
        bits != PJ_AUDIO_BITS_PER_SAMPLE || sample_rate != PJ_AUDIO_SAMPLE_RATE ||
        (channels != 1 && channels != 2) || data_bytes == 0) {
        ESP_LOGE(TAG, "Playback rejected unsupported/empty WAV: %s channels=%u sample_rate=%u bits=%u data_bytes=%u",
                 g_active_playback_path, channels, (unsigned)sample_rate, bits, (unsigned)data_bytes);
        fclose(file);
        free(mono);
        free(stereo);
        playback_task_exit();
        return;
    }
    ESP_LOGI(TAG, "Playback WAV header: %s channels=%u sample_rate=%u bits=%u data_bytes=%u",
             g_active_playback_path, channels, (unsigned)sample_rate, bits, (unsigned)data_bytes);
    if (g_audio_lock == NULL || xSemaphoreTake(g_audio_lock, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Playback could not acquire audio ownership");
        fclose(file);
        free(mono);
        free(stereo);
        playback_task_exit();
        return;
    }
    if (g_playback_stop_requested) {
        xSemaphoreGive(g_audio_lock);
        fclose(file);
        free(mono);
        free(stereo);
        playback_task_exit();
        return;
    }
    esp_err_t output_err = audio_prepare_output("playback", -1,
                                                settings_codec_volume_snapshot());
    if (output_err != ESP_OK) {
        ESP_LOGE(TAG, "Playback output prepare failed: %s", esp_err_to_name(output_err));
        fclose(file);
        free(mono);
        free(stereo);
        xSemaphoreGive(g_audio_lock);
        playback_task_exit();
        return;
    }
    while (!g_playback_stop_requested) {
        if (channels == 1) {
            size_t samples = fread(mono, sizeof(int16_t), PJ_AUDIO_IO_BUFFER_BYTES / sizeof(int16_t), file);
            if (samples == 0) {
                break;
            }
            for (size_t i = 0; i < samples; i++) {
                audio_update_stats(mono[i], &peak, &abs_sum, &sample_count);
                stereo[i * 2] = mono[i];
                stereo[i * 2 + 1] = mono[i];
            }
            int write_err = esp_codec_dev_write(g_audio_playback_codec, stereo, (int)(samples * PJ_AUDIO_FRAME_BYTES));
            if (write_err != ESP_CODEC_DEV_OK) {
                ESP_LOGW(TAG, "Playback codec write failed: %s", esp_err_to_name(write_err));
                break;
            }
        } else {
            size_t read = fread(stereo, 1, PJ_AUDIO_IO_BUFFER_BYTES, file);
            if (read == 0) {
                break;
            }
            for (size_t i = 0; i < read / sizeof(int16_t); i++) {
                audio_update_stats(stereo[i], &peak, &abs_sum, &sample_count);
            }
            int write_err = esp_codec_dev_write(g_audio_playback_codec, stereo, (int)read);
            if (write_err != ESP_CODEC_DEV_OK) {
                ESP_LOGW(TAG, "Playback codec write failed: %s", esp_err_to_name(write_err));
                break;
            }
        }
    }
    audio_finish_output("playback-finish");
    xSemaphoreGive(g_audio_lock);
    fclose(file);
    free(mono);
    free(stereo);
    ESP_LOGI(TAG, "Playback stopped: %s peak=%u avg_abs=%u",
             g_active_playback_path, (unsigned)peak,
             (unsigned)(sample_count > 0 ? abs_sum / sample_count : 0));
    playback_task_exit();
}

static esp_err_t audio_play_tone_ms(uint32_t duration_ms, const audio_tone_diag_opts_t *opts)
{
    if (!g_audio_ready || g_audio_playback_codec == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (g_status.recording || g_record_task != NULL || g_status.playback_active ||
        g_playback_task != NULL || alert_audio_desired()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (g_audio_lock == NULL ||
        xSemaphoreTake(g_audio_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    size_t frames_per_block = PJ_AUDIO_IO_BUFFER_BYTES / PJ_AUDIO_FRAME_BYTES;
    int16_t *stereo = malloc(frames_per_block * PJ_AUDIO_FRAME_BYTES);
    if (stereo == NULL) {
        xSemaphoreGive(g_audio_lock);
        return ESP_ERR_NO_MEM;
    }

    int pa_level_override = opts != NULL ? opts->pa_level : PJ_AUDIO_TONE_DEFAULT_INT;
    int dout_gpio_override = opts != NULL ? opts->dout_gpio : PJ_AUDIO_TONE_DEFAULT_INT;
    int audio_power_override = opts != NULL ? opts->audio_power_level : PJ_AUDIO_TONE_DEFAULT_INT;
    int gpio44_override = opts != NULL ? opts->codec_gpio44 : PJ_AUDIO_TONE_DEFAULT_INT;
    int gp45_override = opts != NULL ? opts->codec_gp45 : PJ_AUDIO_TONE_DEFAULT_INT;

    gpio_num_t original_dout_pin = g_i2s_dout_pin;
    int original_audio_power_level = gpio_get_level(AUDIO_PWR_PIN);
    int original_gpio44 = audio_codec_reg_get(PJ_ES8311_GPIO_REG44);
    int original_gp45 = audio_codec_reg_get(PJ_ES8311_GP_REG45);
    esp_err_t route_err = audio_reconfigure_tx_dout(dout_gpio_override);
    if (route_err != ESP_OK) {
        free(stereo);
        xSemaphoreGive(g_audio_lock);
        return route_err;
    }
    gpio_num_t tone_dout_pin = g_i2s_dout_pin;

    if (audio_power_override == 0 || audio_power_override == 1) {
        gpio_set_level(AUDIO_PWR_PIN, audio_power_override);
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    int codec_volume = settings_codec_volume_snapshot();
    esp_err_t output_err = audio_prepare_output("tone", pa_level_override, codec_volume);
    if (output_err != ESP_OK) {
        if (gp45_override >= 0) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(audio_codec_reg_set(PJ_ES8311_GP_REG45, original_gp45));
        }
        if (gpio44_override >= 0) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(audio_codec_reg_set(PJ_ES8311_GPIO_REG44, original_gpio44));
        }
        if (audio_power_override == 0 || audio_power_override == 1) {
            gpio_set_level(AUDIO_PWR_PIN, original_audio_power_level);
        }
        if (dout_gpio_override >= 0 && g_i2s_dout_pin != original_dout_pin) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(audio_reconfigure_tx_dout((int)original_dout_pin));
        }
        free(stereo);
        xSemaphoreGive(g_audio_lock);
        return output_err;
    }
    if (gpio44_override >= 0) {
        esp_err_t reg_err = audio_codec_reg_set(PJ_ES8311_GPIO_REG44, gpio44_override);
        if (reg_err != ESP_OK) {
            audio_finish_output("tone-setup-failed");
            if (audio_power_override == 0 || audio_power_override == 1) {
                gpio_set_level(AUDIO_PWR_PIN, original_audio_power_level);
            }
            if (dout_gpio_override >= 0 && g_i2s_dout_pin != original_dout_pin) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(audio_reconfigure_tx_dout((int)original_dout_pin));
            }
            free(stereo);
            xSemaphoreGive(g_audio_lock);
            return reg_err;
        }
    }
    if (gp45_override >= 0) {
        esp_err_t reg_err = audio_codec_reg_set(PJ_ES8311_GP_REG45, gp45_override);
        if (reg_err != ESP_OK) {
            audio_finish_output("tone-setup-failed");
            if (gpio44_override >= 0) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(audio_codec_reg_set(PJ_ES8311_GPIO_REG44, original_gpio44));
            }
            if (audio_power_override == 0 || audio_power_override == 1) {
                gpio_set_level(AUDIO_PWR_PIN, original_audio_power_level);
            }
            if (dout_gpio_override >= 0 && g_i2s_dout_pin != original_dout_pin) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(audio_reconfigure_tx_dout((int)original_dout_pin));
            }
            free(stereo);
            xSemaphoreGive(g_audio_lock);
            return reg_err;
        }
    }

    uint32_t total_frames = (PJ_AUDIO_SAMPLE_RATE * duration_ms) / 1000;
    uint32_t generated = 0;
    uint32_t phase = 0;
    esp_err_t result = ESP_OK;

    while (generated < total_frames) {
        size_t frames = frames_per_block;
        if (frames > total_frames - generated) {
            frames = total_frames - generated;
        }
        for (size_t i = 0; i < frames; i++) {
            int16_t sample = phase < (PJ_AUDIO_SAMPLE_RATE / 2)
                                 ? PJ_AUDIO_DIAG_TONE_AMPLITUDE
                                 : -PJ_AUDIO_DIAG_TONE_AMPLITUDE;
            stereo[i * 2] = sample;
            stereo[i * 2 + 1] = sample;
            phase += PJ_AUDIO_DIAG_TONE_HZ;
            while (phase >= PJ_AUDIO_SAMPLE_RATE) {
                phase -= PJ_AUDIO_SAMPLE_RATE;
            }
        }
        int write_err = esp_codec_dev_write(g_audio_playback_codec, stereo, (int)(frames * PJ_AUDIO_FRAME_BYTES));
        if (write_err != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "Audio tone codec write failed: %s", esp_err_to_name(write_err));
            result = ESP_FAIL;
            break;
        }
        generated += (uint32_t)frames;
    }

    audio_finish_output("tone-finish");
    if (gp45_override >= 0) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(audio_codec_reg_set(PJ_ES8311_GP_REG45, original_gp45));
    }
    if (gpio44_override >= 0) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(audio_codec_reg_set(PJ_ES8311_GPIO_REG44, original_gpio44));
    }
    if (audio_power_override == 0 || audio_power_override == 1) {
        gpio_set_level(AUDIO_PWR_PIN, original_audio_power_level);
    }
    if (dout_gpio_override >= 0 && g_i2s_dout_pin != original_dout_pin) {
        esp_err_t restore_err = audio_reconfigure_tx_dout((int)original_dout_pin);
        if (restore_err != ESP_OK) {
            ESP_LOGW(TAG, "I2S TX DOUT restore failed after tone: %s", esp_err_to_name(restore_err));
        }
    }
    free(stereo);
    xSemaphoreGive(g_audio_lock);
    ESP_LOGI(TAG, "Audio tone complete: frames=%u pa_level=%d volume=%d dout=%d pwr=%d gpio44=%02x gp45=%02x result=%s",
             (unsigned)generated,
             pa_level_override == 0 || pa_level_override == 1 ? pa_level_override : AUDIO_PA_ACTIVE_LEVEL,
             codec_volume,
             (int)tone_dout_pin,
             audio_power_override == 0 || audio_power_override == 1 ? audio_power_override : original_audio_power_level,
             gpio44_override >= 0 ? gpio44_override & 0xFF : original_gpio44 & 0xFF,
             gp45_override >= 0 ? gp45_override & 0xFF : original_gp45 & 0xFF,
             esp_err_to_name(result));
    return result;
}

static esp_err_t audio_mic_check_ms(uint32_t duration_ms, int gain_db, audio_mic_check_result_t *result)
{
    if (!g_audio_ready || g_audio_record_codec == NULL || result == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (g_status.recording || g_record_task != NULL || g_status.playback_active ||
        g_playback_task != NULL || alert_audio_desired()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (g_audio_lock == NULL ||
        xSemaphoreTake(g_audio_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (duration_ms == 0) {
        duration_ms = PJ_AUDIO_MIC_CHECK_MS;
    } else if (duration_ms > PJ_AUDIO_MIC_CHECK_MAX_MS) {
        duration_ms = PJ_AUDIO_MIC_CHECK_MAX_MS;
    }

    int16_t *stereo = malloc(PJ_AUDIO_IO_BUFFER_BYTES);
    if (stereo == NULL) {
        xSemaphoreGive(g_audio_lock);
        return ESP_ERR_NO_MEM;
    }

    if (gain_db >= 0) {
        int gain_ret = esp_codec_dev_set_in_gain(g_audio_record_codec, (float)gain_db);
        if (gain_ret != ESP_CODEC_DEV_OK) {
            free(stereo);
            xSemaphoreGive(g_audio_lock);
            return (esp_err_t)gain_ret;
        }
    } else {
        gain_db = (int)PJ_AUDIO_MIC_GAIN_DB;
    }

    memset(result, 0, sizeof(*result));
    result->duration_ms = duration_ms;
    result->gain_db = gain_db;

    uint64_t abs_sum = 0;
    int input_channel = -1;
    TickType_t start = xTaskGetTickCount();
    TickType_t deadline = start + pdMS_TO_TICKS(duration_ms);
    while ((int32_t)(deadline - xTaskGetTickCount()) > 0) {
        int err = esp_codec_dev_read(g_audio_record_codec, stereo, PJ_AUDIO_IO_BUFFER_BYTES);
        if (err != ESP_CODEC_DEV_OK) {
            result->read_errors++;
            if (result->read_errors <= 3) {
                ESP_LOGW(TAG, "Mic check codec read failed: %s", esp_err_to_name(err));
            }
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        size_t frames = PJ_AUDIO_IO_BUFFER_BYTES / PJ_AUDIO_FRAME_BYTES;
        if (input_channel < 0) {
            uint64_t left_abs_sum = 0;
            uint64_t right_abs_sum = 0;
            for (size_t i = 0; i < frames; i++) {
                left_abs_sum += audio_abs_sample(stereo[i * 2]);
                right_abs_sum += audio_abs_sample(stereo[i * 2 + 1]);
            }
            input_channel = pj_audio_select_channel(left_abs_sum, right_abs_sum);
            result->input_channel = input_channel;
        }
        result->bytes_read += PJ_AUDIO_IO_BUFFER_BYTES;
        result->frames += (uint32_t)frames;
        for (size_t i = 0; i < frames; i++) {
            int16_t mono = stereo[i * 2 + (size_t)input_channel];
            uint32_t abs_value = audio_abs_sample(mono);
            if (abs_value > result->peak) {
                result->peak = abs_value;
            }
            if (abs_value <= 8) {
                result->near_zero++;
            }
            if (abs_value >= 32000) {
                result->clipped++;
            }
            abs_sum += abs_value;
        }
    }

    if (result->frames > 0) {
        result->avg_abs = (uint32_t)(abs_sum / result->frames);
    }
    free(stereo);
    xSemaphoreGive(g_audio_lock);
    ESP_LOGI(TAG,
             "Mic check complete: ms=%u gain=%d channel=%d bytes=%u frames=%u peak=%u avg_abs=%u clipped=%u near_zero=%u read_errors=%u",
             (unsigned)result->duration_ms,
             result->gain_db,
             result->input_channel,
             (unsigned)result->bytes_read,
             (unsigned)result->frames,
             (unsigned)result->peak,
             (unsigned)result->avg_abs,
             (unsigned)result->clipped,
             (unsigned)result->near_zero,
             (unsigned)result->read_errors);
    return result->frames > 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}
#endif

#ifndef ESP_PLATFORM
static board_time_snapshot_t board_time_snapshot(void)
{
    return (board_time_snapshot_t) {
        .hour = g_status.hour,
        .minute = g_status.minute,
        .year = g_status.year,
        .month = g_status.month,
        .day = g_status.day,
        .time_set = g_status.time_set,
        .generation = g_time_generation,
    };
}

static int board_time_take_pending(board_time_snapshot_t *snapshot)
{
    if (!g_time_update_pending || !g_status.time_set) {
        return 0;
    }
    g_time_update_pending = 0;
    *snapshot = board_time_snapshot();
    return 1;
}
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
        .flash_mb = 8,
        .psram_mb = 8,
    };
    return profile;
}

static int build_month_from_name(const char *month)
{
    static const char *names[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    for (int i = 0; i < 12; i++) {
        if (strncmp(month, names[i], 3) == 0) {
            return i + 1;
        }
    }
    return 0;
}

static int set_status_time_from_build(void)
{
    char month_name[4] = {0};
    int day = 0;
    int year = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (sscanf(__DATE__, "%3s %d %d", month_name, &day, &year) != 3 ||
        sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) {
        return 0;
    }
    int month = build_month_from_name(month_name);
    if (!valid_time_date(hour, minute, year, month, day) ||
        second < 0 || second > 59) {
        return 0;
    }
#ifdef ESP_PLATFORM
    return board_time_publish(hour, minute, year, month, day, second,
                              board_monotonic_ms(), 0);
#else
    g_status.hour = hour;
    g_status.minute = minute;
    g_status.year = year;
    g_status.month = month;
    g_status.day = day;
    g_status.time_set = 1;
    return 1;
#endif
}

static void init_default_status(const pj_board_profile_t *profile)
{
    memset(&g_status, 0, sizeof(g_status));
    g_status.display = PJ_BOARD_SERVICE_DISABLED;
    g_status.storage = PJ_BOARD_SERVICE_DISABLED;
    g_status.audio = PJ_BOARD_SERVICE_UNAVAILABLE;
    g_status.ble_provisioning = PJ_BOARD_SERVICE_DISABLED;
    g_status.wifi = PJ_BOARD_SERVICE_DISABLED;
    g_status.http = PJ_BOARD_SERVICE_DISABLED;
    g_status.battery_percent = 84;
    g_status.temperature_c = 22;
    g_status.humidity_percent = -1;
#ifdef ESP_PLATFORM
    (void)board_time_publish(9, 41, 2026, 6, 6, 0, board_monotonic_ms(), 0);
#else
    g_status.hour = 9;
    g_status.minute = 41;
    g_status.year = 2026;
    g_status.month = 6;
    g_status.day = 6;
    g_status.time_set = 1;
#endif
    (void)set_status_time_from_build();
    (void)snprintf(g_status.device_id, sizeof(g_status.device_id), "pj-%s", profile->sku);
    (void)snprintf(g_status.token, sizeof(g_status.token), "%s", PJ_DEFAULT_TOKEN);
    (void)snprintf(g_status.ip_addr, sizeof(g_status.ip_addr), "0.0.0.0");
    (void)snprintf(g_status.storage_path, sizeof(g_status.storage_path), "/sdcard");
    (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                   "BLE/Wi-Fi connect path still requires provisioning integration");
}

static int is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int days_in_month(int year, int month)
{
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    if (month < 1 || month > 12) {
        return 31;
    }
    return days[month - 1];
}

static int advance_status_one_minute(uint32_t expected_generation)
{
#ifdef ESP_PLATFORM
    portENTER_CRITICAL(&g_time_lock);
#endif
    if (!g_status.time_set || g_time_generation != expected_generation) {
        goto unchanged;
    }
    g_status.minute++;
    g_time_generation++;
    if (g_status.minute < 60) {
        goto done;
    }
    g_status.minute = 0;
    g_status.hour++;
    if (g_status.hour < 24) {
        goto done;
    }
    g_status.hour = 0;
    g_status.day++;
    if (g_status.day <= days_in_month(g_status.year, g_status.month)) {
        goto done;
    }
    g_status.day = 1;
    g_status.month++;
    if (g_status.month <= 12) {
        goto done;
    }
    g_status.month = 1;
    g_status.year++;
done:
#ifdef ESP_PLATFORM
    portEXIT_CRITICAL(&g_time_lock);
#endif
    return 1;
unchanged:
#ifdef ESP_PLATFORM
    portEXIT_CRITICAL(&g_time_lock);
#endif
    return 0;
}

void pj_board_init(const pj_board_profile_t *profile)
{
    init_default_status(profile);
    pj_settings_defaults(&g_settings);
    pj_home_layout_defaults(&g_home_layout);

#ifdef ESP_PLATFORM
    pj_recording_init(&g_recording);
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        g_status.storage = PJ_BOARD_SERVICE_ERROR;
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error), "NVS init failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "%s", g_status.last_error);
        return;
    }

    g_settings_lock = xSemaphoreCreateMutex();
    if (g_settings_lock == NULL) {
        ESP_LOGW(TAG, "Settings mutex allocation failed");
    }
    esp_err_t settings_err = ESP_FAIL;
    for (int attempt = 0; attempt < 3 && settings_err != ESP_OK; attempt++) {
        settings_err = settings_load();
        if (settings_err != ESP_OK && attempt < 2) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (settings_err != ESP_OK) {
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "settings load failed: %s",
                       esp_err_to_name(settings_err));
        ESP_LOGW(TAG, "%s; preserving in-memory defaults without writing NVS",
                 g_status.last_error);
    }
    g_home_layout_lock = xSemaphoreCreateMutex();
    if (g_home_layout_lock == NULL) {
        ESP_LOGW(TAG, "Home layout mutex allocation failed");
    }
    home_layout_load();
    g_static_art_lock = xSemaphoreCreateMutex();
    if (g_static_art_lock == NULL) {
        ESP_LOGW(TAG, "Static art mutex allocation failed");
    }

    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        (void)snprintf(g_status.device_id, sizeof(g_status.device_id), "pj-%02x%02x%02x", mac[3], mac[4], mac[5]);
    }
    wifi_load_provisioning();
    ESP_LOGI(TAG, "Board profile %s, flash %dMB, PSRAM %dMB", profile->name, profile->flash_mb, profile->psram_mb);
    ESP_LOGI(TAG, "Using Waveshare ESP-IDF V2 BSP pins for Touch-ePaper-1.54 display, touch, SD, power");

    power_init();
    button_init();
    if (battery_adc_init() == ESP_OK) {
        battery_refresh_status();
        ESP_LOGI(TAG, "Battery ADC initialized");
    } else {
        ESP_LOGW(TAG, "Battery ADC init failed");
    }

    esp_err_t i2c_err = i2c_init();
    if (i2c_err == ESP_OK) {
        if (touch_init() == ESP_OK) {
            ESP_LOGI(TAG, "FT6336 touch initialized");
        } else {
            ESP_LOGW(TAG, "FT6336 touch init failed");
        }
        if (shtc3_init() == ESP_OK) {
            ESP_LOGI(TAG, "SHTC3 temp sensor initialized");
            shtc3_refresh_status();
        } else {
            ESP_LOGW(TAG, "SHTC3 init failed");
        }
        if (rtc_init() == ESP_OK) {
            if (rtc_read_status_time()) {
                ESP_LOGI(TAG, "PCF85063 RTC initialized: %04d-%02d-%02d %02d:%02d",
                         g_status.year, g_status.month, g_status.day, g_status.hour, g_status.minute);
            } else {
                ESP_LOGW(TAG, "PCF85063 RTC initialized but time is not set; using firmware build time %04d-%02d-%02d %02d:%02d",
                         g_status.year, g_status.month, g_status.day, g_status.hour, g_status.minute);
                esp_err_t write_err = rtc_write_status_time();
                ESP_ERROR_CHECK_WITHOUT_ABORT(write_err);
                if (write_err == ESP_OK) {
                    portENTER_CRITICAL(&g_time_lock);
                    g_time_wall_trusted = 1;
                    portEXIT_CRITICAL(&g_time_lock);
                }
            }
        } else {
            ESP_LOGW(TAG, "PCF85063 RTC init failed");
        }
        if (touch_task_start() != ESP_OK) {
            ESP_LOGW(TAG, "FT6336 touch poll task failed to start");
        }
    } else {
        ESP_LOGW(TAG, "I2C init failed: %s", esp_err_to_name(i2c_err));
    }
    if (!time_state_initialize()) {
        ESP_LOGE(TAG, "Durable time state is unavailable");
    } else {
        rtc_wake_restore();
    }
    connectivity_state_init();

    esp_err_t display_err = display_init();
    if (display_err == ESP_OK) {
        g_status.display = PJ_BOARD_SERVICE_READY;
        ESP_LOGI(TAG, "e-paper display initialized");
    } else {
        g_status.display = PJ_BOARD_SERVICE_ERROR;
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error), "display init failed: %s", esp_err_to_name(display_err));
        ESP_LOGE(TAG, "%s", g_status.last_error);
    }

    esp_err_t storage_err = storage_init();
    if (storage_err == ESP_OK) {
        g_status.storage = PJ_BOARD_SERVICE_READY;
        ESP_LOGI(TAG, "microSD mounted at %s", g_status.storage_path);
        static_art_load();
    } else {
        g_status.storage = PJ_BOARD_SERVICE_ERROR;
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "microSD mount failed: %s; check card inserted and FAT32/exFAT formatting",
                       esp_err_to_name(storage_err));
        ESP_LOGW(TAG, "%s", g_status.last_error);
    }
    esp_err_t audio_err = audio_init();
    if (audio_err == ESP_OK) {
        g_status.audio = PJ_BOARD_SERVICE_READY;
        esp_err_t alert_err = alert_audio_start();
        if (alert_err != ESP_OK) {
            (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                           "time alert audio task failed: %s", esp_err_to_name(alert_err));
            ESP_LOGW(TAG, "%s", g_status.last_error);
        }
    } else {
        g_status.audio = PJ_BOARD_SERVICE_ERROR;
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "audio init failed: %s", esp_err_to_name(audio_err));
        ESP_LOGW(TAG, "%s", g_status.last_error);
    }

    if (g_wifi_credentials_stored) {
        wifi_apply_provisioning_status();
    } else {
        g_status.ble_provisioning = PJ_BOARD_SERVICE_UNAVAILABLE;
        g_status.wifi = PJ_BOARD_SERVICE_UNAVAILABLE;
    }
#else
    (void)profile;
#endif
}

void pj_board_start_services(const pj_board_profile_t *profile)
{
    (void)profile;
    (void)pj_board_http_start();
#ifdef ESP_PLATFORM
    esp_err_t ble_err = ble_provisioning_start();
    if (ble_err != ESP_OK) {
        g_status.ble_provisioning = PJ_BOARD_SERVICE_ERROR;
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "BLE provisioning start failed: %s", esp_err_to_name(ble_err));
        ESP_LOGE(TAG, "%s", g_status.last_error);
    }
    if (g_wifi_credentials_stored) {
        esp_err_t wifi_err = wifi_start_or_reconfigure();
        if (wifi_err != ESP_OK) {
            g_status.wifi = PJ_BOARD_SERVICE_ERROR;
            (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                           "Wi-Fi start failed: %s", esp_err_to_name(wifi_err));
            ESP_LOGE(TAG, "%s", g_status.last_error);
        }
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(serial_command_task_start());
#endif
}

pj_board_status_t pj_board_status(void)
{
#ifdef ESP_PLATFORM
    pj_board_status_t status = g_status;
    portENTER_CRITICAL(&g_recording_lock);
    status.recording_elapsed_ms = pj_recording_elapsed_ms(&g_recording);
    portEXIT_CRITICAL(&g_recording_lock);
    board_time_snapshot_t time = board_time_snapshot();
    status.hour = time.hour;
    status.minute = time.minute;
    status.year = time.year;
    status.month = time.month;
    status.day = time.day;
    status.time_set = time.time_set;
    connectivity_state_snapshot(&status.wifi_diagnostics, &status.time_sync);
    return status;
#else
    return g_status;
#endif
}

static int settings_apply_to_ui(pj_ui_context_t *ui, const pj_settings_t *settings)
{
    if (ui == NULL || settings == NULL) {
        return 0;
    }
    int changed = ui->volume != settings->volume ||
                  ui->dark_mode != settings->dark_mode ||
                  ui->alarm_on != settings->alarm_enabled ||
                  ui->alarm_hour != settings->alarm_hour ||
                  ui->alarm_minute != settings->alarm_minute ||
                  ui->timer_preset_seconds != settings->timer_seconds ||
                  ui->interval_preset_seconds != settings->interval_seconds ||
                  ui->clock_24h != settings->clock_24h ||
                  ui->temperature_fahrenheit != settings->temperature_fahrenheit ||
                  ui->transcript_font_size != settings->transcript_font_size;
    ui->volume = settings->volume;
    ui->dark_mode = settings->dark_mode;
    ui->alarm_on = settings->alarm_enabled;
    ui->alarm_hour = settings->alarm_hour;
    ui->alarm_minute = settings->alarm_minute;
    ui->timer_preset_seconds = settings->timer_seconds;
    ui->interval_preset_seconds = settings->interval_seconds;
    ui->clock_24h = settings->clock_24h;
    ui->temperature_fahrenheit = settings->temperature_fahrenheit;
    ui->transcript_font_size = settings->transcript_font_size;
    if (!ui->timer_running) {
        ui->timer_seconds = settings->timer_seconds;
    }
    if (!ui->interval_running) {
        ui->interval_seconds = settings->interval_seconds;
    }
    if (changed) {
        pj_ui_request_full_refresh(ui);
    }
    return changed;
}

void pj_board_refresh_settings(pj_ui_context_t *ui)
{
    pj_settings_t settings = g_settings;
#ifdef ESP_PLATFORM
    if (settings_take(portMAX_DELAY)) {
        settings = g_settings;
        settings_give();
    }
#endif
    (void)settings_apply_to_ui(ui, &settings);
}

int pj_board_store_settings_from_ui(const pj_ui_context_t *ui)
{
    if (ui == NULL) {
        return 0;
    }
    pj_settings_t updated = {
        .volume = ui->volume,
        .dark_mode = ui->dark_mode,
        .alarm_enabled = ui->alarm_on,
        .alarm_hour = ui->alarm_hour,
        .alarm_minute = ui->alarm_minute,
        .timer_seconds = ui->timer_preset_seconds,
        .interval_seconds = ui->interval_preset_seconds,
        .clock_24h = ui->clock_24h,
        .temperature_fahrenheit = ui->temperature_fahrenheit,
        .transcript_font_size = ui->transcript_font_size,
    };
    if (!pj_settings_valid(&updated)) {
        return -1;
    }
#ifdef ESP_PLATFORM
    if (!settings_take(portMAX_DELAY)) {
        return -1;
    }
    if (g_settings_update_pending) {
        settings_give();
        return 0;
    }
    if (memcmp(&updated, &g_settings, sizeof(updated)) == 0) {
        settings_give();
        return 0;
    }
    esp_err_t err = settings_save(&updated);
    int codec_volume = -1;
    if (err == ESP_OK) {
        g_settings = updated;
        codec_volume = pj_settings_codec_volume(updated.volume);
    } else {
        g_settings_update_pending = 1;
    }
    settings_give();
    if (err != ESP_OK) {
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "settings save failed: %s", esp_err_to_name(err));
        ESP_LOGW(TAG, "%s", g_status.last_error);
        return -1;
    }
    settings_apply_codec_volume(codec_volume);
    alert_audio_set_volume(codec_volume);
    ESP_LOGI(TAG, "Settings saved from UI: volume=%d theme=%s", updated.volume,
             updated.dark_mode ? "dark" : "light");
#else
    if (memcmp(&updated, &g_settings, sizeof(updated)) == 0) {
        return 0;
    }
    g_settings = updated;
#endif
    return 1;
}

int pj_board_consume_settings_update(pj_ui_context_t *ui)
{
    if (!g_settings_update_pending) {
        return 0;
    }
    g_settings_update_pending = 0;
    pj_board_refresh_settings(ui);
    pj_board_refresh_time_state(ui);
    return 1;
}

void pj_board_refresh_static_art(pj_ui_context_t *ui)
{
    if (ui == NULL || !static_art_take(portMAX_DELAY)) {
        return;
    }
    if (g_static_art_valid) {
        pj_ui_set_static_art(ui, g_static_art.pixels, sizeof(g_static_art.pixels));
    }
    static_art_give();
}

int pj_board_consume_static_art_update(pj_ui_context_t *ui)
{
    if (!g_static_art_update_pending || ui == NULL || !static_art_take(portMAX_DELAY)) {
        return 0;
    }
    g_static_art_update_pending = 0;
    if (g_static_art_valid) {
        pj_ui_set_static_art(ui, g_static_art.pixels, sizeof(g_static_art.pixels));
    }
    static_art_give();
    return 1;
}

void pj_board_refresh_home_layout(pj_ui_context_t *ui)
{
    if (ui == NULL || !home_layout_take(portMAX_DELAY)) {
        return;
    }
    (void)pj_ui_set_home_layout(ui, &g_home_layout);
    home_layout_give();
}

int pj_board_consume_home_layout_update(pj_ui_context_t *ui)
{
    if (!g_home_layout_update_pending || ui == NULL || !home_layout_take(portMAX_DELAY)) {
        return 0;
    }
    g_home_layout_update_pending = 0;
    int applied = pj_ui_set_home_layout(ui, &g_home_layout);
    home_layout_give();
    return applied;
}

void pj_board_refresh_status(pj_ui_context_t *ui)
{
#ifdef ESP_PLATFORM
    battery_refresh_status();
    shtc3_refresh_status();
    int controller_trusted = g_time_controller.wall_time_trusted;
    if (rtc_read_status_time() &&
        pj_time_controller_ready(&g_time_controller) && !controller_trusted) {
        board_time_mark_pending();
    }
    if (g_status.storage_mounted) {
        (void)storage_refresh_capacity();
    }
#endif
    pj_board_status_t status = pj_board_status();
    if (status.time_set) {
        pj_ui_set_time(ui, status.hour, status.minute, status.year, status.month, status.day);
    }
    pj_ui_set_audio_state(ui, g_status.recording, g_status.playback_active);
    pj_ui_set_status(ui, g_status.battery_percent, g_status.temperature_c,
                     g_status.humidity_percent);
#ifdef ESP_PLATFORM
    if (g_status.storage == PJ_BOARD_SERVICE_READY) {
        refresh_ui_notes_from_sd(ui);
        refresh_ui_sync_state(ui);
    }
#endif
}

void pj_board_refresh_notes(pj_ui_context_t *ui)
{
#ifdef ESP_PLATFORM
    if (ui != NULL && g_status.storage == PJ_BOARD_SERVICE_READY) {
        refresh_ui_notes_from_sd(ui);
        refresh_ui_sync_state(ui);
    }
#else
    (void)ui;
#endif
}

int pj_board_consume_audio_update(pj_ui_context_t *ui)
{
#ifdef ESP_PLATFORM
    if (!g_audio_state_update_pending || ui == NULL) {
        return 0;
    }
    g_audio_state_update_pending = 0;
    pj_ui_set_audio_state(ui, g_status.recording, g_status.playback_active);
    return 1;
#else
    (void)ui;
    return 0;
#endif
}

int pj_board_consume_notes_update(pj_ui_context_t *ui)
{
#ifdef ESP_PLATFORM
    if (!g_notes_update_pending || ui == NULL || g_status.storage != PJ_BOARD_SERVICE_READY) {
        return 0;
    }
    g_notes_update_pending = 0;
    refresh_ui_notes_from_sd(ui);
    return 1;
#else
    (void)ui;
    return 0;
#endif
}

int pj_board_tick_time(pj_ui_context_t *ui)
{
    board_time_snapshot_t before = board_time_snapshot();
    if (!before.time_set) {
        return 0;
    }
    if (!advance_status_one_minute(before.generation)) {
        return 0;
    }
    board_time_snapshot_t time = board_time_snapshot();
    pj_ui_set_time(ui, time.hour, time.minute, time.year, time.month, time.day);
    return 1;
}

int pj_board_consume_time_update(pj_ui_context_t *ui)
{
    board_time_snapshot_t time;
    if (!board_time_take_pending(&time)) {
        return 0;
    }
    pj_ui_set_time(ui, time.hour, time.minute, time.year, time.month, time.day);
#ifdef ESP_PLATFORM
    if (pj_time_controller_ready(&g_time_controller)) {
        pj_time_controller_result_t result;
        if (!pj_time_controller_time_changed(
                &g_time_controller, time_controller_wall_time_trusted(NULL),
                &result)) {
            board_time_mark_pending();
        }
        (void)time_state_project(ui);
    }
#endif
    return 1;
}

void pj_board_refresh_time_state(pj_ui_context_t *ui)
{
#ifdef ESP_PLATFORM
    if (!pj_time_controller_ready(&g_time_controller)) {
        return;
    }
    pj_time_controller_result_t result;
    (void)pj_time_controller_update(&g_time_controller, &result);
    (void)time_state_project(ui);
#else
    (void)ui;
#endif
}

#ifdef ESP_PLATFORM
static int time_controller_command_from_ui(
    const pj_ui_time_command_t *source, pj_time_controller_command_t *target)
{
    static const pj_time_controller_command_type_t types[] = {
        [PJ_UI_TIME_COMMAND_ALERT_DISMISS] =
            PJ_TIME_CONTROLLER_COMMAND_ALERT_DISMISS,
        [PJ_UI_TIME_COMMAND_ALARM_SNOOZE] =
            PJ_TIME_CONTROLLER_COMMAND_ALARM_SNOOZE,
        [PJ_UI_TIME_COMMAND_RECOVERY_ACKNOWLEDGE] =
            PJ_TIME_CONTROLLER_COMMAND_RECOVERY_ACKNOWLEDGE,
        [PJ_UI_TIME_COMMAND_STOPWATCH_START] =
            PJ_TIME_CONTROLLER_COMMAND_STOPWATCH_START,
        [PJ_UI_TIME_COMMAND_STOPWATCH_PAUSE] =
            PJ_TIME_CONTROLLER_COMMAND_STOPWATCH_PAUSE,
        [PJ_UI_TIME_COMMAND_STOPWATCH_RESET] =
            PJ_TIME_CONTROLLER_COMMAND_STOPWATCH_RESET,
        [PJ_UI_TIME_COMMAND_TIMER_START] =
            PJ_TIME_CONTROLLER_COMMAND_TIMER_START,
        [PJ_UI_TIME_COMMAND_TIMER_PAUSE] =
            PJ_TIME_CONTROLLER_COMMAND_TIMER_PAUSE,
        [PJ_UI_TIME_COMMAND_TIMER_RESET] =
            PJ_TIME_CONTROLLER_COMMAND_TIMER_RESET,
        [PJ_UI_TIME_COMMAND_INTERVAL_START] =
            PJ_TIME_CONTROLLER_COMMAND_INTERVAL_START,
        [PJ_UI_TIME_COMMAND_INTERVAL_PAUSE] =
            PJ_TIME_CONTROLLER_COMMAND_INTERVAL_PAUSE,
        [PJ_UI_TIME_COMMAND_INTERVAL_RESET] =
            PJ_TIME_CONTROLLER_COMMAND_INTERVAL_RESET,
    };
    if (source == NULL || target == NULL || source->type <= 0 ||
        (size_t)source->type >= sizeof(types) / sizeof(types[0]) ||
        types[source->type] == 0) {
        return 0;
    }
    *target = (pj_time_controller_command_t) {
        .type = types[source->type],
        .alert_id = source->alert_id,
        .duration_ms = source->duration_ms,
        .secondary_duration_ms = source->secondary_duration_ms,
    };
    return 1;
}
#endif

int pj_board_apply_time_actions(pj_ui_context_t *ui)
{
#ifdef ESP_PLATFORM
    if (!pj_time_controller_ready(&g_time_controller) || ui == NULL) {
        return 0;
    }
    pj_ui_time_command_t ui_command;
    pj_time_controller_command_t command;
    if (!pj_ui_consume_time_command(ui, &ui_command) ||
        !time_controller_command_from_ui(&ui_command, &command)) {
        return 0;
    }
    pj_time_controller_result_t result;
    (void)pj_time_controller_apply(&g_time_controller, &command, &result);
    (void)time_state_project(ui);
    return result.state_changed;
#else
    (void)ui;
    return 0;
#endif
}

int pj_board_update_time_state(pj_ui_context_t *ui)
{
#ifdef ESP_PLATFORM
    if (!pj_time_controller_ready(&g_time_controller) || ui == NULL) {
        return 0;
    }
    pj_time_controller_result_t result;
    (void)pj_time_controller_update(&g_time_controller, &result);
    (void)time_state_project(ui);
    return pj_ui_is_dirty(ui);
#else
    (void)ui;
    return 0;
#endif
}

int pj_board_display_framebuffer(const pj_framebuffer_t *fb, const pj_ui_dirty_region_t *dirty)
{
#ifdef ESP_PLATFORM
    if (!g_display_ready || g_epd_spi == NULL) {
        if (!g_display_warning_logged) {
            ESP_LOGW(TAG, "Display refresh skipped: display not initialized");
            g_display_warning_logged = 1;
        }
        return 0;
    }
    if (dirty != NULL && dirty->partial && dirty->width > 0 && dirty->height > 0 && g_epd_shadow_valid) {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        int byte_len = framebuffer_region_to_epd(fb, dirty, &x0, &y0, &x1, &y1);
        if (byte_len > 0) {
            uint16_t mem_y_start = (uint16_t)(PJ_DISPLAY_HEIGHT - 1 - y0);
            uint16_t mem_y_end = (uint16_t)(PJ_DISPLAY_HEIGHT - 1 - y1);
            if (!g_epd_partial_ready) {
                epd_set_windows((uint16_t)x0, mem_y_start, (uint16_t)x1, mem_y_end);
                epd_set_cursor((uint16_t)(x0 >> 3), mem_y_start);
            }
            epd_prepare_partial();
            ESP_LOGI(TAG, "Display partial refresh x=%d y=%d w=%d h=%d bytes=%d ram=0x24",
                     x0, y0, x1 - x0 + 1, y1 - y0 + 1, byte_len);
            epd_set_windows((uint16_t)x0, mem_y_start, (uint16_t)x1, mem_y_end);
            epd_set_cursor((uint16_t)(x0 >> 3), mem_y_start);
            /* Waveshare's 1.54 V2 partial path writes only the new RAM plane here.
             * The full/base refresh seeds both 0x24 and 0x26; rewriting 0x26 per
             * patch causes global contrast shifts on this panel.
             */
            ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x24));
            ESP_ERROR_CHECK_WITHOUT_ABORT(epd_write_bytes(g_epd_buffer, byte_len));
            epd_turn_on_display_part();
            g_epd_shadow_fb = *fb;
            return 1;
        }
    }
    ESP_LOGI(TAG, "Display full refresh");
    g_epd_partial_ready = 0;
    epd_set_lut(WF_FULL_1IN54);
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x11));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x01));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x3C));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_data(0x01));
    framebuffer_to_epd(fb);
    epd_set_windows(0, PJ_DISPLAY_WIDTH - 1, PJ_DISPLAY_HEIGHT - 1, 0);
    epd_set_cursor(0, PJ_DISPLAY_HEIGHT - 1);
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x24));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_write_bytes(g_epd_buffer, PJ_FRAMEBUFFER_BYTES));
    epd_set_cursor(0, PJ_DISPLAY_HEIGHT - 1);
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_send_command(0x26));
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_write_bytes(g_epd_buffer, PJ_FRAMEBUFFER_BYTES));
    epd_turn_on_display();
    g_epd_shadow_fb = *fb;
    g_epd_shadow_valid = 1;
    return 1;
#else
    (void)fb;
    (void)dirty;
    return 0;
#endif
}

int pj_board_poll_event(pj_board_event_t *event)
{
    if (event == NULL) {
        return 0;
    }
    event->type = PJ_BOARD_EVENT_NONE;
    event->x = 0;
    event->y = 0;
#ifdef ESP_PLATFORM
    int level = gpio_get_level(BOOT_BUTTON_PIN);
    TickType_t now = xTaskGetTickCount();
    uint32_t now_ms = (uint32_t)(now * portTICK_PERIOD_MS);
    pj_aux_gesture_t gesture = pj_aux_input_update(&g_aux_input, level, now_ms);
    switch (gesture) {
    case PJ_AUX_GESTURE_SHORT:
        event->type = PJ_BOARD_EVENT_AUX_SHORT;
        break;
    case PJ_AUX_GESTURE_LONG:
        event->type = PJ_BOARD_EVENT_AUX_LONG;
        break;
    case PJ_AUX_GESTURE_DOUBLE:
        event->type = PJ_BOARD_EVENT_AUX_DOUBLE;
        break;
    case PJ_AUX_GESTURE_NONE:
    default:
        break;
    }
    if (event->type != PJ_BOARD_EVENT_NONE) {
        ESP_LOGI(TAG, "AUX %s", event->type == PJ_BOARD_EVENT_AUX_SHORT ? "short" :
                 event->type == PJ_BOARD_EVENT_AUX_LONG ? "long" : "double");
        return 1;
    }

    if (g_board_event_queue != NULL && xQueueReceive(g_board_event_queue, event, 0) == pdTRUE) {
        return 1;
    }
#endif
    return 0;
}

void pj_board_enter_sleep(void)
{
#ifdef ESP_PLATFORM
    int rtc_schedule = rtc_wake_sync();
    uint64_t wake_delay_ms = UINT64_MAX;
    const pj_time_state_t *time_state =
        pj_time_controller_state(&g_time_controller);
    if (time_state != NULL) {
        pj_time_clock_t clock;
        if (board_time_model_clock(&clock)) {
            wake_delay_ms = pj_time_next_wake_delay_ms(time_state, &clock);
        }
    }
    if (wake_delay_ms == 0) {
        ESP_LOGI(TAG, "Sleep deferred because a time alert is due");
        return;
    }
    if (wake_delay_ms != UINT64_MAX) {
        esp_err_t err = esp_sleep_enable_timer_wakeup(wake_delay_ms * 1000u);
        if (err == ESP_OK) {
            g_timer_wakeup_enabled = 1;
            ESP_LOGI(TAG, "Internal fallback wake armed in %llu ms",
                     (unsigned long long)wake_delay_ms);
        } else if (rtc_schedule <= 0) {
            ESP_LOGW(TAG, "Sleep deferred; no scheduled wake source: %s",
                     esp_err_to_name(err));
            return;
        } else {
            ESP_LOGW(TAG, "Internal fallback wake unavailable: %s", esp_err_to_name(err));
        }
    } else if (g_timer_wakeup_enabled) {
        (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
        g_timer_wakeup_enabled = 0;
    }
    if (g_rtc_wake_plan.state == PJ_RTC_WAKE_ARMED &&
        gpio_get_level(RTC_INT_PIN) == 0) {
        ESP_LOGI(TAG, "Sleep deferred because RTC_INT is already active");
        return;
    }
    if (g_display_ready) {
        gpio_set_level(EPD_PWR_PIN, 1);
        g_epd_shadow_valid = 0;
        g_epd_partial_ready = 0;
    }
    ESP_LOGI(TAG, "Entering light sleep; BOOT, RTC_INT, or timer wake the device");
    esp_err_t sleep_err = gpio_wakeup_enable(BOOT_BUTTON_PIN, GPIO_INTR_LOW_LEVEL);
    if (sleep_err == ESP_OK) {
        sleep_err = esp_sleep_enable_gpio_wakeup();
    }
    if (sleep_err == ESP_OK) {
        sleep_err = esp_light_sleep_start();
    }
    if (sleep_err != ESP_OK) {
        gpio_set_level(EPD_PWR_PIN, 0);
        ESP_LOGW(TAG, "Light sleep rejected: %s", esp_err_to_name(sleep_err));
        return;
    }
    uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    int timer_wake = (wake_causes & (1u << ESP_SLEEP_WAKEUP_TIMER)) != 0;
    int rtc_wake = (wake_causes & (1u << ESP_SLEEP_WAKEUP_EXT1)) != 0;
    esp_err_t timer_disable = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    if (timer_disable != ESP_OK && timer_disable != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Timer wake disable failed: %s", esp_err_to_name(timer_disable));
    }
    g_timer_wakeup_enabled = 0;
    esp_err_t gpio_restore = rtc_gpio_hold_dis(RTC_INT_PIN);
    if (gpio_restore == ESP_OK) {
        gpio_restore = rtc_gpio_deinit(RTC_INT_PIN);
    }
    if (gpio_restore == ESP_OK) {
        gpio_restore = gpio_set_pull_mode(RTC_INT_PIN, GPIO_PULLUP_ONLY);
    }
    if (gpio_restore != ESP_OK) {
        ESP_LOGW(TAG, "RTC GPIO5 restore failed: %s", esp_err_to_name(gpio_restore));
    }
    uint8_t rtc_flags = 0;
    pj_rtc_wake_result_t clear_result = rtc_wake_disarm_board(&rtc_flags, 1);
    if (clear_result != PJ_RTC_WAKE_OK) {
        ESP_LOGW(TAG, "RTC wake clear failed at %s", rtc_wake_result_name(clear_result));
    }
    int time_changed = rtc_read_status_time();
    if (pj_time_controller_ready(&g_time_controller)) {
        pj_time_controller_result_t result;
        if (time_changed) {
            if (!pj_time_controller_time_changed(
                    &g_time_controller, time_controller_wall_time_trusted(NULL),
                    &result)) {
                board_time_mark_pending();
            }
        } else {
            (void)pj_time_controller_update(&g_time_controller, &result);
        }
    }
    uint32_t wake_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (gpio_get_level(BOOT_BUTTON_PIN) == 0) {
        pj_aux_input_resume_pressed(&g_aux_input, wake_ms);
    } else {
        pj_aux_input_init(&g_aux_input, 1, wake_ms);
    }
    gpio_set_level(EPD_PWR_PIN, 0);
    const char *source = rtc_wake ? "RTC_INT" : timer_wake ? "timer" : "GPIO";
    ESP_LOGI(TAG, "Woke from light sleep via %s (RTC flags=0x%02x)%s",
             source, rtc_flags,
             rtc_wake &&
             (rtc_flags & PJ_RTC_WAKE_CONTROL2_AF) == 0 ? " spurious" : "");
#endif
}


int pj_board_record_set_active(int active)
{
    active = active != 0;
    if (!active) {
#ifdef ESP_PLATFORM
        if (!g_status.recording) {
            return 1;
        }
        g_record_stop_requested = 1;
        recording_state_request_stop();
        ESP_LOGI(TAG, "Recording stop requested");
#else
        g_status.recording = 0;
#endif
        return 1;
    }
    if (g_status.storage != PJ_BOARD_SERVICE_READY || g_status.audio != PJ_BOARD_SERVICE_READY) {
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG, "Record command ignored: storage/audio unavailable");
#endif
        return 0;
    }
#ifdef ESP_PLATFORM
    if (g_status.recording) {
        return 1;
    }
    if (g_status.playback_active || g_playback_task != NULL) {
        ESP_LOGW(TAG, "Record start ignored: playback active");
        return 0;
    }
    if (g_audio_process_task != NULL) {
        ESP_LOGW(TAG, "Record start ignored: previous recording still processing");
        return 0;
    }
    if (g_record_task != NULL) {
        ESP_LOGW(TAG, "Record start ignored: previous recording still stopping");
        return 0;
    }
    if (!storage_preflight(PJ_STORAGE_RECORD_START_BYTES, "recording")) {
        ESP_LOGW(TAG, "Record start ignored: %s", g_status.last_error);
        return 0;
    }
    next_recording_path(g_active_recording_path, sizeof(g_active_recording_path));
    g_record_stop_requested = 0;
    if (!recording_state_start()) {
        ESP_LOGE(TAG, "Record start failed: invalid lifecycle transition");
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "recording lifecycle was busy");
        return 0;
    }
    alert_audio_set_recording(1);
    board_audio_state_set(1, -1);
    BaseType_t created = xTaskCreate(record_task, "pj-record", PJ_AUDIO_RECORD_TASK_STACK, NULL, 5, &g_record_task);
    if (created != pdPASS) {
        board_audio_state_set(0, -1);
        alert_audio_set_recording(0);
        g_record_task = NULL;
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "recording task creation failed");
        recording_state_finish_capture(0);
        recording_publish_completion();
        ESP_LOGE(TAG, "Record start failed: task create failed");
        return 0;
    }
    ESP_LOGI(TAG, "Recording started: %s", g_active_recording_path);
#else
    g_status.recording = 1;
#endif
    return 1;
}

int pj_board_record_toggle(void)
{
    return pj_board_record_set_active(!g_status.recording);
}

int pj_board_playback_toggle(void)
{
    return pj_board_playback_toggle_index(0);
}

int pj_board_playback_set_active(int active, int note_index)
{
    active = active != 0;
    if (!active) {
#ifdef ESP_PLATFORM
        if (!g_status.playback_active) {
            return 1;
        }
        g_playback_stop_requested = 1;
        ESP_LOGI(TAG, "Playback stop requested");
#else
        g_status.playback_active = 0;
#endif
        return 1;
    }
    if (g_status.storage != PJ_BOARD_SERVICE_READY || g_status.audio != PJ_BOARD_SERVICE_READY) {
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG, "Playback command ignored: storage/audio unavailable");
#endif
        return 0;
    }
#ifdef ESP_PLATFORM
    if (g_status.playback_active) {
        return 1;
    }
    if (g_status.recording || g_record_task != NULL || g_audio_process_task != NULL ||
        alert_audio_desired()) {
        ESP_LOGW(TAG, "Playback start ignored: recording or audio processing active");
        return 0;
    }
    if (g_playback_task != NULL) {
        ESP_LOGW(TAG, "Playback start ignored: previous playback still stopping");
        return 0;
    }
    if (note_index < 0) {
        note_index = 0;
    }
    char filename[96];
    if (!audio_filename_for_index(note_index, filename, sizeof(filename))) {
        ESP_LOGW(TAG, "Playback start ignored: WAV index %d unavailable", note_index);
        return 0;
    }
    (void)snprintf(g_active_playback_path, sizeof(g_active_playback_path), PJ_AUDIO_DIR "/%s", filename);
    g_playback_stop_requested = 0;
    board_audio_state_set(-1, 1);
    BaseType_t created = xTaskCreate(playback_task, "pj-play", PJ_AUDIO_PLAYBACK_TASK_STACK, NULL, 5, &g_playback_task);
    if (created != pdPASS) {
        board_audio_state_set(-1, 0);
        g_playback_task = NULL;
        ESP_LOGE(TAG, "Playback start failed: task create failed");
        return 0;
    }
    ESP_LOGI(TAG, "Playback started: %s", g_active_playback_path);
#else
    (void)note_index;
    g_status.playback_active = 1;
#endif
    return 1;
}

int pj_board_playback_toggle_index(int note_index)
{
    return pj_board_playback_set_active(!g_status.playback_active, note_index);
}

int pj_board_wipe_recordings(pj_ui_context_t *ui)
{
#ifdef ESP_PLATFORM
    if (g_status.storage != PJ_BOARD_SERVICE_READY) {
        ESP_LOGW(TAG, "Wipe recordings ignored: storage unavailable");
        return -1;
    }
    if (g_status.recording || g_record_task != NULL || g_audio_process_task != NULL ||
        g_status.playback_active || g_playback_task != NULL) {
        ESP_LOGW(TAG, "Wipe recordings ignored: audio task active");
        return -2;
    }
    int audio_deleted = delete_dir_entries(PJ_AUDIO_DIR, is_audio_filename);
    int transcripts_deleted = delete_dir_entries(PJ_TRANSCRIPT_DIR, is_transcript_filename);
    int notes_deleted = delete_dir_entries(PJ_NOTE_DIR, is_transcript_filename);
    g_ui_note_audio_count = 0;
    g_ui_note_transcript_view = 0;
    g_notes_update_pending = 1;
    if (ui != NULL) {
        refresh_ui_notes_from_sd(ui);
    }
    ESP_LOGI(TAG, "Wiped recordings: audio=%d transcripts=%d metadata=%d",
             audio_deleted, transcripts_deleted, notes_deleted);
    return audio_deleted;
#else
    char labels[PJ_UI_MAX_NOTES][PJ_UI_NOTE_LABEL_LEN] = {0};
    if (ui != NULL) {
        pj_ui_set_notes(ui, 0, labels);
    }
    return 0;
#endif
}

int pj_board_storage_recover(void)
{
#ifdef ESP_PLATFORM
    if (g_status.recording || g_record_task != NULL || g_audio_process_task != NULL ||
        g_status.playback_active || g_playback_task != NULL) {
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "storage recovery blocked while audio is active");
        return 0;
    }
    if (g_sd_card != NULL) {
        esp_err_t unmount_err = esp_vfs_fat_sdcard_unmount(g_status.storage_path, g_sd_card);
        if (unmount_err != ESP_OK) {
            (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                           "microSD unmount failed: %s", esp_err_to_name(unmount_err));
            return 0;
        }
        g_sd_card = NULL;
    }
    g_status.storage_mounted = 0;
    g_status.storage = PJ_BOARD_SERVICE_UNAVAILABLE;
    g_status.storage_health = PJ_STORAGE_HEALTH_UNMOUNTED;
    esp_err_t err = storage_init();
    if (err != ESP_OK) {
        g_status.storage = PJ_BOARD_SERVICE_ERROR;
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "microSD recovery failed: %s; check card and filesystem",
                       esp_err_to_name(err));
        return 0;
    }
    g_status.storage = PJ_BOARD_SERVICE_READY;
    g_status.last_error[0] = '\0';
    if (static_art_take(portMAX_DELAY)) {
        g_static_art_valid = 0;
        g_static_art_slot = -1;
        static_art_load();
        static_art_give();
    }
    if (!g_audio_ready) {
        esp_err_t audio_err = audio_init();
        if (audio_err == ESP_OK) {
            g_status.audio = PJ_BOARD_SERVICE_READY;
        } else {
            g_status.audio = PJ_BOARD_SERVICE_ERROR;
            (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                           "storage recovered but audio init failed: %s",
                           esp_err_to_name(audio_err));
        }
    }
    g_notes_update_pending = 1;
    return 1;
#else
    return 0;
#endif
}

#ifdef ESP_PLATFORM
static int auth_ok(httpd_req_t *req)
{
    char header[96];
    if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) != ESP_OK) {
        return 0;
    }
    return pj_auth_header_valid(header, g_status.token);
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t require_auth(httpd_req_t *req)
{
    if (auth_ok(req)) {
        return ESP_OK;
    }
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer realm=\"pocket-journal\"");
    return send_json(req, "{\"error\":\"unauthorized\"}");
}

static void drain_body(httpd_req_t *req)
{
    char scratch[128];
    int remaining = req->content_len;
    while (remaining > 0) {
        int got = httpd_req_recv(req, scratch, remaining > (int)sizeof(scratch) ? (int)sizeof(scratch) : remaining);
        if (got <= 0) {
            return;
        }
        remaining -= got;
    }
}

static int read_body(httpd_req_t *req, char *body, size_t body_size)
{
    if (body_size == 0) {
        return 0;
    }
    int remaining = req->content_len;
    size_t used = 0;
    while (remaining > 0 && used + 1 < body_size) {
        int limit = remaining;
        if (limit > (int)(body_size - used - 1)) {
            limit = (int)(body_size - used - 1);
        }
        int got = httpd_req_recv(req, body + used, limit);
        if (got <= 0) {
            body[used] = '\0';
            return 0;
        }
        used += (size_t)got;
        remaining -= got;
    }
    body[used] = '\0';
    if (remaining > 0) {
        char scratch[64];
        while (remaining > 0) {
            int got = httpd_req_recv(req, scratch, remaining > (int)sizeof(scratch) ? (int)sizeof(scratch) : remaining);
            if (got <= 0) {
                break;
            }
            remaining -= got;
        }
    }
    return 1;
}

static int json_int_field(const char *json, const char *field, int *value)
{
    char needle[32];
    (void)snprintf(needle, sizeof(needle), "\"%s\"", field);
    const char *cursor = strstr(json, needle);
    if (cursor == NULL) {
        return 0;
    }
    cursor = strchr(cursor + strlen(needle), ':');
    if (cursor == NULL) {
        return 0;
    }
    cursor++;
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    char *end = NULL;
    long parsed = strtol(cursor, &end, 10);
    if (end == cursor) {
        return 0;
    }
    *value = (int)parsed;
    return 1;
}

static int board_set_time_date(int hour, int minute, int year, int month, int day,
                               const char *source)
{
    if (!valid_time_date(hour, minute, year, month, day)) {
        ESP_LOGE(TAG, "Rejected invalid time/date from %s", source);
        return 0;
    }
    esp_err_t err = rtc_write_civil_time(hour, minute, year, month, day, 0);
    if (err != ESP_OK) {
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "RTC write failed for %s time update: %s",
                       source, esp_err_to_name(err));
        ESP_LOGW(TAG, "%s", g_status.last_error);
        return 0;
    }
    if (!board_time_publish(hour, minute, year, month, day, 0,
                            board_monotonic_ms(), 1)) {
        ESP_LOGE(TAG, "Rejected invalid time/date from %s", source);
        return 0;
    }
    portENTER_CRITICAL(&g_connectivity_lock);
    g_time_sync_state.time_known = 1;
    portEXIT_CRITICAL(&g_connectivity_lock);
    board_time_mark_pending();
    ESP_LOGI(TAG, "Time/date updated from %s: %02d:%02d %04d-%02d-%02d",
             source, hour, minute, year, month, day);
    return 1;
}

static void serial_trim_line(char *line)
{
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t')) {
        line[--len] = '\0';
    }
}

static int serial_parse_int(const char *value, int *out)
{
    if (value == NULL || *value == '\0') {
        return 0;
    }
    char *end = NULL;
    long parsed = strtol(value, &end, 0);
    if (end == value || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX) {
        return 0;
    }
    *out = (int)parsed;
    return 1;
}

static int serial_parse_audio_tone_arg(const char *key, const char *value, audio_tone_diag_opts_t *opts)
{
    int parsed = PJ_AUDIO_TONE_DEFAULT_INT;
    if (strcmp(value, "-") == 0 || strcmp(value, "default") == 0) {
        parsed = PJ_AUDIO_TONE_DEFAULT_INT;
    } else if (!serial_parse_int(value, &parsed)) {
        return 0;
    }

    if (strcmp(key, "pa") == 0) {
        if (parsed != PJ_AUDIO_TONE_DEFAULT_INT && parsed != 0 && parsed != 1) {
            return 0;
        }
        opts->pa_level = parsed;
        return 1;
    }
    if (strcmp(key, "dout") == 0 || strcmp(key, "dout_gpio") == 0) {
        if (parsed < PJ_AUDIO_TONE_DEFAULT_INT) {
            return 0;
        }
        opts->dout_gpio = parsed;
        return 1;
    }
    if (strcmp(key, "pwr") == 0 || strcmp(key, "power") == 0 || strcmp(key, "audio_power") == 0) {
        if (parsed != PJ_AUDIO_TONE_DEFAULT_INT && parsed != 0 && parsed != 1) {
            return 0;
        }
        opts->audio_power_level = parsed;
        return 1;
    }
    if (strcmp(key, "gpio44") == 0 || strcmp(key, "reg44") == 0) {
        if (parsed < PJ_AUDIO_TONE_DEFAULT_INT || parsed > 0xFF) {
            return 0;
        }
        opts->codec_gpio44 = parsed;
        return 1;
    }
    if (strcmp(key, "gp45") == 0 || strcmp(key, "reg45") == 0) {
        if (parsed < PJ_AUDIO_TONE_DEFAULT_INT || parsed > 0xFF) {
            return 0;
        }
        opts->codec_gp45 = parsed;
        return 1;
    }
    return 0;
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static int hex_decode_string(const char *hex, char *out, size_t out_size)
{
    size_t hex_len = strlen(hex);
    if ((hex_len % 2) != 0 || out_size == 0 || (hex_len / 2) >= out_size) {
        return 0;
    }
    for (size_t i = 0; i < hex_len; i += 2) {
        int high = hex_value(hex[i]);
        int low = hex_value(hex[i + 1]);
        if (high < 0 || low < 0) {
            return 0;
        }
        out[i / 2] = (char)((high << 4) | low);
    }
    out[hex_len / 2] = '\0';
    return 1;
}

static int connectivity_add_json(cJSON *json, const pj_board_status_t *status)
{
    cJSON *wifi = cJSON_AddObjectToObject(json, "wifi_diagnostics");
    cJSON *time_sync = cJSON_AddObjectToObject(json, "time_sync");
    if (wifi == NULL || time_sync == NULL) {
        return 0;
    }

    const pj_wifi_state_t *wifi_state = &status->wifi_diagnostics;
    cJSON_AddBoolToObject(wifi, "provisioned", wifi_state->provisioned != 0);
    cJSON_AddStringToObject(wifi, "state", service_name(status->wifi));
    cJSON_AddStringToObject(wifi, "phase",
                            pj_wifi_phase_name(wifi_state->phase));
    cJSON_AddStringToObject(wifi, "ip", status->ip_addr);
    cJSON_AddStringToObject(wifi, "dhcp_state",
                            pj_wifi_dhcp_state_name(wifi_state->dhcp_state));
    if (wifi_state->last_disconnect_reason == 0) {
        cJSON_AddNullToObject(wifi, "last_disconnect_reason");
    } else {
        cJSON_AddNumberToObject(wifi, "last_disconnect_reason",
                                wifi_state->last_disconnect_reason);
    }
    cJSON_AddStringToObject(wifi, "retry_state",
                            pj_wifi_retry_state_name(wifi_state->retry_state));
    cJSON_AddNumberToObject(wifi, "retry_count", wifi_state->retry_count);
    cJSON_AddNumberToObject(wifi, "backoff_ms", wifi_state->backoff_ms);
    if (wifi_state->ap_visible < 0) {
        cJSON_AddNullToObject(wifi, "ap_visible");
    } else {
        cJSON_AddBoolToObject(wifi, "ap_visible",
                              wifi_state->ap_visible != 0);
    }
    if (wifi_state->has_ip) {
        cJSON_AddNumberToObject(wifi, "rssi_dbm", wifi_state->rssi_dbm);
        cJSON_AddNumberToObject(wifi, "channel", wifi_state->channel);
    } else {
        cJSON_AddNullToObject(wifi, "rssi_dbm");
        cJSON_AddNullToObject(wifi, "channel");
    }
    char wifi_success[32] = {0};
    if (format_utc_time(wifi_state->last_success_utc_s, wifi_success,
                        sizeof(wifi_success))) {
        cJSON_AddStringToObject(wifi, "last_success_utc", wifi_success);
    } else {
        cJSON_AddNullToObject(wifi, "last_success_utc");
    }

    const pj_time_sync_state_t *sync_state = &status->time_sync;
    cJSON_AddStringToObject(time_sync, "state",
                            pj_time_sync_state_name(sync_state->state));
    cJSON_AddStringToObject(time_sync, "failure",
                            pj_time_sync_failure_name(sync_state->failure));
    cJSON_AddStringToObject(time_sync, "publication",
                            pj_time_sync_publication_name(sync_state->publication));
    cJSON_AddStringToObject(time_sync, "correction",
                            pj_time_sync_correction_name(sync_state->correction));
    cJSON_AddStringToObject(time_sync, "system_clock", "utc");
    cJSON_AddStringToObject(time_sync, "civil_time_semantics", "local");
    cJSON_AddStringToObject(time_sync, "server", "pool.ntp.org");
    cJSON_AddBoolToObject(time_sync, "has_ip", sync_state->has_ip != 0);
    cJSON_AddNumberToObject(time_sync, "attempt_count",
                            sync_state->attempt_count);
    cJSON_AddNumberToObject(time_sync, "backoff_ms", sync_state->backoff_ms);
    cJSON_AddNumberToObject(time_sync, "last_offset_ms",
                            (double)sync_state->last_offset_ms);
    char sync_success[32] = {0};
    if (format_utc_time(sync_state->last_success_utc_s, sync_success,
                        sizeof(sync_success))) {
        cJSON_AddStringToObject(time_sync, "last_success_utc", sync_success);
    } else {
        cJSON_AddNullToObject(time_sync, "last_success_utc");
    }
    return 1;
}

static void serial_print_status(void)
{
    int pending_sync = 0;
    int transferred_sync = 0;
    collect_sync_counts(&pending_sync, &transferred_sync);
    pj_board_status_t status = pj_board_status();
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        printf("PJ_ERR {\"error\":\"status allocation failed\"}\n");
        return;
    }
    cJSON_AddStringToObject(json, "device_id", status.device_id);
    cJSON_AddStringToObject(json, "storage", service_name(status.storage));
    cJSON_AddStringToObject(json, "audio", service_name(status.audio));
    cJSON_AddBoolToObject(json, "time_set", status.time_set != 0);
    cJSON_AddBoolToObject(json, "wifi_provisioned",
                          status.wifi_diagnostics.provisioned != 0);
    cJSON_AddStringToObject(json, "wifi", service_name(status.wifi));
    cJSON_AddStringToObject(json, "ip", status.ip_addr);
    cJSON_AddNumberToObject(json, "pending_sync", pending_sync);
    cJSON_AddNumberToObject(json, "transferred_sync", transferred_sync);
    if (!connectivity_add_json(json, &status)) {
        cJSON_Delete(json);
        printf("PJ_ERR {\"error\":\"status allocation failed\"}\n");
        return;
    }
    char *encoded = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (encoded == NULL) {
        printf("PJ_ERR {\"error\":\"status encoding failed\"}\n");
        return;
    }
    printf("PJ_OK %s\n", encoded);
    cJSON_free(encoded);
}

static void serial_command_task(void *arg)
{
    (void)arg;
    char line[512] = {0};
    size_t line_length = 0;
    int discarding_overflow = 0;
    setvbuf(stdin, NULL, _IONBF, 0);
    while (1) {
        char *chunk = discarding_overflow ? line : line + line_length;
        size_t available = discarding_overflow ? sizeof(line) : sizeof(line) - line_length;
        if (fgets(chunk, available, stdin) == NULL) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        size_t chunk_length = strlen(chunk);
        int line_complete = chunk_length > 0 &&
            (chunk[chunk_length - 1] == '\n' || chunk[chunk_length - 1] == '\r');
        if (discarding_overflow) {
            if (line_complete) {
                discarding_overflow = 0;
            }
            continue;
        }
        line_length += chunk_length;
        if (!line_complete) {
            if (line_length + 1 < sizeof(line)) {
                continue;
            }
            printf("PJ_ERR {\"error\":\"USB command too long\"}\n");
            fflush(stdout);
            line[0] = '\0';
            line_length = 0;
            discarding_overflow = 1;
            continue;
        }
        serial_trim_line(line);
        line_length = 0;
        if (line[0] == '\0') {
            continue;
        }
        if (strcmp(line, "PJ_STATUS") == 0) {
            serial_print_status();
            fflush(stdout);
            continue;
        }
        if (strcmp(line, "PJ_WIPE_RECORDINGS") == 0) {
            int deleted = pj_board_wipe_recordings(NULL);
            if (deleted >= 0) {
                printf("PJ_OK {\"deleted\":%d}\n", deleted);
            } else if (deleted == -2) {
                printf("PJ_ERR {\"error\":\"audio task active\"}\n");
            } else {
                printf("PJ_ERR {\"error\":\"storage unavailable\"}\n");
            }
            fflush(stdout);
            continue;
        }
        if (strcmp(line, "PJ_HOME_RESET") == 0) {
            pj_home_layout_t fallback;
            pj_home_layout_defaults(&fallback);
            esp_err_t err = home_layout_replace(&fallback);
            if (err == ESP_OK) {
                printf("PJ_OK {\"home_reset\":true}\n");
            } else {
                printf("PJ_ERR {\"error\":\"home reset failed: %s\"}\n", esp_err_to_name(err));
            }
            fflush(stdout);
            continue;
        }
        if (strncmp(line, "PJ_AUDIO_TONE", 13) == 0) {
            audio_tone_diag_opts_t tone_opts = {
                .pa_level = PJ_AUDIO_TONE_DEFAULT_INT,
                .dout_gpio = PJ_AUDIO_TONE_DEFAULT_INT,
                .audio_power_level = PJ_AUDIO_TONE_DEFAULT_INT,
                .codec_gpio44 = PJ_AUDIO_TONE_DEFAULT_INT,
                .codec_gp45 = PJ_AUDIO_TONE_DEFAULT_INT,
            };
            int positional_index = 0;
            int parse_ok = 1;
            char *arg = line + 13;
            while (*arg == ' ' || *arg == '\t') {
                arg++;
            }

            while (*arg != '\0') {
                char *token = arg;
                while (*arg != '\0' && *arg != ' ' && *arg != '\t') {
                    arg++;
                }
                if (*arg != '\0') {
                    *arg++ = '\0';
                }
                while (*arg == ' ' || *arg == '\t') {
                    arg++;
                }

                char *equals = strchr(token, '=');
                if (equals != NULL) {
                    *equals++ = '\0';
                    parse_ok = serial_parse_audio_tone_arg(token, equals, &tone_opts);
                } else {
                    const char *pos_key = positional_index == 0 ? "pa" : positional_index == 1 ? "dout" : "";
                    parse_ok = pos_key[0] != '\0' && serial_parse_audio_tone_arg(pos_key, token, &tone_opts);
                    positional_index++;
                }
                if (!parse_ok) {
                    break;
                }
            }
            if (!parse_ok) {
                printf("PJ_ERR {\"error\":\"expected PJ_AUDIO_TONE [0|1|-] [dout_gpio] [pa=0|1|-] [dout=gpio] [pwr=0|1|-] [gpio44=0x00..0xff] [gp45=0x00..0xff]\"}\n");
                fflush(stdout);
                continue;
            }

            esp_err_t err = audio_play_tone_ms(PJ_AUDIO_DIAG_TONE_MS, &tone_opts);
            if (err == ESP_OK) {
                printf("PJ_OK {\"tone_ms\":%d,\"pa_level\":%d,\"volume\":%d,\"dout_gpio\":%d,\"audio_power_level\":%d,\"gpio44\":%d,\"gp45\":%d}\n",
                       PJ_AUDIO_DIAG_TONE_MS,
                       tone_opts.pa_level == 0 || tone_opts.pa_level == 1 ? tone_opts.pa_level : AUDIO_PA_ACTIVE_LEVEL,
                       pj_settings_codec_volume(g_settings.volume),
                       tone_opts.dout_gpio >= 0 ? tone_opts.dout_gpio : (int)g_i2s_dout_pin,
                       tone_opts.audio_power_level == 0 || tone_opts.audio_power_level == 1 ? tone_opts.audio_power_level : gpio_get_level(AUDIO_PWR_PIN),
                       tone_opts.codec_gpio44 >= 0 ? tone_opts.codec_gpio44 : audio_codec_reg_get(PJ_ES8311_GPIO_REG44),
                       tone_opts.codec_gp45 >= 0 ? tone_opts.codec_gp45 : audio_codec_reg_get(PJ_ES8311_GP_REG45));
            } else {
                printf("PJ_ERR {\"error\":\"audio tone failed: %s\"}\n", esp_err_to_name(err));
            }
            fflush(stdout);
            continue;
        }
        if (strncmp(line, "PJ_MIC_CHECK", 12) == 0) {
            int duration_ms = PJ_AUDIO_MIC_CHECK_MS;
            int gain_db = -1;
            int positional_index = 0;
            int parse_ok = 1;
            char *arg = line + 12;
            while (*arg == ' ' || *arg == '\t') {
                arg++;
            }

            while (*arg != '\0') {
                char *token = arg;
                while (*arg != '\0' && *arg != ' ' && *arg != '\t') {
                    arg++;
                }
                if (*arg != '\0') {
                    *arg++ = '\0';
                }
                while (*arg == ' ' || *arg == '\t') {
                    arg++;
                }

                char *equals = strchr(token, '=');
                if (equals != NULL) {
                    *equals++ = '\0';
                    int parsed = 0;
                    if (!serial_parse_int(equals, &parsed)) {
                        parse_ok = 0;
                    } else if (strcmp(token, "ms") == 0 || strcmp(token, "duration") == 0 || strcmp(token, "duration_ms") == 0) {
                        duration_ms = parsed;
                    } else if (strcmp(token, "gain") == 0 || strcmp(token, "gain_db") == 0) {
                        gain_db = parsed;
                    } else {
                        parse_ok = 0;
                    }
                } else {
                    int parsed = 0;
                    parse_ok = positional_index == 0 && serial_parse_int(token, &parsed);
                    if (parse_ok) {
                        duration_ms = parsed;
                        positional_index++;
                    }
                }
                if (!parse_ok) {
                    break;
                }
            }
            if (!parse_ok || duration_ms <= 0 || duration_ms > PJ_AUDIO_MIC_CHECK_MAX_MS ||
                (gain_db != -1 && (gain_db < 0 || gain_db > 42))) {
                printf("PJ_ERR {\"error\":\"expected PJ_MIC_CHECK [duration_ms] [ms=1..10000] [gain_db=0..42]\"}\n");
                fflush(stdout);
                continue;
            }

            audio_mic_check_result_t result;
            esp_err_t err = audio_mic_check_ms((uint32_t)duration_ms, gain_db, &result);
            if (err == ESP_OK) {
                int silent = result.peak < 64;
                printf("PJ_OK {\"duration_ms\":%u,\"sample_rate\":%d,\"gain_db\":%d,\"input_channel\":%d,"
                       "\"bytes_read\":%u,\"frames\":%u,\"peak\":%u,\"avg_abs\":%u,"
                       "\"clipped\":%u,\"near_zero\":%u,\"read_errors\":%u,\"silent\":%s}\n",
                       (unsigned)result.duration_ms,
                       PJ_AUDIO_SAMPLE_RATE,
                       result.gain_db,
                       result.input_channel,
                       (unsigned)result.bytes_read,
                       (unsigned)result.frames,
                       (unsigned)result.peak,
                       (unsigned)result.avg_abs,
                       (unsigned)result.clipped,
                       (unsigned)result.near_zero,
                       (unsigned)result.read_errors,
                       silent ? "true" : "false");
            } else {
                printf("PJ_ERR {\"error\":\"mic check failed: %s\"}\n", esp_err_to_name(err));
            }
            fflush(stdout);
            continue;
        }
        if (strncmp(line, "PJ_WIFI_HEX ", 12) == 0) {
            char *ssid_hex = line + 12;
            char *password_hex = strchr(ssid_hex, ' ');
            char *token_hex = NULL;
            if (password_hex != NULL) {
                *password_hex++ = '\0';
                token_hex = strchr(password_hex, ' ');
            }
            if (token_hex != NULL) {
                *token_hex++ = '\0';
            }
            char ssid[PJ_WIFI_SSID_MAX_LEN + 1];
            char password[PJ_WIFI_PASSWORD_MAX_LEN + 1];
            char token[sizeof(g_status.token)];
            if (password_hex == NULL || token_hex == NULL ||
                !hex_decode_string(ssid_hex, ssid, sizeof(ssid)) ||
                !hex_decode_string(password_hex, password, sizeof(password)) ||
                !hex_decode_string(token_hex, token, sizeof(token)) ||
                ssid[0] == '\0' || token[0] == '\0') {
                printf("PJ_ERR {\"error\":\"expected PJ_WIFI_HEX ssid_hex password_hex token_hex\"}\n");
                fflush(stdout);
                continue;
            }
            esp_err_t err = wifi_save_provisioning(ssid, password, token);
            if (err == ESP_OK) {
                printf("PJ_OK {\"device_id\":\"%s\",\"wifi\":\"stored\"}\n", g_status.device_id);
            } else {
                printf("PJ_ERR {\"error\":\"wifi provisioning failed: %s\"}\n", esp_err_to_name(err));
            }
            fflush(stdout);
            continue;
        }
        const char *time_payload = NULL;
        if (strncmp(line, "PJ_TIME ", 8) == 0) {
            time_payload = line + 8;
        } else if (strncmp(line, "PJ_SET_TIME ", 12) == 0) {
            time_payload = line + 12;
        }
        if (time_payload != NULL) {
            int year = 0;
            int month = 0;
            int day = 0;
            int hour = 0;
            int minute = 0;
            if (sscanf(time_payload, "%d %d %d %d %d", &year, &month, &day, &hour, &minute) == 5 &&
                valid_time_date(hour, minute, year, month, day)) {
                if (board_set_time_date(hour, minute, year, month, day,
                                        "USB-C partner")) {
                    printf("PJ_OK {\"hour\":%d,\"minute\":%d,\"year\":%d,\"month\":%d,\"day\":%d}\n",
                           hour, minute, year, month, day);
                } else {
                    printf("PJ_ERR {\"error\":\"time update could not be persisted to RTC\"}\n");
                }
            } else {
                printf("PJ_ERR {\"error\":\"expected PJ_TIME yyyy mm dd hh mm\"}\n");
            }
            fflush(stdout);
            continue;
        }
        printf("PJ_ERR {\"error\":\"unknown command\"}\n");
        fflush(stdout);
    }
}

static esp_err_t serial_command_task_start(void)
{
    if (g_serial_command_task_started) {
        return ESP_OK;
    }
    BaseType_t created = xTaskCreate(serial_command_task, "pj-serial", PJ_SERIAL_COMMAND_TASK_STACK, NULL, 3, NULL);
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    g_serial_command_task_started = 1;
    ESP_LOGI(TAG, "USB-C partner serial commands enabled");
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    int pending_sync = 0;
    int transferred_sync = 0;
    pj_board_status_t status = pj_board_status();
    if (g_status.storage_mounted) {
        (void)storage_refresh_capacity();
    }
    if (g_status.storage == PJ_BOARD_SERVICE_READY) {
        collect_sync_counts(&pending_sync, &transferred_sync);
    }
    cJSON *json = cJSON_CreateObject();
    cJSON *time = json == NULL ? NULL : cJSON_AddObjectToObject(json, "time");
    if (json == NULL || time == NULL) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"status allocation failed\"}");
    }
    cJSON_AddStringToObject(json, "device_id", g_status.device_id);
    cJSON_AddStringToObject(json, "firmware_version", "v0");
    cJSON_AddStringToObject(json, "board_profile", "waveshare-esp32-s3-touch-epaper-1.54-v2");
    cJSON_AddStringToObject(json, "display", service_name(g_status.display));
    cJSON_AddStringToObject(json, "storage", service_name(g_status.storage));
    cJSON_AddStringToObject(json, "audio", service_name(g_status.audio));
    cJSON_AddStringToObject(json, "ble_provisioning", service_name(g_status.ble_provisioning));
    cJSON_AddStringToObject(json, "wifi", service_name(g_status.wifi));
    cJSON_AddStringToObject(json, "http", service_name(g_status.http));
    cJSON_AddStringToObject(json, "ip", g_status.ip_addr);
    cJSON_AddBoolToObject(json, "wifi_provisioned", g_wifi_credentials_stored != 0);
    cJSON_AddNumberToObject(json, "battery_percent", g_status.battery_percent);
    cJSON_AddNumberToObject(json, "temperature_c", g_status.temperature_c);
    if (g_status.humidity_percent < 0) {
        cJSON_AddNullToObject(json, "humidity_percent");
    } else {
        cJSON_AddNumberToObject(json, "humidity_percent", g_status.humidity_percent);
    }
    cJSON_AddNumberToObject(time, "hour", g_status.hour);
    cJSON_AddNumberToObject(time, "minute", g_status.minute);
    cJSON_AddNumberToObject(time, "year", g_status.year);
    cJSON_AddNumberToObject(time, "month", g_status.month);
    cJSON_AddNumberToObject(time, "day", g_status.day);
    cJSON_AddBoolToObject(json, "storage_mounted", g_status.storage_mounted != 0);
    cJSON_AddStringToObject(json, "storage_health", pj_storage_health_name(g_status.storage_health));
    cJSON_AddNumberToObject(json, "storage_total_bytes", (double)g_status.storage_total_bytes);
    cJSON_AddNumberToObject(json, "storage_free_bytes", (double)g_status.storage_free_bytes);
    cJSON_AddNumberToObject(json, "storage_recovery_count", g_status.storage_recovery_count);
    cJSON_AddNumberToObject(json, "pending_sync", pending_sync);
    cJSON_AddNumberToObject(json, "transferred_sync", transferred_sync);
    cJSON_AddStringToObject(json, "last_error", g_status.last_error);
    if (!connectivity_add_json(json, &status)) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"status allocation failed\"}");
    }
    char *encoded = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (encoded == NULL) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"status encoding failed\"}");
    }
    esp_err_t result = send_json(req, encoded);
    cJSON_free(encoded);
    return result;
}

static esp_err_t time_get_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    board_time_snapshot_t time = board_time_snapshot();
    char json[96];
    (void)snprintf(json, sizeof(json),
                   "{\"hour\":%d,\"minute\":%d,\"year\":%d,\"month\":%d,\"day\":%d}",
                   time.hour, time.minute, time.year, time.month, time.day);
    return send_json(req, json);
}

static esp_err_t time_put_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    char body[192];
    board_time_snapshot_t time = board_time_snapshot();
    int hour = time.hour;
    int minute = time.minute;
    int year = time.year;
    int month = time.month;
    int day = time.day;
    if (!read_body(req, body, sizeof(body))) {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"expected hour/minute/month/day integers; optional year\"}");
    }
    (void)json_int_field(body, "year", &year);
    if (!json_int_field(body, "hour", &hour) ||
        !json_int_field(body, "minute", &minute) ||
        !json_int_field(body, "month", &month) ||
        !json_int_field(body, "day", &day) ||
        !valid_time_date(hour, minute, year, month, day)) {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"expected hour/minute/month/day integers; optional year\"}");
    }
    if (!board_set_time_date(hour, minute, year, month, day, "HTTP")) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req,
                         "{\"error\":\"time update could not be persisted to RTC\"}");
    }
    return time_get_handler(req);
}

static esp_err_t settings_get_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    pj_settings_t settings = g_settings;
    if (settings_take(portMAX_DELAY)) {
        settings = g_settings;
        settings_give();
    }
    int pending_sync = 0;
    int transferred_sync = 0;
    collect_sync_counts(&pending_sync, &transferred_sync);
    char json[512];
    (void)snprintf(json, sizeof(json),
                   "{\"theme\":\"%s\",\"volume\":%d,\"alarm_enabled\":%s,"
                   "\"alarm_hour\":%d,\"alarm_minute\":%d,\"timer_seconds\":%d,"
                   "\"interval_seconds\":%d,\"clock_24h\":%s,"
                   "\"temperature_unit\":\"%s\",\"transcript_font_size\":%d,"
                   "\"sync_pending\":%d,\"sync_transferred\":%d}",
                   settings.dark_mode ? "dark" : "light",
                   settings.volume,
                   settings.alarm_enabled ? "true" : "false",
                   settings.alarm_hour,
                   settings.alarm_minute,
                   settings.timer_seconds,
                   settings.interval_seconds,
                   settings.clock_24h ? "true" : "false",
                   settings.temperature_fahrenheit ? "f" : "c",
                   settings.transcript_font_size,
                   pending_sync,
                   transferred_sync);
    return send_json(req, json);
}

static int json_exact_int(const cJSON *item, int *value)
{
    if (!cJSON_IsNumber(item) || item->valuedouble != (double)item->valueint) {
        return 0;
    }
    *value = item->valueint;
    return 1;
}

static int settings_parse_update(const cJSON *json, pj_settings_t *settings)
{
    if (!cJSON_IsObject(json) || json->child == NULL) {
        return 0;
    }
    for (const cJSON *item = json->child; item != NULL; item = item->next) {
        const char *key = item->string;
        if (key == NULL) {
            return 0;
        }
        if (strcmp(key, "theme") == 0) {
            if (!cJSON_IsString(item) || item->valuestring == NULL) {
                return 0;
            }
            if (strcmp(item->valuestring, "light") == 0) {
                settings->dark_mode = 0;
            } else if (strcmp(item->valuestring, "dark") == 0) {
                settings->dark_mode = 1;
            } else {
                return 0;
            }
        } else if (strcmp(key, "alarm_enabled") == 0) {
            if (!cJSON_IsBool(item)) {
                return 0;
            }
            settings->alarm_enabled = cJSON_IsTrue(item) ? 1 : 0;
        } else if (strcmp(key, "volume") == 0) {
            if (!json_exact_int(item, &settings->volume)) {
                return 0;
            }
        } else if (strcmp(key, "alarm_hour") == 0) {
            if (!json_exact_int(item, &settings->alarm_hour)) {
                return 0;
            }
        } else if (strcmp(key, "alarm_minute") == 0) {
            if (!json_exact_int(item, &settings->alarm_minute)) {
                return 0;
            }
        } else if (strcmp(key, "timer_seconds") == 0) {
            if (!json_exact_int(item, &settings->timer_seconds)) {
                return 0;
            }
        } else if (strcmp(key, "interval_seconds") == 0) {
            if (!json_exact_int(item, &settings->interval_seconds)) {
                return 0;
            }
        } else if (strcmp(key, "clock_24h") == 0) {
            if (!cJSON_IsBool(item)) {
                return 0;
            }
            settings->clock_24h = cJSON_IsTrue(item) ? 1 : 0;
        } else if (strcmp(key, "temperature_unit") == 0) {
            if (!cJSON_IsString(item) || item->valuestring == NULL) {
                return 0;
            }
            if (strcmp(item->valuestring, "c") == 0) {
                settings->temperature_fahrenheit = 0;
            } else if (strcmp(item->valuestring, "f") == 0) {
                settings->temperature_fahrenheit = 1;
            } else {
                return 0;
            }
        } else if (strcmp(key, "transcript_font_size") == 0) {
            if (!json_exact_int(item, &settings->transcript_font_size)) {
                return 0;
            }
        } else {
            return 0;
        }
    }
    return pj_settings_valid(settings);
}

static esp_err_t settings_put_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    char body[512];
    if (req->content_len <= 0 || req->content_len >= (int)sizeof(body)) {
        drain_body(req);
        httpd_resp_set_status(req, req->content_len >= (int)sizeof(body) ?
                                  "413 Payload Too Large" : "400 Bad Request");
        return send_json(req, "{\"error\":\"invalid settings body\"}");
    }
    if (!read_body(req, body, sizeof(body))) {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"invalid settings body\"}");
    }
    cJSON *json = cJSON_ParseWithOpts(body, NULL, 1);
    if (!settings_take(portMAX_DELAY)) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"settings busy\"}");
    }
    pj_settings_t updated = g_settings;
    if (!settings_parse_update(json, &updated)) {
        cJSON_Delete(json);
        settings_give();
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"unsupported or invalid settings\"}");
    }
    cJSON_Delete(json);

    esp_err_t err = settings_save(&updated);
    int codec_volume = -1;
    if (err == ESP_OK) {
        g_settings = updated;
        codec_volume = pj_settings_codec_volume(updated.volume);
        g_settings_update_pending = 1;
    }
    settings_give();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP settings save failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_json(req, "{\"error\":\"settings store failed\"}");
    }
    settings_apply_codec_volume(codec_volume);
    alert_audio_set_volume(codec_volume);
    return settings_get_handler(req);
}

static esp_err_t home_get_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    pj_home_layout_t *layout = malloc(sizeof(*layout));
    if (layout == NULL || !home_layout_take(portMAX_DELAY)) {
        free(layout);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"home layout busy\"}");
    }
    *layout = g_home_layout;
    home_layout_give();

    cJSON *root = cJSON_CreateObject();
    cJSON *slots = root == NULL ? NULL : cJSON_AddArrayToObject(root, "slots");
    if (root != NULL) {
        cJSON_AddStringToObject(root, "title", layout->title);
    }
    for (uint8_t i = 0; slots != NULL && i < layout->slot_count; i++) {
        cJSON *slot = cJSON_CreateObject();
        if (slot == NULL || !cJSON_AddItemToArray(slots, slot)) {
            cJSON_Delete(slot);
            slots = NULL;
            break;
        }
        cJSON_AddStringToObject(slot, "label", layout->slots[i].label);
        cJSON_AddStringToObject(slot, "icon", layout->slots[i].icon);
        cJSON_AddStringToObject(slot, "state", layout->slots[i].destination);
    }
    free(layout);
    char *body = slots == NULL ? NULL : cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"home layout response out of memory\"}");
    }
    esp_err_t err = send_json(req, body);
    cJSON_free(body);
    return err;
}

static int json_has_exact_keys(const cJSON *object, const char *const *keys, size_t key_count)
{
    int seen[4] = {0};
    size_t count = 0;
    for (const cJSON *item = object == NULL ? NULL : object->child; item != NULL; item = item->next) {
        size_t index = 0;
        while (index < key_count && strcmp(item->string, keys[index]) != 0) {
            index++;
        }
        if (index == key_count || seen[index]) {
            return 0;
        }
        seen[index] = 1;
        count++;
    }
    return count == key_count;
}

static int home_parse_layout(const cJSON *json, pj_home_layout_t *layout, int *reset)
{
    static const char *const RESET_KEYS[] = {"reset"};
    static const char *const ROOT_KEYS[] = {"title", "slots"};
    static const char *const SLOT_KEYS[] = {"label", "icon", "state"};
    if (!cJSON_IsObject(json) || layout == NULL || reset == NULL) {
        return 0;
    }
    cJSON *reset_item = cJSON_GetObjectItemCaseSensitive(json, "reset");
    if (reset_item != NULL) {
        if (!json_has_exact_keys(json, RESET_KEYS, 1) || !cJSON_IsTrue(reset_item)) {
            return 0;
        }
        pj_home_layout_defaults(layout);
        *reset = 1;
        return 1;
    }
    if (!json_has_exact_keys(json, ROOT_KEYS, 2)) {
        return 0;
    }
    cJSON *title = cJSON_GetObjectItemCaseSensitive(json, "title");
    cJSON *slots = cJSON_GetObjectItemCaseSensitive(json, "slots");
    int slot_count = cJSON_IsArray(slots) ? cJSON_GetArraySize(slots) : 0;
    if (!cJSON_IsString(title) || title->valuestring == NULL ||
        slot_count < 1 || slot_count > PJ_HOME_MAX_SLOTS ||
        strlen(title->valuestring) >= PJ_HOME_TITLE_LEN) {
        return 0;
    }
    memset(layout, 0, sizeof(*layout));
    (void)snprintf(layout->title, sizeof(layout->title), "%s", title->valuestring);
    layout->slot_count = (uint8_t)slot_count;
    for (int i = 0; i < slot_count; i++) {
        cJSON *slot = cJSON_GetArrayItem(slots, i);
        if (!cJSON_IsObject(slot) || !json_has_exact_keys(slot, SLOT_KEYS, 3)) {
            return 0;
        }
        cJSON *label = cJSON_GetObjectItemCaseSensitive(slot, "label");
        cJSON *icon = cJSON_GetObjectItemCaseSensitive(slot, "icon");
        cJSON *state = cJSON_GetObjectItemCaseSensitive(slot, "state");
        if (!cJSON_IsString(label) || !cJSON_IsString(icon) || !cJSON_IsString(state) ||
            label->valuestring == NULL || icon->valuestring == NULL || state->valuestring == NULL ||
            strlen(label->valuestring) >= PJ_HOME_LABEL_LEN ||
            strlen(icon->valuestring) >= PJ_HOME_ICON_LEN ||
            strlen(state->valuestring) >= PJ_HOME_DESTINATION_LEN) {
            return 0;
        }
        (void)snprintf(layout->slots[i].label, sizeof(layout->slots[i].label), "%s", label->valuestring);
        (void)snprintf(layout->slots[i].icon, sizeof(layout->slots[i].icon), "%s", icon->valuestring);
        (void)snprintf(layout->slots[i].destination, sizeof(layout->slots[i].destination), "%s", state->valuestring);
    }
    *reset = 0;
    return pj_home_layout_valid(layout);
}

static esp_err_t home_put_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    if (req->content_len <= 0 || req->content_len >= 1024) {
        drain_body(req);
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"invalid home layout body\"}");
    }
    char *body = malloc((size_t)req->content_len + 1u);
    pj_home_layout_t *layout = malloc(sizeof(*layout));
    if (body == NULL || layout == NULL) {
        free(layout);
        free(body);
        drain_body(req);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"home layout update out of memory\"}");
    }
    if (!read_body(req, body, (size_t)req->content_len + 1u)) {
        free(layout);
        free(body);
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"invalid home layout body\"}");
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    int reset = 0;
    if (!home_parse_layout(json, layout, &reset)) {
        cJSON_Delete(json);
        free(layout);
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"unsupported or invalid home layout\"}");
    }
    cJSON_Delete(json);
    esp_err_t err = home_layout_replace(layout);
    free(layout);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP home layout save failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_json(req, "{\"error\":\"home layout store failed\"}");
    }
    ESP_LOGI(TAG, "Home layout persisted%s; UI apply queued", reset ? " (built-in reset)" : "");
    return home_get_handler(req);
}

static esp_err_t update_ok_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    drain_body(req);
    return send_json(req, "{\"updated\":true}");
}

static esp_err_t static_art_get_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    pj_static_art_t *art = malloc(sizeof(*art));
    if (art == NULL) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"static art read out of memory\"}");
    }
    if (!static_art_take(portMAX_DELAY)) {
        free(art);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"static art store busy\"}");
    }
    int valid = g_static_art_valid;
    if (valid) {
        *art = g_static_art;
    }
    static_art_give();
    if (!valid) {
        free(art);
        httpd_resp_set_status(req, "404 Not Found");
        return send_json(req, "{\"error\":\"static art not set\"}");
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr_chunk(req,
        "{\"width\":200,\"height\":200,\"encoding\":\"rows\",\"rows\":[");
    char row[PJ_STATIC_ART_WIDTH + 4];
    for (int y = 0; err == ESP_OK && y < PJ_STATIC_ART_HEIGHT; y++) {
        size_t offset = 0;
        if (y > 0) {
            row[offset++] = ',';
        }
        row[offset++] = '\"';
        for (int x = 0; x < PJ_STATIC_ART_WIDTH; x++) {
            row[offset++] = pj_static_art_pixel(art, x, y) ? '1' : '0';
        }
        row[offset++] = '\"';
        row[offset] = '\0';
        err = httpd_resp_sendstr_chunk(req, row);
    }
    free(art);
    if (err == ESP_OK) {
        err = httpd_resp_sendstr_chunk(req, "]}");
    }
    if (err == ESP_OK) {
        err = httpd_resp_sendstr_chunk(req, NULL);
    }
    return err;
}

static esp_err_t static_art_put_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    if (req->content_len <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"static art body is required\"}");
    }
    if (req->content_len > 65536) {
        drain_body(req);
        httpd_resp_set_status(req, "413 Payload Too Large");
        return send_json(req, "{\"error\":\"static art body exceeds 65536 bytes\"}");
    }

    char *body = malloc((size_t)req->content_len + 1u);
    if (body == NULL) {
        drain_body(req);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"static art parse out of memory\"}");
    }
    if (!read_body(req, body, (size_t)req->content_len + 1u)) {
        free(body);
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"incomplete static art body\"}");
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!cJSON_IsObject(json)) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"static art body must be a JSON object\"}");
    }

    cJSON *width = cJSON_GetObjectItemCaseSensitive(json, "width");
    cJSON *height = cJSON_GetObjectItemCaseSensitive(json, "height");
    cJSON *encoding = cJSON_GetObjectItemCaseSensitive(json, "encoding");
    cJSON *rows_json = cJSON_GetObjectItemCaseSensitive(json, "rows");
    const char **rows = calloc(PJ_STATIC_ART_HEIGHT, sizeof(*rows));
    pj_static_art_t *art = malloc(sizeof(*art));
    if (rows == NULL || art == NULL) {
        free(rows);
        free(art);
        cJSON_Delete(json);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"static art validation out of memory\"}");
    }
    size_t row_count = cJSON_IsArray(rows_json) ? (size_t)cJSON_GetArraySize(rows_json) : 0;
    int rows_are_strings = row_count == PJ_STATIC_ART_HEIGHT;
    for (int i = 0; rows_are_strings && i < PJ_STATIC_ART_HEIGHT; i++) {
        cJSON *row = cJSON_GetArrayItem(rows_json, i);
        rows_are_strings = cJSON_IsString(row) && row->valuestring != NULL;
        rows[i] = rows_are_strings ? row->valuestring : NULL;
    }

    char validation_error[96];
    int width_value = cJSON_IsNumber(width) && width->valuedouble == PJ_STATIC_ART_WIDTH ?
        PJ_STATIC_ART_WIDTH : 0;
    int height_value = cJSON_IsNumber(height) && height->valuedouble == PJ_STATIC_ART_HEIGHT ?
        PJ_STATIC_ART_HEIGHT : 0;
    int parsed = rows_are_strings &&
        pj_static_art_from_rows(width_value,
                                height_value,
                                cJSON_IsString(encoding) ? encoding->valuestring : NULL,
                                rows, row_count, art, validation_error, sizeof(validation_error));
    if (!rows_are_strings) {
        (void)snprintf(validation_error, sizeof(validation_error),
                       row_count == PJ_STATIC_ART_HEIGHT ?
                       "each row must be a string" : "rows must contain exactly 200 strings");
    }
    free(rows);
    cJSON_Delete(json);
    if (!parsed) {
        char encoded_error[128];
        (void)snprintf(encoded_error, sizeof(encoded_error), "{\"error\":\"%s\"}", validation_error);
        free(art);
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, encoded_error);
    }

    if (!static_art_take(portMAX_DELAY)) {
        free(art);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"static art store busy\"}");
    }
    int saved_slot = -1;
    esp_err_t err = static_art_save(art, &saved_slot);
    if (err == ESP_OK) {
        g_static_art = *art;
        g_static_art_valid = 1;
        g_static_art_slot = saved_slot;
        g_static_art_update_pending = 1;
    }
    static_art_give();
    free(art);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Static art save failed: %s", esp_err_to_name(err));
        if (err == ESP_ERR_INVALID_STATE) {
            httpd_resp_set_status(req, "503 Service Unavailable");
            return send_json(req, "{\"error\":\"storage unavailable; run storage recovery\"}");
        }
        if (err == ESP_ERR_NO_MEM && g_status.storage_health == PJ_STORAGE_HEALTH_FULL) {
            httpd_resp_set_status(req, "507 Insufficient Storage");
            return send_json(req, "{\"error\":\"insufficient storage for static art\"}");
        }
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_json(req, "{\"error\":\"static art store failed\"}");
    }
    return send_json(req, "{\"updated\":true}");
}

static int audio_sha256_hex(const char *filename, char out[65])
{
    char path[160];
    uint8_t buffer[1024];
    uint8_t digest[PSA_HASH_LENGTH(PSA_ALG_SHA_256)];
    size_t digest_length = 0;
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    int valid = 0;
    (void)snprintf(path, sizeof(path), PJ_AUDIO_DIR "/%s", filename);
    FILE *file = fopen(path, "rb");
    if (file == NULL || psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_setup(&operation, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        if (file != NULL) {
            fclose(file);
        }
        return 0;
    }
    for (;;) {
        size_t read = fread(buffer, 1, sizeof(buffer), file);
        if (read > 0 && psa_hash_update(&operation, buffer, read) != PSA_SUCCESS) {
            break;
        }
        if (read < sizeof(buffer)) {
            if (ferror(file) == 0 &&
                psa_hash_finish(&operation, digest, sizeof(digest), &digest_length) == PSA_SUCCESS &&
                digest_length == sizeof(digest)) {
                static const char HEX[] = "0123456789abcdef";
                for (size_t i = 0; i < sizeof(digest); i++) {
                    out[i * 2] = HEX[digest[i] >> 4];
                    out[i * 2 + 1] = HEX[digest[i] & 0x0f];
                }
                out[64] = '\0';
                valid = 1;
            }
            break;
        }
    }
    fclose(file);
    if (!valid) {
        (void)psa_hash_abort(&operation);
    }
    return valid;
}

static esp_err_t audio_list_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    if (g_status.storage != PJ_BOARD_SERVICE_READY) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"storage unavailable; run storage recovery\"}");
    }
    httpd_resp_set_type(req, "application/json");
    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, "{\"audio\":["), TAG, "audio list send failed");
    pj_audio_entry_t *entries = calloc(PJ_AUDIO_MAX_INDEXED_FILES, sizeof(entries[0]));
    int emitted = 0;
    if (entries != NULL) {
        int count = collect_audio_entries(entries, PJ_AUDIO_MAX_INDEXED_FILES);
        for (int i = 0; i < count; i++) {
            cJSON *item = cJSON_CreateObject();
            if (item == NULL) {
                continue;
            }
            cJSON_AddStringToObject(item, "audio_id", entries[i].filename);
            cJSON_AddStringToObject(item, "filename", entries[i].filename);
            cJSON_AddStringToObject(item, "label", entries[i].label);
            cJSON_AddNumberToObject(item, "size", entries[i].size_bytes);
            cJSON_AddNumberToObject(item, "data_bytes", entries[i].data_bytes);
            char source_sha256[65];
            if (audio_sha256_hex(entries[i].filename, source_sha256)) {
                cJSON_AddStringToObject(item, "source_sha256", source_sha256);
            } else {
                cJSON_AddNullToObject(item, "source_sha256");
            }
            cJSON_AddStringToObject(item, "created_at", entries[i].note.created_at);
            cJSON_AddNumberToObject(item, "duration_ms", entries[i].note.duration_ms);
            cJSON_AddBoolToObject(item, "synced", entries[i].note.synced != 0);
            cJSON_AddBoolToObject(item, "transcript_uploaded", entries[i].note.synced != 0);
            cJSON_AddStringToObject(item, "transcript_path", entries[i].note.transcript_path);
            char *encoded = cJSON_PrintUnformatted(item);
            cJSON_Delete(item);
            if (encoded == NULL) {
                continue;
            }
            esp_err_t err = emitted ? httpd_resp_sendstr_chunk(req, ",") : ESP_OK;
            if (err == ESP_OK) {
                err = httpd_resp_sendstr_chunk(req, encoded);
            }
            cJSON_free(encoded);
            if (err != ESP_OK) {
                free(entries);
                ESP_RETURN_ON_ERROR(err, TAG, "audio list item send failed");
            }
            emitted++;
        }
        free(entries);
    }
    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, "]}"), TAG, "audio list end send failed");
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t audio_delete_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    int deleted = pj_board_wipe_recordings(NULL);
    if (deleted == -2) {
        httpd_resp_set_status(req, "409 Conflict");
        return send_json(req, "{\"error\":\"audio task active\"}");
    }
    if (deleted < 0) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"storage unavailable\"}");
    }
    char json[96];
    (void)snprintf(json, sizeof(json), "{\"deleted\":%d}", deleted);
    return send_json(req, json);
}

static esp_err_t audio_download_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    if (g_status.storage != PJ_BOARD_SERVICE_READY) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"storage unavailable; run storage recovery\"}");
    }
    const char *id = strrchr(req->uri, '/');
    if (id == NULL || id[1] == '\0' || strstr(id + 1, "..") != NULL || strchr(id + 1, '/') != NULL) {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"invalid audio id\"}");
    }
    char path[160];
    (void)snprintf(path, sizeof(path), PJ_AUDIO_DIR "/%s", id + 1);
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        httpd_resp_set_status(req, "404 Not Found");
        return send_json(req, "{\"error\":\"audio not found\"}");
    }
    httpd_resp_set_type(req, "audio/wav");
    char chunk[512];
    while (1) {
        size_t read = fread(chunk, 1, sizeof(chunk), file);
        if (read > 0) {
            esp_err_t err = httpd_resp_send_chunk(req, chunk, read);
            if (err != ESP_OK) {
                fclose(file);
                return err;
            }
        }
        if (read < sizeof(chunk)) {
            break;
        }
    }
    fclose(file);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t transcript_put_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    if (g_status.storage != PJ_BOARD_SERVICE_READY) {
        drain_body(req);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"storage unavailable; run storage recovery\"}");
    }
    const char *id = strrchr(req->uri, '/');
    if (id == NULL || id[1] == '\0' || strstr(id + 1, "..") != NULL || strchr(id + 1, '/') != NULL) {
        httpd_resp_set_status(req, "400 Bad Request");
        drain_body(req);
        return send_json(req, "{\"error\":\"invalid transcript id\"}");
    }
    pj_audio_entry_t entry;
    if (!probe_audio_entry(id + 1, &entry)) {
        httpd_resp_set_status(req, "404 Not Found");
        drain_body(req);
        return send_json(req, "{\"error\":\"audio not found\"}");
    }
    if (req->content_len <= 0) {
        drain_body(req);
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"transcript body is required\"}");
    }
    if (req->content_len > (int)PJ_TRANSCRIPT_MAX_BODY_BYTES) {
        drain_body(req);
        httpd_resp_set_status(req, "413 Payload Too Large");
        return send_json(req, "{\"error\":\"transcript body exceeds 65536 bytes\"}");
    }
    char *body = malloc((size_t)req->content_len + 1u);
    if (body == NULL) {
        drain_body(req);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"transcript upload out of memory\"}");
    }
    if (!read_body(req, body, (size_t)req->content_len + 1u)) {
        free(body);
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"incomplete transcript body\"}");
    }
    pj_transcript_body_result_t validation = pj_transcript_body_validate(
        body, (size_t)req->content_len, (size_t)req->content_len);
    if (validation != PJ_TRANSCRIPT_BODY_VALID) {
        free(body);
        httpd_resp_set_status(req, "400 Bad Request");
        if (validation == PJ_TRANSCRIPT_BODY_MALFORMED) {
            return send_json(req, "{\"error\":\"malformed transcript JSON\"}");
        }
        return send_json(req, "{\"error\":\"transcript must be a JSON object with non-empty text\"}");
    }
    char path[PJ_NOTE_TRANSCRIPT_PATH_LEN];
    transcript_path_for_audio(path, sizeof(path), id + 1);
    esp_err_t write_err = json_write_file_atomic(path, body, (size_t)req->content_len);
    free(body);
    if (write_err == ESP_ERR_NO_MEM && g_status.storage_health == PJ_STORAGE_HEALTH_FULL) {
        httpd_resp_set_status(req, "507 Insufficient Storage");
        return send_json(req, "{\"error\":\"insufficient storage for transcript\"}");
    }
    if (write_err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"storage unavailable; run storage recovery\"}");
    }
    if (write_err != ESP_OK) {
        ESP_LOGW(TAG, "Transcript store failed: %s", path);
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_json(req, "{\"error\":\"transcript store failed\"}");
    }
    if (!probe_audio_entry(id + 1, &entry)) {
        ESP_LOGW(TAG, "Transcript stored but note metadata refresh failed: %s", id + 1);
    }
    g_notes_update_pending = 1;
    ESP_LOGI(TAG, "Transcript stored and note marked synced: %s", path);
    return send_json(req, "{\"uploaded\":true}");
}

static esp_err_t storage_recover_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    if (g_status.recording || g_record_task != NULL || g_audio_process_task != NULL ||
        g_status.playback_active || g_playback_task != NULL) {
        httpd_resp_set_status(req, "409 Conflict");
        return send_json(req, "{\"error\":\"storage recovery blocked while audio is active\"}");
    }
    if (!pj_board_storage_recover()) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"storage recovery failed; check card and filesystem\"}");
    }
    return send_json(req, "{\"recovered\":true}");
}

static esp_err_t register_uri(httpd_handle_t server, const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t spec = {
        .uri = uri,
        .method = method,
        .handler = handler,
        .user_ctx = NULL,
    };
    esp_err_t err = httpd_register_uri_handler(server, &spec);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP route registration failed for %s: %s", uri, esp_err_to_name(err));
    }
    return err;
}
#endif

int pj_board_http_start(void)
{
#ifdef ESP_PLATFORM
    if (g_http_server != NULL) {
        return 1;
    }

    esp_err_t err = network_stack_init();
    if (err != ESP_OK) {
        g_status.http = PJ_BOARD_SERVICE_ERROR;
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "network stack init failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "%s", g_status.last_error);
        return 0;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = PJ_HTTP_MAX_URI_HANDLERS;
    config.uri_match_fn = httpd_uri_match_wildcard;
    err = httpd_start(&g_http_server, &config);
    if (err != ESP_OK) {
        g_status.http = PJ_BOARD_SERVICE_ERROR;
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error), "HTTP start failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "%s", g_status.last_error);
        return 0;
    }

#define REGISTER_URI_OR_FAIL(uri, method, handler) do { \
        err = register_uri(g_http_server, uri, method, handler); \
        if (err != ESP_OK) { \
            g_status.http = PJ_BOARD_SERVICE_ERROR; \
            (void)snprintf(g_status.last_error, sizeof(g_status.last_error), \
                           "HTTP route registration failed for %s: %s", uri, esp_err_to_name(err)); \
            httpd_stop(g_http_server); \
            g_http_server = NULL; \
            return 0; \
        } \
    } while (0)

    REGISTER_URI_OR_FAIL("/v1/status", HTTP_GET, status_handler);
    REGISTER_URI_OR_FAIL("/v1/storage/recover", HTTP_POST, storage_recover_handler);
    REGISTER_URI_OR_FAIL("/v1/time", HTTP_GET, time_get_handler);
    REGISTER_URI_OR_FAIL("/v1/time", HTTP_PUT, time_put_handler);
    REGISTER_URI_OR_FAIL("/v1/settings", HTTP_GET, settings_get_handler);
    REGISTER_URI_OR_FAIL("/v1/settings", HTTP_PUT, settings_put_handler);
    REGISTER_URI_OR_FAIL("/v1/home", HTTP_GET, home_get_handler);
    REGISTER_URI_OR_FAIL("/v1/home", HTTP_PUT, home_put_handler);
    REGISTER_URI_OR_FAIL("/v1/static-art", HTTP_GET, static_art_get_handler);
    REGISTER_URI_OR_FAIL("/v1/static-art", HTTP_PUT, static_art_put_handler);
    REGISTER_URI_OR_FAIL("/v1/audio", HTTP_GET, audio_list_handler);
    REGISTER_URI_OR_FAIL("/v1/audio", HTTP_DELETE, audio_delete_handler);
    REGISTER_URI_OR_FAIL("/v1/audio/*", HTTP_GET, audio_download_handler);
    REGISTER_URI_OR_FAIL("/v1/transcripts/*", HTTP_PUT, transcript_put_handler);
    REGISTER_URI_OR_FAIL("/v1/calendar/today", HTTP_PUT, update_ok_handler);
#undef REGISTER_URI_OR_FAIL

    g_status.http = PJ_BOARD_SERVICE_READY;
    ESP_LOGI(TAG, "HTTP API started with bearer authentication enabled");
    return 1;
#else
    g_status.http = PJ_BOARD_SERVICE_UNAVAILABLE;
    return 0;
#endif
}
