#include "pj_board.h"
#include "pj_alert_audio.h"
#include "pj_audio_lifecycle.h"
#include "pj_audio_level.h"
#include "pj_aux_input.h"
#include "pj_auth.h"
#include "pj_display_refresh.h"
#include "pj_note_model.h"
#include "pj_ota.h"
#include "pj_power_input.h"
#include "pj_recording.h"
#include "pj_rtc_wake.h"
#include "pj_runtime_diagnostics.h"
#include "pj_settings.h"
#include "pj_storage.h"
#include "pj_storage_coordinator.h"
#include "pj_time_civil.h"
#include "pj_time_clock.h"
#include "pj_time_controller.h"
#include "pj_time_sync.h"
#include "pj_time_transaction.h"
#include "pj_touch_candidate.h"
#include "pj_transcript_upload.h"
#include "pj_usb_sync.h"
#include "pj_wifi_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdarg.h>
#ifdef ESP_PLATFORM
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#endif

#ifdef ESP_PLATFORM
#include "esp_check.h"
#include "esp_attr.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_memory_utils.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sleep.h"
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
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
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
#define PJ_NVS_TIME_STATE "time_state"
#define PJ_NVS_WAKE_PLAN "wake_plan"
#define PJ_NVS_UTC_OFFSET "utc_offset"
#define PJ_WIFI_SSID_MAX_LEN 32
#define PJ_WIFI_PASSWORD_MAX_LEN 64
#define EPD_SPI_NUM SPI2_HOST
#define PJ_EPD_SPI_CLOCK_HZ (20 * 1000 * 1000)
#define PJ_EPD_BUSY_TIMEOUT_US (10 * 1000 * 1000)
#define PJ_EPD_BUSY_POLL_TICKS 1
#define PJ_EPD_LUT_TRANSFER_BYTES 153
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
/*
 * esp_codec_dev subtracts the declared PA gain from its DAC target.  Zero
 * compensation keeps full volume at 0 dB; the former 6 dB declaration
 * attenuated every nonzero setting by about half in voltage amplitude.
 */
#define PJ_AUDIO_CODEC_PA_GAIN_COMPENSATION_DB 0.0f
#define PJ_AUDIO_RECORD_TASK_STACK 6144
#define PJ_AUDIO_PROCESS_TASK_STACK 6144
#define PJ_AUDIO_PROCESS_QUEUE_LENGTH 4U
#define PJ_AUDIO_PROCESS_SWAP_WAIT_MS 30000U
#define PJ_AUDIO_PROCESS_SWAP_POLL_MS 20U
#define PJ_AUDIO_PLAYBACK_TASK_STACK 6144
#define PJ_ALERT_AUDIO_TASK_STACK 4096
#define PJ_ALERT_AUDIO_FAILURE_ACK_THRESHOLD 3
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
#define PJ_AUX_POLL_MS 10
#define PJ_TOUCH_EVENT_QUEUE_DEPTH 8
#define PJ_AUX_EVENT_QUEUE_DEPTH 4
#define PJ_TOUCH_STABLE_SAMPLES 2
#define PJ_TOUCH_MOVE_TOLERANCE 18
#define PJ_HTTP_MAX_URI_HANDLERS 20
#define PJ_AUDIO_DIR "/sdcard/pj/audio"
#define PJ_TRANSCRIPT_DIR "/sdcard/pj/transcripts"
#define PJ_NOTE_DIR "/sdcard/pj/notes"
#define PJ_USB_TRANSCRIPT_TEMP_PATH PJ_TRANSCRIPT_DIR "/.usb-upload.tmp"
#define PJ_USB_SETTINGS_BODY_BYTES 256U
#define PJ_AUDIO_INITIAL_INDEX_CAPACITY 16U
#define PJ_AUDIO_MAX_INDEXED_FILES 512U
#define PJ_AUDIO_COLLECT_STORAGE_BUSY (-1)
#define PJ_AUDIO_COLLECT_TOO_MANY (-2)
#define PJ_AUDIO_COLLECT_NO_MEMORY (-3)
#define PJ_AUDIO_COLLECT_OPEN_FAILED (-4)
/* PJ_AUDIO_READ holds 1024 raw bytes plus 2049 hex bytes on this task's stack. */
#define PJ_SERIAL_COMMAND_TASK_STACK 12288
#define PJ_SERIAL_MIN_FREE_STACK_BYTES 2048U
#define PJ_SERIAL_RX_BUFFER_BYTES 1024U
#define PJ_SYNC_INVENTORY_TASK_STACK 8192
#define PJ_SERIAL_TX_BUFFER_BYTES 4096U
#define PJ_STORAGE_WIPE_TASK_STACK 4096
#define PJ_STORAGE_WIPE_BATCH_ENTRIES 64U
#define PJ_STORAGE_WIPE_MAX_BATCHES 64U
#define PJ_INTERVAL_RESET_WAIT_MS 3000U
#define PJ_INTERVAL_RESET_POLL_MS 20U
#define PJ_CONNECTIVITY_TASK_STACK 4096
#define PJ_SNTP_PUBLICATION_TASK_STACK 4096
#define PJ_CONNECTIVITY_POLL_MS 250
#define PJ_WIFI_RECONNECT_SETTLE_MS 250
#define PJ_WIFI_CONTROL_EVENT_POLL_MS 10
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
static int g_display_warning_logged;
static uint32_t g_time_generation;
#ifndef ESP_PLATFORM
static int g_time_update_pending;
static int g_settings_update_pending;
#endif

#ifdef ESP_PLATFORM
typedef struct {
    int valid;
    char request_id[PJ_USB_SYNC_REQUEST_ID_BYTES];
    uint32_t expected_generation;
    size_t payload_size;
    char payload[PJ_USB_SETTINGS_BODY_BYTES + 1U];
    pj_settings_t committed;
    uint32_t resulting_generation;
    int changed;
} pj_usb_settings_replay_t;

static pj_usb_settings_replay_t g_usb_settings_replay;
#endif

typedef struct {
    int hour;
    int minute;
    int second;
    int year;
    int month;
    int day;
    int time_set;
    uint32_t generation;
} board_time_snapshot_t;

typedef enum {
    BOARD_TIME_UPDATE_OK = 0,
    BOARD_TIME_UPDATE_INVALID,
    BOARD_TIME_UPDATE_YEAR_REQUIRED,
    BOARD_TIME_UPDATE_UNAVAILABLE,
    BOARD_TIME_UPDATE_RTC_SNAPSHOT_FAILED,
    BOARD_TIME_UPDATE_OFFSET_PERSIST_FAILED,
    BOARD_TIME_UPDATE_RTC_WRITE_FAILED,
    BOARD_TIME_UPDATE_PARTIAL_COMMIT,
} board_time_update_status_t;

typedef struct {
    board_time_update_status_t status;
    uint32_t partial_components;
} board_time_update_result_t;

static int valid_time_date(int hour, int minute, int year, int month, int day)
{
    return year >= 2024 && year <= 2099 &&
           pj_time_clock_civil_valid(year, month, day, hour, minute, 0);
}

#ifdef ESP_PLATFORM
typedef struct {
    char final_path[128];
    uint32_t data_bytes;
    uint32_t raw_peak;
    uint32_t raw_avg;
    uint32_t raw_clipped;
    int input_channel;
} audio_process_args_t;

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
static QueueHandle_t g_aux_event_queue;
static QueueHandle_t g_audio_process_queue;
static StaticQueue_t g_audio_process_queue_storage;
static uint8_t g_audio_process_queue_buffer[
    PJ_AUDIO_PROCESS_QUEUE_LENGTH * sizeof(audio_process_args_t)];
static SemaphoreHandle_t g_i2c_lock;
static SemaphoreHandle_t g_rtc_sequence_lock;
static SemaphoreHandle_t g_audio_lock;
static SemaphoreHandle_t g_settings_lock;
static SemaphoreHandle_t g_json_write_lock;
static SemaphoreHandle_t g_time_transaction_lock;
static StaticSemaphore_t g_time_transaction_lock_storage;
static SemaphoreHandle_t g_provisioning_lock;
static StaticSemaphore_t g_provisioning_lock_storage;
static SemaphoreHandle_t g_status_lock;
static StaticSemaphore_t g_status_lock_storage;
static SemaphoreHandle_t g_audio_lifecycle_lock;
static StaticSemaphore_t g_audio_lifecycle_lock_storage;
static EventGroupHandle_t g_board_update_events;
static StaticEventGroup_t g_board_update_events_storage;
static sdmmc_card_t *g_sd_card;
static DMA_ATTR uint8_t g_epd_buffer[PJ_FRAMEBUFFER_BYTES];
static DMA_ATTR uint8_t g_epd_lut_buffer[PJ_EPD_LUT_TRANSFER_BYTES];
static pj_framebuffer_t g_epd_shadow_fb;
static int g_display_ready;
static int g_epd_shadow_valid;
static int g_epd_partial_ready;
static uint32_t g_epd_refresh_busy_us;
static pj_display_refresh_policy_t g_epd_refresh_policy;
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
static QueueHandle_t g_sntp_publication_queue;
static TaskHandle_t g_sntp_publication_task;
static int g_wifi_control_disconnect_pending;
static portMUX_TYPE g_connectivity_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE g_credentials_lock = portMUX_INITIALIZER_UNLOCKED;
static pj_wifi_state_t g_wifi_state;
static pj_time_sync_state_t g_time_sync_state;
static int g_mdns_started;
static int g_ble_started;
static uint8_t g_ble_addr_type;
static int g_ble_provision_active;
static char g_ble_ssid[PJ_WIFI_SSID_MAX_LEN + 1];
static char g_ble_password[PJ_WIFI_PASSWORD_MAX_LEN + 1];
static char g_ble_token[sizeof(g_status.token)];
static char g_ble_state[24] = "idle";
static pj_aux_input_t g_aux_input;
static pj_power_input_t g_power_input;
static int g_aux_task_started;
static int g_aux_released = 1;
static int g_power_released = 1;
static int g_touch_task_started;
static int g_serial_command_task_started;
static int g_touch_pressed;
static uint16_t g_touch_press_x;
static uint16_t g_touch_press_y;
static TickType_t g_touch_last_event_tick;
static pj_touch_candidate_t g_touch_candidate;
static uint8_t g_touch_raw_event;
static TaskHandle_t g_record_task;
static TaskHandle_t g_audio_process_task;
static TaskHandle_t g_playback_task;
static TaskHandle_t g_alert_audio_task;
static TaskHandle_t g_storage_wipe_task;
static TaskHandle_t g_sync_inventory_task;
static SemaphoreHandle_t g_sync_inventory_lock;
static StaticSemaphore_t g_sync_inventory_lock_storage;
static int g_record_audio_owned;
static int g_record_storage_owned;
static int g_playback_storage_owned;
static int g_alert_audio_output_owned;
static char g_active_recording_path[128];
static char g_active_playback_path[128];
static int g_ui_note_audio_indices[PJ_UI_MAX_NOTES];
static int g_ui_note_audio_count;
static int g_ui_note_transcript_view;
static char g_wifi_ssid[PJ_WIFI_SSID_MAX_LEN + 1];
static char g_wifi_password[PJ_WIFI_PASSWORD_MAX_LEN + 1];
static int g_wifi_credentials_stored;
static char g_provisioned_token[sizeof(g_status.token)];
static int g_utc_offset_known;
static int g_utc_offset_minutes;
static uint32_t g_record_sequence;
static portMUX_TYPE g_time_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE g_recording_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE g_storage_coordinator_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE g_aux_state_lock = portMUX_INITIALIZER_UNLOCKED;
static pj_storage_coordinator_t g_storage_coordinator;
static uint32_t g_runtime_boot_id;
static int g_cached_pending_sync;
static int g_cached_transferred_sync;
typedef enum {
    PJ_SYNC_INVENTORY_IDLE = 0,
    PJ_SYNC_INVENTORY_REQUESTED,
    PJ_SYNC_INVENTORY_RUNNING,
    PJ_SYNC_INVENTORY_RESULT,
} sync_inventory_worker_state_t;
static sync_inventory_worker_state_t g_sync_inventory_worker_state;
static int g_sync_inventory_discard;
static pj_board_sync_inventory_t g_sync_inventory_result;
static pj_recording_t g_recording;
static pj_audio_lifecycle_t g_audio_lifecycle;
static pj_usb_upload_t g_usb_upload;
typedef struct {
    char audio_id[PJ_NOTE_FILENAME_LEN];
    char sha256[PJ_USB_SYNC_SHA256_HEX_BYTES];
    off_t size;
    time_t modified;
    int valid;
} usb_audio_identity_cache_t;
static usb_audio_identity_cache_t g_usb_audio_identity;
#ifdef ESP_PLATFORM
static DMA_ATTR int16_t g_record_stereo_buffer[PJ_AUDIO_IO_BUFFER_BYTES / sizeof(int16_t)];
static DMA_ATTR int16_t g_record_mono_buffer[PJ_AUDIO_IO_BUFFER_BYTES / PJ_AUDIO_FRAME_BYTES];
_Static_assert(PJ_AUDIO_IO_BUFFER_BYTES % PJ_AUDIO_FRAME_BYTES == 0,
               "recording workspace must contain complete stereo frames");
#endif
static pj_time_clock_anchor_t g_time_clock_anchor;
static pj_time_controller_t g_time_controller;
static pj_time_controller_diagnostic_t g_time_last_diagnostic;
static int g_time_wall_trusted;
static int g_time_known;
static pj_rtc_wake_plan_t g_rtc_wake_plan;
static int g_rtc_ext1_enabled;
static int g_rtc_wake_hardware_verified;
static int g_timer_wakeup_enabled;
static int g_rtc_wake_restored;
static int g_rtc_wake_metadata_blocked;

typedef struct {
    struct timeval tv;
    uint64_t received_monotonic_ms;
} sntp_publication_event_t;

typedef struct {
    pj_time_transaction_result_t transaction;
    esp_err_t rtc_snapshot_error;
    esp_err_t offset_store_error;
    esp_err_t rtc_write_error;
    esp_err_t rtc_restore_error;
    esp_err_t offset_restore_error;
} board_time_commit_result_t;

typedef struct {
    uint64_t alert_id;
    pj_alert_audio_kind_t kind;
    uint8_t volume;
    uint8_t recording;
    uint8_t deferred;
    uint32_t stop_generation;
} alert_audio_intent_t;

typedef struct {
    uint32_t requested_generation;
    uint32_t completed_generation;
    uint8_t state_changed;
    uint8_t persistence_ok;
    uint8_t interval_active_before;
    uint8_t interval_active_after;
    uint8_t audio_silenced;
} interval_reset_state_t;

typedef enum {
    PJ_WIPE_WORKER_RELEASE_NOW = 0,
    PJ_WIPE_WORKER_RELEASE_AFTER_RESPONSE,
} recording_wipe_release_mode_t;

static portMUX_TYPE g_alert_audio_intent_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE g_interval_reset_lock = portMUX_INITIALIZER_UNLOCKED;
static alert_audio_intent_t g_alert_audio_intent;
static uint64_t g_alert_ack_pending;
static uint32_t g_alert_audio_stop_completed_generation;
static interval_reset_state_t g_interval_reset;

static uint64_t board_monotonic_ms(void);
static esp_err_t connectivity_runtime_start(void);
static void connectivity_state_init(void);
static void connectivity_state_snapshot(pj_wifi_state_t *wifi,
                                        pj_time_sync_state_t *time_sync);
static int board_time_publish(int hour, int minute, int year, int month, int day,
                              int second, uint64_t monotonic_ms, int trusted);
static board_time_snapshot_t board_time_snapshot(void);
static void board_time_mark_pending(void);
static board_time_commit_result_t board_time_commit_locked(
    int hour, int minute, int year, int month, int day, int second,
    const pj_time_clock_anchor_t *target_anchor, int anchor_at_rtc_write,
    int update_utc_offset, int utc_offset_minutes,
    int publish_time_sync_known);
static int board_time_model_clock(pj_time_clock_t *clock);
static int time_state_initialize(void);
static int time_state_project(pj_ui_context_t *ui);
static int rtc_wake_sync(void);
static int rtc_wake_sync_locked(void);
static pj_rtc_wake_result_t rtc_wake_disarm_board(uint8_t *flags, int force);
static int settings_codec_volume_snapshot(void);
static void alert_audio_project(const pj_time_alert_t *alert,
                                pj_time_conflict_action_t action);
static void alert_audio_set_recording(int recording);
static void alert_audio_set_volume(int codec_volume);
static esp_err_t aux_task_start(void);
static esp_err_t board_event_queue_ensure(void);
static void board_queue_event(const pj_board_event_t *event);

static pj_alert_audio_t g_alert_audio;

enum {
    BOARD_UPDATE_TIME = (1U << 0),
    BOARD_UPDATE_SETTINGS = (1U << 1),
    BOARD_UPDATE_AUDIO = (1U << 2),
    BOARD_UPDATE_NOTES = (1U << 3),
};

/* Task-context only. No ISR path takes these mutexes. */
static void board_status_take(void)
{
    if (g_status_lock != NULL) {
        (void)xSemaphoreTakeRecursive(g_status_lock, portMAX_DELAY);
    }
}

static void board_status_give(void)
{
    if (g_status_lock != NULL) {
        (void)xSemaphoreGiveRecursive(g_status_lock);
    }
}

static pj_board_status_t board_status_snapshot_base(void)
{
    pj_board_status_t status;
    board_status_take();
    status = g_status;
    board_status_give();
    return status;
}

static void board_status_set_error(const char *format, ...)
{
    char message[sizeof(g_status.last_error)];
    va_list args;
    va_start(args, format);
    (void)vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    board_status_take();
    (void)snprintf(g_status.last_error, sizeof(g_status.last_error), "%s",
                   message);
    board_status_give();
}

static void ble_state_set(const char *format, ...)
{
    char state[sizeof(g_ble_state)];
    va_list args;
    va_start(args, format);
    (void)vsnprintf(state, sizeof(state), format, args);
    va_end(args);
    portENTER_CRITICAL(&g_credentials_lock);
    (void)snprintf(g_ble_state, sizeof(g_ble_state), "%s", state);
    portEXIT_CRITICAL(&g_credentials_lock);
}

static void ble_state_snapshot(char *state, size_t capacity)
{
    if (state == NULL || capacity == 0) {
        return;
    }
    portENTER_CRITICAL(&g_credentials_lock);
    (void)snprintf(state, capacity, "%s", g_ble_state);
    portEXIT_CRITICAL(&g_credentials_lock);
}

static int ble_provision_claim(void)
{
    int claimed = 0;
    portENTER_CRITICAL(&g_credentials_lock);
    if (!g_ble_provision_active) {
        g_ble_provision_active = 1;
        claimed = 1;
    }
    portEXIT_CRITICAL(&g_credentials_lock);
    return claimed;
}

static void ble_provision_release(void)
{
    portENTER_CRITICAL(&g_credentials_lock);
    g_ble_provision_active = 0;
    portEXIT_CRITICAL(&g_credentials_lock);
}

static int wifi_credentials_stored_snapshot(void)
{
    int stored;
    portENTER_CRITICAL(&g_credentials_lock);
    stored = g_wifi_credentials_stored;
    portEXIT_CRITICAL(&g_credentials_lock);
    return stored;
}

static void board_status_set_error_if_empty(const char *message)
{
    board_status_take();
    if (g_status.last_error[0] == '\0') {
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error), "%s",
                       message);
    }
    board_status_give();
}

static void board_status_clear_error_if_equal(const char *message)
{
    board_status_take();
    if (strcmp(g_status.last_error, message) == 0) {
        g_status.last_error[0] = '\0';
    }
    board_status_give();
}

static void board_status_clear_error_if_owned(const char *prefix,
                                              const char *alternate_prefix)
{
    board_status_take();
    int owned = prefix != NULL &&
        strncmp(g_status.last_error, prefix, strlen(prefix)) == 0;
    if (!owned && alternate_prefix != NULL) {
        owned = strncmp(g_status.last_error, alternate_prefix,
                        strlen(alternate_prefix)) == 0;
    }
    if (owned) {
        g_status.last_error[0] = '\0';
    }
    board_status_give();
}

typedef enum {
    BOARD_SERVICE_DISPLAY,
    BOARD_SERVICE_STORAGE,
    BOARD_SERVICE_AUDIO,
    BOARD_SERVICE_BLE,
    BOARD_SERVICE_WIFI,
    BOARD_SERVICE_HTTP,
} board_service_field_t;

static void board_status_set_service(board_service_field_t field,
                                     pj_board_service_state_t state)
{
    board_status_take();
    switch (field) {
    case BOARD_SERVICE_DISPLAY: g_status.display = state; break;
    case BOARD_SERVICE_STORAGE: g_status.storage = state; break;
    case BOARD_SERVICE_AUDIO: g_status.audio = state; break;
    case BOARD_SERVICE_BLE: g_status.ble_provisioning = state; break;
    case BOARD_SERVICE_WIFI: g_status.wifi = state; break;
    case BOARD_SERVICE_HTTP: g_status.http = state; break;
    }
    board_status_give();
}

static void board_update_publish(EventBits_t bit)
{
    if (g_board_update_events != NULL) {
        (void)xEventGroupSetBits(g_board_update_events, bit);
    }
}

static int board_update_pending(EventBits_t bit)
{
    return g_board_update_events != NULL &&
           (xEventGroupGetBits(g_board_update_events) & bit) != 0U;
}

static int board_update_take(EventBits_t bit)
{
    return g_board_update_events != NULL &&
           (xEventGroupClearBits(g_board_update_events, bit) & bit) != 0U;
}

static int audio_lifecycle_take(void)
{
    return g_audio_lifecycle_lock != NULL &&
           xSemaphoreTake(g_audio_lifecycle_lock, portMAX_DELAY) == pdTRUE;
}

static void audio_lifecycle_give(void)
{
    if (g_audio_lifecycle_lock != NULL) {
        xSemaphoreGive(g_audio_lifecycle_lock);
    }
}

static int audio_lifecycle_active(void)
{
    int active = 1;
    if (audio_lifecycle_take()) {
        active = pj_audio_lifecycle_active(&g_audio_lifecycle);
        audio_lifecycle_give();
    }
    return active;
}

static int storage_shared_try_acquire(void)
{
    int acquired;
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    acquired = pj_storage_shared_try_acquire(&g_storage_coordinator);
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
    return acquired;
}

static void storage_shared_release(void)
{
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    pj_storage_shared_release(&g_storage_coordinator);
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
}

static int record_storage_try_acquire(void)
{
    int acquired = 0;
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    if (!g_record_storage_owned && pj_storage_shared_try_acquire(&g_storage_coordinator)) {
        g_record_storage_owned = 1;
        acquired = 1;
    }
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
    return acquired;
}

static void record_storage_release(void)
{
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    if (g_record_storage_owned) {
        g_record_storage_owned = 0;
        pj_storage_shared_release(&g_storage_coordinator);
    }
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
}

static int storage_audio_publication_try_begin(void)
{
    int acquired;
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    acquired = pj_storage_audio_publication_try_begin(
        &g_storage_coordinator);
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
    return acquired;
}

static void storage_audio_publication_finish(void)
{
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    pj_storage_audio_publication_finish(&g_storage_coordinator);
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
}

static int playback_storage_try_acquire(void)
{
    int acquired = 0;
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    if (!g_playback_storage_owned && pj_storage_shared_try_acquire(&g_storage_coordinator)) {
        g_playback_storage_owned = 1;
        acquired = 1;
    }
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
    return acquired;
}

static void playback_storage_release(void)
{
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    if (g_playback_storage_owned) {
        g_playback_storage_owned = 0;
        pj_storage_shared_release(&g_storage_coordinator);
    }
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
}

typedef struct {
    pj_wipe_status_t current;
    pj_wipe_status_t history[PJ_STORAGE_WIPE_HISTORY_CAPACITY];
    size_t history_count;
} storage_wipe_snapshot_t;

static storage_wipe_snapshot_t storage_wipe_snapshot(void)
{
    storage_wipe_snapshot_t snapshot = {0};
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    snapshot.current = pj_storage_wipe_status(&g_storage_coordinator);
    snapshot.history_count = pj_storage_wipe_history(
        &g_storage_coordinator, snapshot.history,
        PJ_STORAGE_WIPE_HISTORY_CAPACITY);
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
    return snapshot;
}

static int storage_sleep_try_begin(void)
{
    int acquired;
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    acquired = pj_storage_sleep_try_begin(&g_storage_coordinator);
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
    return acquired;
}

static void storage_sleep_finish(void)
{
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    pj_storage_sleep_finish(&g_storage_coordinator);
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
}

static int ota_mutations_reserve(void)
{
    int acquired;
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    acquired = pj_storage_ota_try_begin(&g_storage_coordinator);
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
    return acquired;
}

static void ota_mutations_release(void)
{
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    pj_storage_ota_finish(&g_storage_coordinator);
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
}

static void storage_sync_counts_cache(int pending, int transferred)
{
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    g_cached_pending_sync = pending;
    g_cached_transferred_sync = transferred;
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
}

static void storage_sync_counts_snapshot(int *pending, int *transferred)
{
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    *pending = g_cached_pending_sync;
    *transferred = g_cached_transferred_sync;
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
}

static void board_audio_state_set(int recording, int playback_active)
{
    board_status_take();
    if (recording >= 0) {
        g_status.recording = recording;
    }
    if (playback_active >= 0) {
        g_status.playback_active = playback_active;
    }
    board_status_give();
}

static int recording_state_start(void)
{
    int started;
    portENTER_CRITICAL(&g_recording_lock);
    started = pj_recording_start(&g_recording, PJ_AUDIO_SAMPLE_RATE,
                                 PJ_AUDIO_CHANNELS, PJ_AUDIO_BITS_PER_SAMPLE);
    portEXIT_CRITICAL(&g_recording_lock);
    return started;
}

static int recording_state_commit(size_t bytes)
{
    int committed;
    portENTER_CRITICAL(&g_recording_lock);
    committed = pj_recording_commit(&g_recording, bytes);
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
        board_update_publish(BOARD_UPDATE_NOTES);
    } else {
        board_status_set_error_if_empty(
            "recording failed before a valid note was published");
    }
}

static pj_time_activity_t board_time_activity(void)
{
    pj_board_status_t status = board_status_snapshot_base();
    return status.recording ? PJ_TIME_ACTIVITY_RECORDING :
           status.playback_active ? PJ_TIME_ACTIVITY_PLAYBACK :
                                    PJ_TIME_ACTIVITY_IDLE;
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
        board_status_set_error("time alert audio prepare failed: %s",
                               esp_err_to_name(err));
        ESP_LOGW(TAG, "Time alert audio prepare failed: %s",
                 esp_err_to_name(err));
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
        board_status_set_error("time alert audio write failed: %s",
                               esp_err_to_name((esp_err_t)result));
        ESP_LOGW(TAG, "Time alert audio write failed: %s",
                 esp_err_to_name((esp_err_t)result));
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
        board_status_set_error("time alert audio cleanup failed: %s",
                               esp_err_to_name(result));
        ESP_LOGW(TAG, "Time alert audio cleanup failed: %s",
                 esp_err_to_name(result));
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

static void alert_audio_publish_ack(uint64_t alert_id)
{
    portENTER_CRITICAL(&g_alert_audio_intent_lock);
    g_alert_ack_pending = alert_id;
    portEXIT_CRITICAL(&g_alert_audio_intent_lock);
    board_update_publish(BOARD_UPDATE_AUDIO);
}

static uint64_t alert_audio_take_ack(void)
{
    uint64_t alert_id;
    portENTER_CRITICAL(&g_alert_audio_intent_lock);
    alert_id = g_alert_ack_pending;
    g_alert_ack_pending = 0;
    portEXIT_CRITICAL(&g_alert_audio_intent_lock);
    return alert_id;
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
    uint64_t reported_alert_id = 0;
    uint64_t failure_alert_id = 0;
    unsigned consecutive_failures = 0;
    while (1) {
        alert_audio_intent_t intent = alert_audio_intent_snapshot();
        alert_audio_apply_intent(&intent);
        pj_alert_audio_result_t result = pj_alert_audio_pump(&g_alert_audio, scratch);
        if (intent.alert_id == 0 && g_alert_audio.alert_id == 0 &&
            g_alert_audio.state != PJ_ALERT_AUDIO_PLAYING &&
            !g_alert_audio.cleanup_pending) {
            portENTER_CRITICAL(&g_alert_audio_intent_lock);
            if (g_alert_audio_intent.alert_id == 0 &&
                g_alert_audio_intent.stop_generation == intent.stop_generation) {
                g_alert_audio_stop_completed_generation = intent.stop_generation;
            }
            portEXIT_CRITICAL(&g_alert_audio_intent_lock);
        }
        if (g_alert_audio.alert_id != failure_alert_id) {
            failure_alert_id = g_alert_audio.alert_id;
            consecutive_failures = 0;
        }
        if (result < 0) {
            if (consecutive_failures < PJ_ALERT_AUDIO_FAILURE_ACK_THRESHOLD) {
                consecutive_failures++;
            }
        } else {
            consecutive_failures = 0;
        }
        uint64_t settled_id = 0;
        pj_alert_audio_kind_t settled_kind = 0;
        int settled = pj_alert_audio_settled(
            &g_alert_audio, &settled_id, &settled_kind);
        int alert_failed = consecutive_failures >=
            PJ_ALERT_AUDIO_FAILURE_ACK_THRESHOLD &&
            g_alert_audio.alert_id != 0;
        if ((settled || alert_failed) &&
            (settled ? settled_id : g_alert_audio.alert_id) != reported_alert_id) {
            settled_id = settled ? settled_id : g_alert_audio.alert_id;
            alert_audio_publish_ack(settled_id);
            reported_alert_id = settled_id;
        }
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
    int desired;
    portENTER_CRITICAL(&g_alert_audio_intent_lock);
    desired = g_alert_audio_intent.alert_id != 0 &&
        g_alert_audio_intent.alert_id != g_alert_ack_pending;
    portEXIT_CRITICAL(&g_alert_audio_intent_lock);
    return desired;
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

static uint32_t interval_reset_request(void)
{
    portENTER_CRITICAL(&g_alert_audio_intent_lock);
    g_alert_audio_intent.alert_id = 0;
    g_alert_audio_intent.kind = 0;
    g_alert_audio_intent.deferred = 0;
    g_alert_ack_pending = 0;
    uint32_t audio_generation = g_alert_audio_intent.stop_generation + 1U;
    if (audio_generation == 0U) {
        audio_generation = 1U;
    }
    g_alert_audio_intent.stop_generation = audio_generation;
    if (g_alert_audio_task == NULL) {
        g_alert_audio_stop_completed_generation = audio_generation;
    }
    portEXIT_CRITICAL(&g_alert_audio_intent_lock);
    alert_audio_notify();

    portENTER_CRITICAL(&g_interval_reset_lock);
    uint32_t generation = g_interval_reset.requested_generation + 1U;
    if (generation == 0U) {
        generation = 1U;
    }
    g_interval_reset.requested_generation = generation;
    g_interval_reset.audio_silenced = 0;
    portEXIT_CRITICAL(&g_interval_reset_lock);
    return generation;
}

static uint32_t interval_reset_take_pending(void)
{
    uint32_t generation = 0;
    portENTER_CRITICAL(&g_interval_reset_lock);
    if (g_interval_reset.requested_generation !=
        g_interval_reset.completed_generation) {
        generation = g_interval_reset.requested_generation;
    }
    portEXIT_CRITICAL(&g_interval_reset_lock);
    return generation;
}

static void interval_reset_complete(
    uint32_t generation, const pj_time_controller_result_t *result,
    int interval_active_before, int interval_active_after)
{
    int persistence_ok = result != NULL && result->command_attempted &&
        result->persistence_attempted &&
        result->save_result == PJ_TIME_CONTROLLER_SAVE_OK;
    portENTER_CRITICAL(&g_interval_reset_lock);
    g_interval_reset.completed_generation = generation;
    g_interval_reset.state_changed = result != NULL && result->state_changed;
    g_interval_reset.persistence_ok = persistence_ok;
    g_interval_reset.interval_active_before = interval_active_before != 0;
    g_interval_reset.interval_active_after = interval_active_after != 0;
    portEXIT_CRITICAL(&g_interval_reset_lock);
}

static int interval_reset_wait(uint32_t generation,
                               interval_reset_state_t *result)
{
    TickType_t deadline = xTaskGetTickCount() +
        pdMS_TO_TICKS(PJ_INTERVAL_RESET_WAIT_MS);
    while (1) {
        interval_reset_state_t snapshot;
        portENTER_CRITICAL(&g_interval_reset_lock);
        snapshot = g_interval_reset;
        portEXIT_CRITICAL(&g_interval_reset_lock);
        uint32_t audio_requested;
        uint32_t audio_completed;
        portENTER_CRITICAL(&g_alert_audio_intent_lock);
        audio_requested = g_alert_audio_intent.stop_generation;
        audio_completed = g_alert_audio_stop_completed_generation;
        portEXIT_CRITICAL(&g_alert_audio_intent_lock);
        if (snapshot.completed_generation == generation &&
            audio_requested == audio_completed) {
            snapshot.audio_silenced = 1;
            if (result != NULL) {
                *result = snapshot;
            }
            return 1;
        }
        if ((int32_t)(xTaskGetTickCount() - deadline) >= 0) {
            return 0;
        }
        vTaskDelay(pdMS_TO_TICKS(PJ_INTERVAL_RESET_POLL_MS));
    }
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
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL,
    };
    transaction.tx_data[0] = data;
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
    int credentials_stored = wifi_credentials_stored_snapshot();
    portENTER_CRITICAL(&g_connectivity_lock);
    pj_wifi_state_init(&g_wifi_state, credentials_stored, now_ms);
    pj_time_sync_init(&g_time_sync_state, g_time_wall_trusted, now_ms);
    g_time_sync_state.utc_offset_known = g_utc_offset_known != 0;
    g_time_sync_state.utc_offset_minutes = (int16_t)g_utc_offset_minutes;
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

static int time_transaction_take(void)
{
    return g_time_transaction_lock != NULL &&
           xSemaphoreTake(g_time_transaction_lock, portMAX_DELAY) == pdTRUE;
}

static void time_transaction_give(void)
{
    if (g_time_transaction_lock != NULL) {
        xSemaphoreGive(g_time_transaction_lock);
    }
}

/* Caller holds g_time_transaction_lock across RTC durability and publication. */
static pj_time_sync_publication_t publish_sntp_civil_time_locked(
    int64_t utc_epoch_s, uint64_t anchor_monotonic_ms)
{
    int offset_known;
    int offset_minutes;
    portENTER_CRITICAL(&g_connectivity_lock);
    offset_known = g_utc_offset_known;
    offset_minutes = g_utc_offset_minutes;
    portEXIT_CRITICAL(&g_connectivity_lock);
    if (!offset_known) {
        return PJ_TIME_SYNC_PUBLICATION_TIMEZONE_REQUIRED;
    }

    pj_time_civil_t civil;
    pj_time_clock_anchor_t target_anchor;
    if (!pj_time_civil_from_utc(utc_epoch_s, offset_minutes, &civil) ||
        !pj_time_clock_anchor_set(
            &target_anchor, civil.year, civil.month, civil.day, civil.hour,
            civil.minute, civil.second, anchor_monotonic_ms)) {
        return PJ_TIME_SYNC_PUBLICATION_CIVIL_FAILED;
    }

    board_time_commit_result_t commit = board_time_commit_locked(
        civil.hour, civil.minute, civil.year, civil.month, civil.day,
        civil.second, &target_anchor, 0, 0, 0, 0);
    switch (commit.transaction.status) {
    case PJ_TIME_TRANSACTION_OK:
        return PJ_TIME_SYNC_PUBLICATION_PUBLISHED;
    case PJ_TIME_TRANSACTION_RTC_SNAPSHOT_FAILED:
        board_status_set_error("SNTP RTC snapshot failed: %s",
                               esp_err_to_name(commit.rtc_snapshot_error));
        ESP_LOGW(TAG, "SNTP RTC snapshot failed: %s",
                 esp_err_to_name(commit.rtc_snapshot_error));
        return PJ_TIME_SYNC_PUBLICATION_RTC_FAILED;
    case PJ_TIME_TRANSACTION_RTC_FAILED_ROLLED_BACK:
        board_status_set_error(
            "SNTP RTC write failed and was rolled back: %s",
            esp_err_to_name(commit.rtc_write_error));
        ESP_LOGW(TAG, "SNTP RTC write failed and was rolled back: %s",
                 esp_err_to_name(commit.rtc_write_error));
        return PJ_TIME_SYNC_PUBLICATION_RTC_ROLLED_BACK;
    case PJ_TIME_TRANSACTION_PARTIAL_COMMIT:
        board_status_set_error(
            "SNTP RTC rollback failed; partial components=0x%" PRIx32,
            commit.transaction.partial_components);
        ESP_LOGE(TAG, "SNTP RTC rollback failed; partial components=0x%" PRIx32,
                 commit.transaction.partial_components);
        return PJ_TIME_SYNC_PUBLICATION_PARTIAL_COMMIT;
    case PJ_TIME_TRANSACTION_INVALID:
    case PJ_TIME_TRANSACTION_OFFSET_FAILED_ROLLED_BACK:
    default:
        return PJ_TIME_SYNC_PUBLICATION_CIVIL_FAILED;
    }
}

static void connectivity_sntp_process(const sntp_publication_event_t *event)
{
    if (event == NULL || !time_transaction_take()) {
        return;
    }

    const struct timeval *tv = &event->tv;
    uint64_t now_ms = event->received_monotonic_ms;
    if (tv->tv_usec < 0 || tv->tv_usec >= 1000000 ||
        !pj_time_sync_epoch_valid((int64_t)tv->tv_sec)) {
        portENTER_CRITICAL(&g_connectivity_lock);
        (void)pj_time_sync_on_success(&g_time_sync_state,
                                      (int64_t)tv->tv_sec,
                                      0, 0, now_ms);
        portEXIT_CRITICAL(&g_connectivity_lock);
        time_transaction_give();
        ESP_LOGW(TAG, "SNTP response rejected outside supported UTC range");
        return;
    }

    int old_valid;
    int64_t old_epoch_ms = 0;
    portENTER_CRITICAL(&g_connectivity_lock);
    old_valid = pj_time_sync_expected_epoch_ms(&g_time_sync_state, now_ms,
                                                &old_epoch_ms);
    portEXIT_CRITICAL(&g_connectivity_lock);

    uint64_t fractional_ms = (uint64_t)tv->tv_usec / 1000u;
    uint64_t anchor_ms = fractional_ms <= now_ms ? now_ms - fractional_ms : 0;
    pj_time_sync_publication_t publication = publish_sntp_civil_time_locked(
        (int64_t)tv->tv_sec, anchor_ms);
    portENTER_CRITICAL(&g_connectivity_lock);
    (void)pj_time_sync_on_success(&g_time_sync_state, (int64_t)tv->tv_sec,
                                  old_valid, old_epoch_ms, now_ms);
    if (g_wifi_state.has_ip) {
        pj_wifi_state_set_last_success_utc(&g_wifi_state,
                                           (int64_t)tv->tv_sec);
    }
    pj_time_sync_set_publication(&g_time_sync_state, publication);
    pj_time_sync_correction_t correction = g_time_sync_state.correction;
    portEXIT_CRITICAL(&g_connectivity_lock);
    time_transaction_give();
    ESP_LOGI(TAG, "SNTP UTC acquired (%s), civil publication=%s",
             pj_time_sync_correction_name(correction),
             pj_time_sync_publication_name(publication));
}

static void connectivity_sntp_publication_worker(void *arg)
{
    (void)arg;
    sntp_publication_event_t event;
    while (xQueueReceive(g_sntp_publication_queue, &event, portMAX_DELAY) ==
           pdTRUE) {
        connectivity_sntp_process(&event);
    }
    g_sntp_publication_task = NULL;
    vTaskDelete(NULL);
}

/* esp_netif invokes this only after its supported SNTP implementation has
 * updated the POSIX clock. Never perform RTC, NVS, or UI publication here. */
static void connectivity_sntp_callback(struct timeval *tv)
{
    if (g_sntp_publication_queue == NULL) {
        return;
    }
    sntp_publication_event_t event = {
        .received_monotonic_ms = board_monotonic_ms(),
    };
    if (tv != NULL) {
        event.tv = *tv;
    }
    (void)xQueueOverwrite(g_sntp_publication_queue, &event);
}

static esp_err_t connectivity_sntp_worker_ensure(void)
{
    if (g_sntp_publication_queue == NULL) {
        g_sntp_publication_queue = xQueueCreate(
            1, sizeof(sntp_publication_event_t));
        if (g_sntp_publication_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (g_sntp_publication_task == NULL) {
        BaseType_t created = xTaskCreate(
            connectivity_sntp_publication_worker, "pj-sntp-publish",
            PJ_SNTP_PUBLICATION_TASK_STACK, NULL, 4,
            &g_sntp_publication_task);
        if (created != pdPASS) {
            g_sntp_publication_task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static esp_err_t connectivity_sntp_ensure_initialized(void)
{
    if (g_sntp_initialized) {
        return ESP_OK;
    }
    esp_err_t worker_err = connectivity_sntp_worker_ensure();
    if (worker_err != ESP_OK) {
        return worker_err;
    }
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.start = false;
    config.wait_for_sync = false;
    config.smooth_sync = false;
    config.sync_cb = connectivity_sntp_callback;
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err == ESP_OK) {
        g_sntp_initialized = 1;
    }
    return err;
}

static esp_err_t wifi_disconnect_for_control(void)
{
    portENTER_CRITICAL(&g_connectivity_lock);
    g_wifi_control_disconnect_pending = 1;
    portEXIT_CRITICAL(&g_connectivity_lock);

    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK) {
        portENTER_CRITICAL(&g_connectivity_lock);
        g_wifi_control_disconnect_pending = 0;
        portEXIT_CRITICAL(&g_connectivity_lock);
        return err;
    }

    for (unsigned waited_ms = 0; waited_ms < PJ_WIFI_RECONNECT_SETTLE_MS;
         waited_ms += PJ_WIFI_CONTROL_EVENT_POLL_MS) {
        int pending;
        portENTER_CRITICAL(&g_connectivity_lock);
        pending = g_wifi_control_disconnect_pending;
        portEXIT_CRITICAL(&g_connectivity_lock);
        if (!pending) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(PJ_WIFI_CONTROL_EVENT_POLL_MS));
    }

    /* Never let a missing control event hide the next genuine failure. */
    portENTER_CRITICAL(&g_connectivity_lock);
    g_wifi_control_disconnect_pending = 0;
    portEXIT_CRITICAL(&g_connectivity_lock);
    return ESP_OK;
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
                (void)wifi_disconnect_for_control();
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
    pj_board_status_t status = board_status_snapshot_base();
    ESP_RETURN_ON_ERROR(mdns_hostname_set(status.device_id), TAG,
                        "mDNS hostname failed");
    ESP_RETURN_ON_ERROR(mdns_instance_name_set("Pocket Journal"), TAG, "mDNS instance failed");
    mdns_txt_item_t txt[] = {
        {"device_id", status.device_id},
        {"path", "/v1/status"},
    };
    ESP_RETURN_ON_ERROR(mdns_service_add(status.device_id, "_pocket-journal", "_tcp", 80,
                                         txt, sizeof(txt) / sizeof(txt[0])),
                        TAG, "mDNS service registration failed");
    g_mdns_started = 1;
    ESP_LOGI(TAG, "mDNS advertised: %s.local _pocket-journal._tcp",
             status.device_id);
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
        const wifi_event_sta_connected_t *event = event_data;
        unsigned channel = event == NULL ? 0U : (unsigned)event->channel;
        unsigned auth_mode = event == NULL ? 0U : (unsigned)event->authmode;
        portENTER_CRITICAL(&g_connectivity_lock);
        pj_wifi_state_on_associated(&g_wifi_state, board_monotonic_ms(),
                                    channel, auth_mode);
        portEXIT_CRITICAL(&g_connectivity_lock);
        ESP_LOGI(TAG, "Wi-Fi associated: channel=%u auth_mode=%u",
                 channel, auth_mode);
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = event_data;
        unsigned reason = event == NULL ? 0U : (unsigned)event->reason;
        int ap_observed = event != NULL && event->ssid_len > 0;
        int rssi_dbm = event == NULL ? -127 : event->rssi;
        int control_disconnect = 0;
        pj_wifi_phase_t failure_phase = PJ_WIFI_PHASE_DISCONNECTED;
        portENTER_CRITICAL(&g_connectivity_lock);
        if (g_wifi_control_disconnect_pending) {
            /* A DHCP/reprovision reconnect is transport control, not a new
             * connection failure to classify or count. */
            g_wifi_control_disconnect_pending = 0;
            control_disconnect = 1;
        } else {
            pj_wifi_state_on_disconnected(&g_wifi_state, reason,
                                          ap_observed, rssi_dbm,
                                          board_monotonic_ms());
            failure_phase = g_wifi_state.phase;
        }
        pj_time_sync_on_network_lost(&g_time_sync_state,
                                     board_monotonic_ms());
        portEXIT_CRITICAL(&g_connectivity_lock);
        board_status_take();
        g_status.wifi = PJ_BOARD_SERVICE_UNAVAILABLE;
        (void)snprintf(g_status.ip_addr, sizeof(g_status.ip_addr), "0.0.0.0");
        if (!control_disconnect) {
            (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                           "Wi-Fi %s (reason %u); retry scheduled",
                           pj_wifi_phase_name(failure_phase), reason);
        }
        board_status_give();
        if (!control_disconnect) {
            ESP_LOGW(TAG,
                     "Wi-Fi disconnected: reason=%u phase=%s ap_visible=%d rssi=%d",
                     reason, pj_wifi_phase_name(failure_phase),
                     ap_observed, rssi_dbm);
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
        board_status_take();
        g_status.wifi = PJ_BOARD_SERVICE_READY;
        if (event != NULL) {
            (void)snprintf(g_status.ip_addr, sizeof(g_status.ip_addr), IPSTR,
                           IP2STR(&event->ip_info.ip));
        }
        if (strncmp(g_status.last_error, "Wi-Fi ", 6) == 0) {
            g_status.last_error[0] = '\0';
        }
        char ip_addr[sizeof(g_status.ip_addr)];
        (void)snprintf(ip_addr, sizeof(ip_addr), "%s", g_status.ip_addr);
        board_status_give();
        ESP_LOGI(TAG, "Wi-Fi connected: ip=%s rssi=%d channel=%u",
                 ip_addr, rssi_dbm, channel);
        esp_err_t mdns_err = mdns_start();
        if (mdns_err != ESP_OK) {
            board_status_set_error("mDNS start failed: %s",
                                   esp_err_to_name(mdns_err));
            ESP_LOGW(TAG, "mDNS start failed: %s", esp_err_to_name(mdns_err));
        }
        return;
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) {
        portENTER_CRITICAL(&g_connectivity_lock);
        pj_wifi_state_on_lost_ip(&g_wifi_state, board_monotonic_ms());
        pj_time_sync_on_network_lost(&g_time_sync_state,
                                     board_monotonic_ms());
        portEXIT_CRITICAL(&g_connectivity_lock);
        board_status_take();
        g_status.wifi = PJ_BOARD_SERVICE_UNAVAILABLE;
        (void)snprintf(g_status.ip_addr, sizeof(g_status.ip_addr), "0.0.0.0");
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "Wi-Fi lost its DHCP lease; retry scheduled");
        board_status_give();
    }
}

static esp_err_t wifi_apply_config(void)
{
    char ssid[sizeof(g_wifi_ssid)];
    char password[sizeof(g_wifi_password)];
    portENTER_CRITICAL(&g_credentials_lock);
    (void)snprintf(ssid, sizeof(ssid), "%s", g_wifi_ssid);
    (void)snprintf(password, sizeof(password), "%s", g_wifi_password);
    portEXIT_CRITICAL(&g_credentials_lock);
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, strlen(ssid));
    memcpy(config.sta.password, password, strlen(password));
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    config.sta.disable_wpa3_compatible_mode = true;
    config.sta.failure_retry_cnt = 3;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    memset(password, 0, sizeof(password));
    return err;
}

static esp_err_t wifi_start_or_reconfigure(void)
{
    if (!wifi_credentials_stored_snapshot()) {
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
        (void)wifi_disconnect_for_control();
        ESP_RETURN_ON_ERROR(wifi_apply_config(), TAG, "Wi-Fi station reconfiguration failed");
    }
    portENTER_CRITICAL(&g_connectivity_lock);
    pj_wifi_state_set_provisioned(&g_wifi_state, 1, board_monotonic_ms());
    pj_wifi_state_on_driver_started(&g_wifi_state, board_monotonic_ms());
    portEXIT_CRITICAL(&g_connectivity_lock);
    ESP_RETURN_ON_ERROR(connectivity_runtime_start(), TAG,
                        "connectivity task start failed");
    board_status_take();
    g_status.wifi = PJ_BOARD_SERVICE_UNAVAILABLE;
    (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                   "Wi-Fi connecting");
    board_status_give();
    return ESP_OK;
}

static void wifi_apply_provisioning_status(void)
{
    if (!wifi_credentials_stored_snapshot()) {
        return;
    }
    board_status_take();
    g_status.ble_provisioning = g_ble_started ? PJ_BOARD_SERVICE_READY :
        PJ_BOARD_SERVICE_UNAVAILABLE;
    if (g_status.wifi != PJ_BOARD_SERVICE_READY) {
        g_status.wifi = PJ_BOARD_SERVICE_UNAVAILABLE;
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error), "Wi-Fi credentials stored");
    }
    board_status_give();
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

static void time_offset_load(void)
{
    nvs_handle_t nvs;
    if (nvs_open(PJ_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    int32_t stored = 0;
    esp_err_t err = nvs_get_i32(nvs, PJ_NVS_UTC_OFFSET, &stored);
    nvs_close(nvs);
    if (err == ESP_OK && pj_time_utc_offset_valid((int)stored)) {
        g_utc_offset_minutes = (int)stored;
        g_utc_offset_known = 1;
        ESP_LOGI(TAG, "Fixed UTC offset loaded from NVS: %+d minutes",
                 g_utc_offset_minutes);
    } else if (err == ESP_OK) {
        ESP_LOGW(TAG, "Ignoring invalid fixed UTC offset in NVS: %" PRId32,
                 stored);
    }
}

static esp_err_t time_offset_store_state(int known, int offset_minutes)
{
    if ((known != 0 && known != 1) ||
        (known && !pj_time_utc_offset_valid(offset_minutes))) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PJ_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = known
        ? nvs_set_i32(nvs, PJ_NVS_UTC_OFFSET, (int32_t)offset_minutes)
        : nvs_erase_key(nvs, PJ_NVS_UTC_OFFSET);
    if (!known && err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
        nvs_close(nvs);
        return err;
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static void time_offset_publish(int offset_minutes)
{
    portENTER_CRITICAL(&g_connectivity_lock);
    g_utc_offset_minutes = offset_minutes;
    g_utc_offset_known = 1;
    if (g_connectivity_state_initialized) {
        g_time_sync_state.utc_offset_known = 1;
        g_time_sync_state.utc_offset_minutes = (int16_t)offset_minutes;
        if (g_time_sync_state.publication ==
            PJ_TIME_SYNC_PUBLICATION_TIMEZONE_REQUIRED) {
            g_time_sync_state.publication = PJ_TIME_SYNC_PUBLICATION_NOT_READY;
        }
    }
    portEXIT_CRITICAL(&g_connectivity_lock);
    ESP_LOGI(TAG, "Fixed UTC offset persisted: %+d minutes", offset_minutes);
}

static int provisioned_token_read(char *token, size_t capacity)
{
    if (token == NULL || capacity == 0U) {
        return 0;
    }
    int available = 0;
    portENTER_CRITICAL(&g_credentials_lock);
    available = pj_auth_copy_provisioned_token(
        g_wifi_credentials_stored, g_provisioned_token, token, capacity);
    portEXIT_CRITICAL(&g_credentials_lock);
    return available;
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
    char token[sizeof(g_status.token)] = {0};
    size_t token_len = sizeof(token);
    int have_ssid = nvs_get_str(nvs, PJ_NVS_WIFI_SSID, g_wifi_ssid, &ssid_len) == ESP_OK && g_wifi_ssid[0] != '\0';
    int have_password = nvs_get_str(nvs, PJ_NVS_WIFI_PASSWORD, g_wifi_password, &password_len) == ESP_OK;
    int have_token = nvs_get_str(nvs, PJ_NVS_TOKEN, token, &token_len) == ESP_OK &&
                     token[0] != '\0';
    nvs_close(nvs);

    if (have_ssid && have_password && have_token) {
        portENTER_CRITICAL(&g_credentials_lock);
        (void)snprintf(g_provisioned_token, sizeof(g_provisioned_token), "%s",
                       token);
        g_wifi_credentials_stored = 1;
        portEXIT_CRITICAL(&g_credentials_lock);
        board_status_take();
        (void)snprintf(g_status.token, sizeof(g_status.token), "%s", token);
        board_status_give();
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
    if (g_provisioning_lock == NULL ||
        xSemaphoreTake(g_provisioning_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PJ_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        xSemaphoreGive(g_provisioning_lock);
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
        xSemaphoreGive(g_provisioning_lock);
        return err;
    }

    portENTER_CRITICAL(&g_credentials_lock);
    (void)snprintf(g_wifi_ssid, sizeof(g_wifi_ssid), "%s", ssid);
    (void)snprintf(g_wifi_password, sizeof(g_wifi_password), "%s", password);
    (void)snprintf(g_provisioned_token, sizeof(g_provisioned_token), "%s",
                   token);
    g_wifi_credentials_stored = 1;
    portEXIT_CRITICAL(&g_credentials_lock);
    board_status_take();
    (void)snprintf(g_status.token, sizeof(g_status.token), "%s", token);
    board_status_give();
    wifi_apply_provisioning_status();
    ESP_LOGI(TAG, "Wi-Fi credentials stored from partner provisioning");
    esp_err_t connect_err = wifi_start_or_reconfigure();
    xSemaphoreGive(g_provisioning_lock);
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
        ble_state_set("stored");
        board_status_set_service(BOARD_SERVICE_BLE, PJ_BOARD_SERVICE_READY);
    } else {
        ble_state_set("error-%s", esp_err_to_name(err));
        board_status_set_service(BOARD_SERVICE_BLE, PJ_BOARD_SERVICE_ERROR);
    }
    memset(args, 0, sizeof(*args));
    free(args);
    ble_provision_release();
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
        char ble_state[sizeof(g_ble_state)];
        ble_state_snapshot(ble_state, sizeof(ble_state));
        pj_board_status_t status = board_status_snapshot_base();
        int length = snprintf(json, sizeof(json),
                              "{\"device_id\":\"%s\",\"state\":\"%s\",\"wifi\":\"%s\"}",
                              status.device_id, ble_state,
                              service_name(status.wifi));
        return os_mbuf_append(ctxt->om, json, (uint16_t)length) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    if (!ble_connection_encrypted(conn_handle)) {
        ble_state_set("pairing-required");
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
    if (!ble_provision_claim()) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    ble_provision_args_t *args = malloc(sizeof(*args));
    if (args == NULL) {
        ble_provision_release();
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    (void)snprintf(args->ssid, sizeof(args->ssid), "%s", g_ble_ssid);
    (void)snprintf(args->password, sizeof(args->password), "%s", g_ble_password);
    (void)snprintf(args->token, sizeof(args->token), "%s", g_ble_token);
    ble_state_set("applying");
    if (xTaskCreate(ble_provision_task, "pj-ble-provision", 4096, args, 4,
                    NULL) != pdPASS) {
        free(args);
        ble_provision_release();
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
    ble_state_set("advertising");
    board_status_set_service(BOARD_SERVICE_BLE, PJ_BOARD_SERVICE_READY);
    ESP_LOGI(TAG, "BLE provisioning advertising as %s", name);
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_CONNECT) {
        if (event->connect.status != 0) {
            ble_advertise();
        } else {
            ble_state_set("pairing-required");
        }
    } else if (event->type == BLE_GAP_EVENT_ENC_CHANGE) {
        struct ble_gap_conn_desc desc;
        if (event->enc_change.status == 0 &&
            ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0 &&
            desc.sec_state.encrypted) {
            ble_state_set("paired");
        } else {
            ble_state_set("pairing-required");
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
        board_status_set_service(BOARD_SERVICE_BLE, PJ_BOARD_SERVICE_ERROR);
        ble_state_set("address-error");
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
    pj_board_status_t status = board_status_snapshot_base();
    (void)snprintf(name, sizeof(name), "PJ-%.6s", status.device_id + 3);
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
    board_status_set_service(BOARD_SERVICE_BLE, PJ_BOARD_SERVICE_READY);
    return ESP_OK;
}

static esp_err_t epd_write_bytes(const uint8_t *data, int len)
{
    if (data == NULL || len <= 0 || !esp_ptr_dma_capable(data) ||
        ((uintptr_t)data & (sizeof(uint32_t) - 1u)) != 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    spi_transaction_t transaction = {
        .length = (size_t)len * 8u,
        .tx_buffer = data,
        .flags = SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL,
    };
    ESP_RETURN_ON_ERROR(gpio_set_level(EPD_DC_PIN, 1), TAG,
                        "display data select failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(EPD_CS_PIN, 0), TAG,
                        "display chip select failed");
    esp_err_t transmit_err = spi_device_polling_transmit(g_epd_spi, &transaction);
    esp_err_t deselect_err = gpio_set_level(EPD_CS_PIN, 1);
    return transmit_err != ESP_OK ? transmit_err : deselect_err;
}

static esp_err_t epd_write_byte(int data_mode, uint8_t value)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(EPD_DC_PIN, data_mode), TAG,
                        "display byte mode select failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(EPD_CS_PIN, 0), TAG,
                        "display chip select failed");
    esp_err_t transmit_err = epd_spi_byte(value);
    esp_err_t deselect_err = gpio_set_level(EPD_CS_PIN, 1);
    return transmit_err != ESP_OK ? transmit_err : deselect_err;
}

static esp_err_t epd_send_command(uint8_t command)
{
    return epd_write_byte(0, command);
}

static esp_err_t epd_send_data(uint8_t data)
{
    return epd_write_byte(1, data);
}

static void epd_record_busy_time(int64_t elapsed_us)
{
    if (elapsed_us <= 0) {
        return;
    }
    uint32_t elapsed = elapsed_us > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_us;
    if (UINT32_MAX - g_epd_refresh_busy_us < elapsed) {
        g_epd_refresh_busy_us = UINT32_MAX;
    } else {
        g_epd_refresh_busy_us += elapsed;
    }
}

static esp_err_t epd_wait_busy(void)
{
    int64_t started_us = esp_timer_get_time();
    while (gpio_get_level(EPD_BUSY_PIN) == 1) {
        int64_t elapsed_us = esp_timer_get_time() - started_us;
        if (elapsed_us >= PJ_EPD_BUSY_TIMEOUT_US) {
            epd_record_busy_time(elapsed_us);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(PJ_EPD_BUSY_POLL_TICKS);
    }
    epd_record_busy_time(esp_timer_get_time() - started_us);
    return ESP_OK;
}

static esp_err_t epd_set_windows(uint16_t x_start, uint16_t y_start,
                                 uint16_t x_end, uint16_t y_end)
{
    ESP_RETURN_ON_ERROR(epd_send_command(0x44), TAG, "display X window command failed");
    ESP_RETURN_ON_ERROR(epd_send_data((x_start >> 3) & 0xFF), TAG,
                        "display X window start failed");
    ESP_RETURN_ON_ERROR(epd_send_data((x_end >> 3) & 0xFF), TAG,
                        "display X window end failed");
    ESP_RETURN_ON_ERROR(epd_send_command(0x45), TAG, "display Y window command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(y_start & 0xFF), TAG,
                        "display Y window start low failed");
    ESP_RETURN_ON_ERROR(epd_send_data((y_start >> 8) & 0xFF), TAG,
                        "display Y window start high failed");
    ESP_RETURN_ON_ERROR(epd_send_data(y_end & 0xFF), TAG,
                        "display Y window end low failed");
    ESP_RETURN_ON_ERROR(epd_send_data((y_end >> 8) & 0xFF), TAG,
                        "display Y window end high failed");
    return ESP_OK;
}

static esp_err_t epd_set_cursor(uint16_t x_start, uint16_t y_start)
{
    ESP_RETURN_ON_ERROR(epd_send_command(0x4E), TAG, "display X cursor command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(x_start & 0xFF), TAG, "display X cursor failed");
    ESP_RETURN_ON_ERROR(epd_send_command(0x4F), TAG, "display Y cursor command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(y_start & 0xFF), TAG,
                        "display Y cursor low failed");
    ESP_RETURN_ON_ERROR(epd_send_data((y_start >> 8) & 0xFF), TAG,
                        "display Y cursor high failed");
    return epd_wait_busy();
}

static esp_err_t epd_set_lut(const uint8_t *lut)
{
    if (lut == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(g_epd_lut_buffer, lut, PJ_EPD_LUT_TRANSFER_BYTES);
    ESP_RETURN_ON_ERROR(epd_send_command(0x32), TAG, "display LUT command failed");
    ESP_RETURN_ON_ERROR(epd_write_bytes(g_epd_lut_buffer, PJ_EPD_LUT_TRANSFER_BYTES),
                        TAG, "display LUT transfer failed");
    ESP_RETURN_ON_ERROR(epd_wait_busy(), TAG, "display LUT busy timeout");

    ESP_RETURN_ON_ERROR(epd_send_command(0x3F), TAG, "display LUT gate command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(lut[153]), TAG, "display LUT gate data failed");
    ESP_RETURN_ON_ERROR(epd_send_command(0x03), TAG, "display LUT voltage command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(lut[154]), TAG, "display LUT voltage data failed");
    ESP_RETURN_ON_ERROR(epd_send_command(0x04), TAG, "display LUT source command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(lut[155]), TAG, "display LUT source data 0 failed");
    ESP_RETURN_ON_ERROR(epd_send_data(lut[156]), TAG, "display LUT source data 1 failed");
    ESP_RETURN_ON_ERROR(epd_send_data(lut[157]), TAG, "display LUT source data 2 failed");
    ESP_RETURN_ON_ERROR(epd_send_command(0x2C), TAG, "display LUT VCOM command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(lut[158]), TAG, "display LUT VCOM data failed");
    return ESP_OK;
}

static esp_err_t epd_turn_on_display(void)
{
    ESP_RETURN_ON_ERROR(epd_send_command(0x22), TAG, "display control command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(0xC7), TAG, "display control data failed");
    ESP_RETURN_ON_ERROR(epd_send_command(0x20), TAG, "display activate command failed");
    return epd_wait_busy();
}

static esp_err_t epd_turn_on_display_part(void)
{
    ESP_RETURN_ON_ERROR(epd_send_command(0x22), TAG,
                        "display partial control command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(0xCF), TAG, "display partial control data failed");
    ESP_RETURN_ON_ERROR(epd_send_command(0x20), TAG,
                        "display partial activate command failed");
    return epd_wait_busy();
}

static esp_err_t epd_prepare_partial(void)
{
    if (g_epd_partial_ready) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Display partial mode init");
    ESP_RETURN_ON_ERROR(epd_send_command(0x11), TAG,
                        "display partial data-entry command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(0x01), TAG,
                        "display partial data-entry mode failed");
    ESP_RETURN_ON_ERROR(epd_set_lut(WF_PARTIAL_1IN54), TAG,
                        "display partial LUT setup failed");
    ESP_RETURN_ON_ERROR(epd_send_command(0x37), TAG,
                        "display partial option command failed");
    const uint8_t partial_options[] = {0x00, 0x00, 0x00, 0x00, 0x00,
                                       0x40, 0x00, 0x00, 0x00, 0x00};
    for (size_t i = 0; i < sizeof(partial_options); i++) {
        ESP_RETURN_ON_ERROR(epd_send_data(partial_options[i]), TAG,
                            "display partial option data failed");
    }
    ESP_RETURN_ON_ERROR(epd_send_command(0x3C), TAG,
                        "display partial border command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(0x80), TAG,
                        "display partial border data failed");
    g_epd_partial_ready = 1;
    return ESP_OK;
}

static esp_err_t epd_controller_configure(void)
{
    g_epd_partial_ready = 0;
    g_epd_refresh_busy_us = 0;

    ESP_RETURN_ON_ERROR(gpio_set_level(EPD_CS_PIN, 1), TAG,
                        "display deselect during reset failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(EPD_RST_PIN, 1), TAG,
                        "display reset high failed");
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_RETURN_ON_ERROR(gpio_set_level(EPD_RST_PIN, 0), TAG,
                        "display reset low failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(gpio_set_level(EPD_RST_PIN, 1), TAG,
                        "display reset release failed");
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_RETURN_ON_ERROR(epd_wait_busy(), TAG, "display reset busy timeout");
    ESP_RETURN_ON_ERROR(epd_send_command(0x12), TAG, "display soft reset failed");
    ESP_RETURN_ON_ERROR(epd_wait_busy(), TAG, "display soft reset busy timeout");
    ESP_RETURN_ON_ERROR(epd_send_command(0x01), TAG, "display driver output command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(0xC7), TAG, "display driver output low failed");
    ESP_RETURN_ON_ERROR(epd_send_data(0x00), TAG, "display driver output high failed");
    ESP_RETURN_ON_ERROR(epd_send_data(0x01), TAG, "display driver direction failed");
    ESP_RETURN_ON_ERROR(epd_send_command(0x11), TAG, "display data-entry command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(0x01), TAG, "display data-entry mode failed");
    ESP_RETURN_ON_ERROR(epd_set_windows(0, PJ_DISPLAY_WIDTH - 1,
                                        PJ_DISPLAY_HEIGHT - 1, 0),
                        TAG, "display base window failed");
    ESP_RETURN_ON_ERROR(epd_send_command(0x3C), TAG, "display border command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(0x01), TAG, "display border data failed");
    ESP_RETURN_ON_ERROR(epd_send_command(0x18), TAG, "display temperature command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(0x80), TAG, "display temperature data failed");
    ESP_RETURN_ON_ERROR(epd_send_command(0x22), TAG, "display load control command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(0xB1), TAG, "display load control data failed");
    ESP_RETURN_ON_ERROR(epd_send_command(0x20), TAG, "display load activate failed");
    ESP_RETURN_ON_ERROR(epd_wait_busy(), TAG, "display load busy timeout");
    ESP_RETURN_ON_ERROR(epd_set_cursor(0, PJ_DISPLAY_HEIGHT - 1), TAG,
                        "display base cursor failed");
    ESP_RETURN_ON_ERROR(epd_set_lut(WF_FULL_1IN54), TAG,
                        "display full LUT setup failed");
    return ESP_OK;
}

static esp_err_t display_init(void)
{
    g_epd_shadow_valid = 0;
    g_epd_partial_ready = 0;
    pj_display_refresh_policy_init(&g_epd_refresh_policy,
                                   PJ_DISPLAY_REFRESH_DEFAULT_PARTIAL_LIMIT);

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

    if (!esp_ptr_dma_capable(g_epd_buffer) || !esp_ptr_dma_capable(g_epd_lut_buffer)) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(epd_controller_configure(), TAG,
                        "display controller configure failed");

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

typedef struct {
    uint16_t x;
    uint16_t y;
} epd_partial_plane_context_t;

static int epd_partial_position(void *opaque)
{
    const epd_partial_plane_context_t *context = opaque;
    return epd_set_cursor(context->x, context->y);
}

static int epd_partial_command(void *opaque, uint8_t command)
{
    (void)opaque;
    return epd_send_command(command);
}

static int epd_partial_write(void *opaque, const uint8_t *data, size_t length)
{
    (void)opaque;
    if (length > INT_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    return epd_write_bytes(data, (int)length);
}

static int epd_partial_activate(void *opaque)
{
    (void)opaque;
    return epd_turn_on_display_part();
}

static esp_err_t epd_refresh_partial(const pj_framebuffer_t *framebuffer,
                                     const pj_display_refresh_plan_t *plan)
{
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    int byte_len = framebuffer_region_to_epd(framebuffer, &plan->region,
                                             &x0, &y0, &x1, &y1);
    if (byte_len <= 0) {
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t mem_y_start = (uint16_t)(PJ_DISPLAY_HEIGHT - 1 - y0);
    uint16_t mem_y_end = (uint16_t)(PJ_DISPLAY_HEIGHT - 1 - y1);
    if (!g_epd_partial_ready) {
        ESP_RETURN_ON_ERROR(epd_set_windows((uint16_t)x0, mem_y_start,
                                            (uint16_t)x1, mem_y_end),
                            TAG, "display partial pre-window failed");
        ESP_RETURN_ON_ERROR(epd_set_cursor((uint16_t)(x0 >> 3), mem_y_start),
                            TAG, "display partial pre-cursor failed");
    }
    ESP_RETURN_ON_ERROR(epd_prepare_partial(), TAG,
                        "display partial mode preparation failed");
    ESP_LOGI(TAG, "Display partial refresh x=%d y=%d w=%d h=%d bytes=%d ram=0x24->0x26->0x24",
             x0, y0, x1 - x0 + 1, y1 - y0 + 1, byte_len);
    ESP_RETURN_ON_ERROR(epd_set_windows((uint16_t)x0, mem_y_start,
                                        (uint16_t)x1, mem_y_end),
                        TAG, "display partial window failed");
    epd_partial_plane_context_t context = {
        .x = (uint16_t)(x0 >> 3),
        .y = mem_y_start,
    };
    const pj_display_partial_plane_io_t io = {
        .context = &context,
        .position = epd_partial_position,
        .command = epd_partial_command,
        .write = epd_partial_write,
        .activate = epd_partial_activate,
    };
    esp_err_t result = (esp_err_t)pj_display_refresh_commit_partial_planes(
        &io, g_epd_buffer, (size_t)byte_len);
    if (result != ESP_OK) {
        return result;
    }
    return ESP_OK;
}

static esp_err_t epd_refresh_full(const pj_framebuffer_t *framebuffer)
{
    g_epd_partial_ready = 0;
    ESP_RETURN_ON_ERROR(epd_set_lut(WF_FULL_1IN54), TAG,
                        "display full LUT preparation failed");
    ESP_RETURN_ON_ERROR(epd_send_command(0x11), TAG,
                        "display full data-entry command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(0x01), TAG,
                        "display full data-entry mode failed");
    ESP_RETURN_ON_ERROR(epd_send_command(0x3C), TAG,
                        "display full border command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(0x01), TAG,
                        "display full border data failed");
    framebuffer_to_epd(framebuffer);
    ESP_RETURN_ON_ERROR(epd_set_windows(0, PJ_DISPLAY_WIDTH - 1,
                                        PJ_DISPLAY_HEIGHT - 1, 0),
                        TAG, "display full window failed");
    ESP_RETURN_ON_ERROR(epd_set_cursor(0, PJ_DISPLAY_HEIGHT - 1), TAG,
                        "display full cursor 0x26 failed");
    ESP_RETURN_ON_ERROR(epd_send_command(0x26), TAG,
                        "display full RAM 0x26 command failed");
    ESP_RETURN_ON_ERROR(epd_write_bytes(g_epd_buffer, PJ_FRAMEBUFFER_BYTES), TAG,
                        "display full RAM 0x26 transfer failed");
    ESP_RETURN_ON_ERROR(epd_set_cursor(0, PJ_DISPLAY_HEIGHT - 1), TAG,
                        "display full cursor 0x24 failed");
    ESP_RETURN_ON_ERROR(epd_send_command(0x24), TAG,
                        "display full RAM 0x24 command failed");
    ESP_RETURN_ON_ERROR(epd_write_bytes(g_epd_buffer, PJ_FRAMEBUFFER_BYTES), TAG,
                        "display full RAM 0x24 transfer failed");
    ESP_RETURN_ON_ERROR(epd_turn_on_display(), TAG, "display full activation failed");
    return ESP_OK;
}

static int storage_refresh_capacity(void)
{
    pj_board_status_t status = board_status_snapshot_base();
    if (!status.storage_mounted) {
        board_status_take();
        g_status.storage_total_bytes = 0;
        g_status.storage_free_bytes = 0;
        g_status.storage_health = PJ_STORAGE_HEALTH_UNMOUNTED;
        board_status_give();
        return 0;
    }
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    esp_err_t err = esp_vfs_fat_info(status.storage_path, &total_bytes,
                                     &free_bytes);
    if (err != ESP_OK) {
        board_status_take();
        g_status.storage_health = PJ_STORAGE_HEALTH_IO_ERROR;
        g_status.storage = PJ_BOARD_SERVICE_ERROR;
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "microSD capacity check failed: %s; use storage recovery",
                       esp_err_to_name(err));
        board_status_give();
        return 0;
    }
    board_status_take();
    g_status.storage_total_bytes = total_bytes;
    g_status.storage_free_bytes = free_bytes;
    g_status.storage_health = pj_storage_capacity_health(1, 1, total_bytes, free_bytes,
                                                         PJ_STORAGE_RESERVE_BYTES);
    g_status.storage = PJ_BOARD_SERVICE_READY;
    board_status_give();
    return 1;
}

static int storage_preflight(uint64_t write_bytes, const char *operation)
{
    if (!storage_refresh_capacity()) {
        return 0;
    }
    pj_board_status_t status = board_status_snapshot_base();
    if (!pj_storage_can_write(status.storage_free_bytes, write_bytes,
                              PJ_STORAGE_RESERVE_BYTES)) {
        board_status_take();
        g_status.storage_health = PJ_STORAGE_HEALTH_FULL;
        (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                       "microSD has insufficient free space for %s; delete or sync notes",
                       operation);
        board_status_give();
        ESP_LOGW(TAG, "microSD has insufficient free space for %s "
                      "(free=%llu reserve=%llu requested=%llu)", operation,
                 (unsigned long long)status.storage_free_bytes,
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
    pj_board_status_t status = board_status_snapshot_base();
    board_status_take();
    g_status.storage_mounted = 0;
    g_status.storage_total_bytes = 0;
    g_status.storage_free_bytes = 0;
    g_status.storage_health = PJ_STORAGE_HEALTH_UNMOUNTED;
    board_status_give();
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

    esp_err_t err = esp_vfs_fat_sdmmc_mount(status.storage_path, &host,
                                            &slot_config, &mount_config,
                                            &g_sd_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "microSD mount at %dkHz failed: %s; retrying at identification frequency",
                 host.max_freq_khz, esp_err_to_name(err));
        host.max_freq_khz = SDMMC_FREQ_PROBING;
        g_sd_card = NULL;
        err = esp_vfs_fat_sdmmc_mount(status.storage_path, &host, &slot_config,
                                      &mount_config, &g_sd_card);
    }
    if (err == ESP_OK && g_sd_card == NULL) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        board_status_take();
        g_status.storage_mounted = 1;
        board_status_give();
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
                board_status_take();
                g_status.storage_recovery_count += (unsigned)recovered;
                board_status_give();
                ESP_LOGW(TAG, "Recovered %d interrupted storage artifact(s)", recovered);
                (void)storage_refresh_capacity();
            }
        }
    }
    if (err != ESP_OK) {
        if (g_sd_card != NULL) {
            esp_err_t unmount_err = esp_vfs_fat_sdcard_unmount(
                status.storage_path, g_sd_card);
            if (unmount_err != ESP_OK) {
                ESP_LOGW(TAG, "microSD cleanup unmount failed after initialization error: %s",
                         esp_err_to_name(unmount_err));
            }
            g_sd_card = NULL;
        }
        board_status_take();
        g_status.storage_mounted = 0;
        g_status.storage_total_bytes = 0;
        g_status.storage_free_bytes = 0;
        g_status.storage_health = PJ_STORAGE_HEALTH_IO_ERROR;
        board_status_give();
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

static int transcript_path_for_audio(char *out, size_t out_size,
                                     const char *filename)
{
    if (out == NULL || out_size == 0U || filename == NULL) {
        return 0;
    }
    const char *end = memchr(filename, '\0', PJ_NOTE_FILENAME_LEN);
    if (end == NULL) {
        out[0] = '\0';
        return 0;
    }
    size_t filename_len = (size_t)(end - filename);
    int written = snprintf(out, out_size, PJ_TRANSCRIPT_DIR "/%.*s.json",
                           (int)filename_len, filename);
    if (written < 0 || (size_t)written >= out_size) {
        out[0] = '\0';
        return 0;
    }
    return 1;
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
    if (!storage_shared_try_acquire()) {
        return ESP_ERR_INVALID_STATE;
    }
    pj_board_status_t status = board_status_snapshot_base();
    if (status.storage != PJ_BOARD_SERVICE_READY) {
        storage_shared_release();
        return ESP_ERR_INVALID_STATE;
    }
    if (!storage_preflight(body_size, "JSON update")) {
        status = board_status_snapshot_base();
        esp_err_t result = status.storage_health == PJ_STORAGE_HEALTH_FULL ?
                           ESP_ERR_NO_MEM : ESP_FAIL;
        storage_shared_release();
        return result;
    }
    if (g_json_write_lock == NULL ||
        xSemaphoreTake(g_json_write_lock, portMAX_DELAY) != pdTRUE) {
        storage_shared_release();
        return ESP_ERR_NO_MEM;
    }
    remove(temporary_path);
    FILE *file = fopen(temporary_path, "wb");
    if (file == NULL) {
        xSemaphoreGive(g_json_write_lock);
        storage_shared_release();
        return ESP_FAIL;
    }
    int written = fwrite(body, 1, body_size, file) == body_size;
    int flushed = fflush(file) == 0 && fsync(fileno(file)) == 0;
    int closed = fclose(file) == 0;
    if (!written || !flushed || !closed) {
        remove(temporary_path);
        xSemaphoreGive(g_json_write_lock);
        storage_shared_release();
        return ESP_FAIL;
    }
    struct stat existing;
    int had_existing = stat(path, &existing) == 0;
    remove(backup_path);
    if (had_existing && rename(path, backup_path) != 0) {
        remove(temporary_path);
        xSemaphoreGive(g_json_write_lock);
        storage_shared_release();
        return ESP_FAIL;
    }
    if (rename(temporary_path, path) != 0) {
        if (had_existing) {
            (void)rename(backup_path, path);
        }
        remove(temporary_path);
        xSemaphoreGive(g_json_write_lock);
        storage_shared_release();
        return ESP_FAIL;
    }
    if (had_existing) {
        remove(backup_path);
    }
    (void)storage_refresh_capacity();
    xSemaphoreGive(g_json_write_lock);
    storage_shared_release();
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
    if (!transcript_path_for_audio(path, path_size, filename)) {
        return 0;
    }
    return pj_transcript_marker_load(path, label, label_size);
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

static int recording_file_valid_for_replace(const char *path, void *context);

static pj_recording_raw_publish_result_t recording_publish_file(
    const char *temporary_path, const char *final_path,
    uint32_t data_bytes, int *sync_resume_needed)
{
    if (sync_resume_needed == NULL) {
        return PJ_RECORDING_RAW_PUBLISH_FAILED;
    }
    *sync_resume_needed = 0;
    if (!recording_file_valid(temporary_path, data_bytes)) {
        ESP_LOGE(TAG,
                 "Recording validation unavailable before publish; "
                 "preserving temporary for recovery: %s",
                 temporary_path);
        return PJ_RECORDING_RAW_PUBLISH_RETRYABLE;
    }

    pj_board_sync_inventory_mutation_t sync_mutation;
    if (!pj_board_companion_sync_inventory_mutation_begin(
            &sync_mutation, 0)) {
        ESP_LOGE(TAG,
                 "Recording publication blocked by unavailable Sync state; "
                 "preserving finalized temporary: %s",
                 temporary_path);
        return PJ_RECORDING_RAW_PUBLISH_RETRYABLE;
    }

    uint32_t expected_data_bytes = data_bytes;
    pj_recording_raw_publish_result_t result = pj_recording_publish_raw(
        temporary_path, final_path, recording_file_valid_for_replace,
        &expected_data_bytes);
    if (result == PJ_RECORDING_RAW_PUBLISH_RETRYABLE) {
        ESP_LOGE(TAG,
                 "Recording publication incomplete; recoverable audio remains "
                 "at %s or %s errno=%d",
                 temporary_path, final_path, errno);
    }

    *sync_resume_needed = sync_mutation.start_pending;
    if (!pj_board_companion_sync_inventory_mutation_finish(&sync_mutation)) {
        ESP_LOGE(TAG, "Recording publication could not release the Sync barrier");
        return PJ_RECORDING_RAW_PUBLISH_FAILED;
    }
    if (result != PJ_RECORDING_RAW_PUBLISH_SUCCEEDED) {
        return result;
    }

    write_recording_metadata(final_path, data_bytes);
    return PJ_RECORDING_RAW_PUBLISH_SUCCEEDED;
}

static int probe_audio_entry_unlocked(const char *filename, pj_audio_entry_t *entry)
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

static int probe_audio_entry(const char *filename, pj_audio_entry_t *entry)
{
    if (!storage_shared_try_acquire()) {
        return 0;
    }
    int valid = probe_audio_entry_unlocked(filename, entry);
    storage_shared_release();
    return valid;
}

static int audio_entry_compare_newest_first(const void *left, const void *right)
{
    const pj_audio_entry_t *a = (const pj_audio_entry_t *)left;
    const pj_audio_entry_t *b = (const pj_audio_entry_t *)right;
    return strcmp(b->filename, a->filename);
}

static int collect_audio_entries(pj_audio_entry_t **entries_out)
{
    if (entries_out == NULL) {
        return 0;
    }
    *entries_out = NULL;
    if (!storage_shared_try_acquire()) {
        return PJ_AUDIO_COLLECT_STORAGE_BUSY;
    }
    DIR *dir = opendir(PJ_AUDIO_DIR);
    if (dir == NULL) {
        storage_shared_release();
        return PJ_AUDIO_COLLECT_OPEN_FAILED;
    }
    pj_audio_entry_t *entries = NULL;
    size_t capacity = 0;
    size_t count = 0;
    int result = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' || !is_audio_filename(entry->d_name)) {
            continue;
        }
        if (count == capacity) {
            if (capacity == PJ_AUDIO_MAX_INDEXED_FILES) {
                result = PJ_AUDIO_COLLECT_TOO_MANY;
                break;
            }
            size_t next_capacity = capacity == 0U ?
                PJ_AUDIO_INITIAL_INDEX_CAPACITY : capacity * 2U;
            if (next_capacity > PJ_AUDIO_MAX_INDEXED_FILES) {
                next_capacity = PJ_AUDIO_MAX_INDEXED_FILES;
            }
            pj_audio_entry_t *resized = heap_caps_realloc(
                entries, next_capacity * sizeof(entries[0]),
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (resized == NULL) {
                result = PJ_AUDIO_COLLECT_NO_MEMORY;
                break;
            }
            entries = resized;
            capacity = next_capacity;
        }
        if (probe_audio_entry_unlocked(entry->d_name, &entries[count])) {
            count++;
        }
    }
    closedir(dir);
    if (result == 0) {
        qsort(entries, count, sizeof(entries[0]), audio_entry_compare_newest_first);
        *entries_out = entries;
        result = (int)count;
    } else {
        free(entries);
    }
    storage_shared_release();
    return result;
}

static pj_board_sync_inventory_state_t collect_sync_counts_fresh(
    int *pending, int *transferred)
{
    if (pending == NULL || transferred == NULL) {
        return PJ_BOARD_SYNC_INVENTORY_ERROR;
    }
    *pending = 0;
    *transferred = 0;
    pj_audio_entry_t *entries = NULL;
    int count = collect_audio_entries(&entries);
    if (count < 0) {
        free(entries);
        return count == PJ_AUDIO_COLLECT_STORAGE_BUSY ?
            PJ_BOARD_SYNC_INVENTORY_BUSY : PJ_BOARD_SYNC_INVENTORY_ERROR;
    }
    for (int i = 0; i < count; i++) {
        if (entries[i].note.synced) {
            (*transferred)++;
        } else {
            (*pending)++;
        }
    }
    free(entries);
    storage_sync_counts_cache(*pending, *transferred);
    return PJ_BOARD_SYNC_INVENTORY_READY;
}

static void sync_inventory_worker_task(void *context)
{
    (void)context;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(g_sync_inventory_lock, portMAX_DELAY);
        if (g_sync_inventory_worker_state != PJ_SYNC_INVENTORY_REQUESTED) {
            xSemaphoreGive(g_sync_inventory_lock);
            continue;
        }
        g_sync_inventory_worker_state = PJ_SYNC_INVENTORY_RUNNING;
        xSemaphoreGive(g_sync_inventory_lock);

        pj_board_sync_inventory_t result = {0};
        result.state = collect_sync_counts_fresh(
            &result.pending, &result.transferred);

        xSemaphoreTake(g_sync_inventory_lock, portMAX_DELAY);
        if (g_sync_inventory_discard) {
            g_sync_inventory_discard = 0;
            g_sync_inventory_worker_state = PJ_SYNC_INVENTORY_IDLE;
        } else {
            g_sync_inventory_result = result;
            g_sync_inventory_worker_state = PJ_SYNC_INVENTORY_RESULT;
        }
        xSemaphoreGive(g_sync_inventory_lock);
    }
}

static int sync_inventory_worker_ensure(void)
{
    if (g_sync_inventory_lock == NULL) {
        g_sync_inventory_lock = xSemaphoreCreateMutexStatic(
            &g_sync_inventory_lock_storage);
        if (g_sync_inventory_lock == NULL) {
            return 0;
        }
    }
    if (g_sync_inventory_task != NULL) {
        return 1;
    }
    return xTaskCreate(sync_inventory_worker_task, "pj-sync-inventory",
                       PJ_SYNC_INVENTORY_TASK_STACK, NULL, 1,
                       &g_sync_inventory_task) == pdPASS;
}

static void collect_sync_counts(int *pending, int *transferred)
{
    storage_sync_counts_snapshot(pending, transferred);
    int fresh_pending = 0;
    int fresh_transferred = 0;
    if (collect_sync_counts_fresh(&fresh_pending, &fresh_transferred) ==
        PJ_BOARD_SYNC_INVENTORY_READY) {
        *pending = fresh_pending;
        *transferred = fresh_transferred;
    }
}

static void sync_inventory_worker_request_nonblocking(void)
{
    if (g_sync_inventory_lock == NULL || g_sync_inventory_task == NULL) {
        return;
    }
    int notify_worker = 0;
    if (xSemaphoreTake(g_sync_inventory_lock, 0) != pdTRUE) {
        return;
    }
    if (g_sync_inventory_worker_state == PJ_SYNC_INVENTORY_IDLE) {
        /* Fresh counts are cached by the worker; only the Sync UI owns results. */
        g_sync_inventory_discard = 1;
        g_sync_inventory_worker_state = PJ_SYNC_INVENTORY_REQUESTED;
        notify_worker = 1;
    }
    xSemaphoreGive(g_sync_inventory_lock);
    if (notify_worker) {
        xTaskNotifyGive(g_sync_inventory_task);
    }
}

static void collect_sync_counts_nonblocking(int *pending, int *transferred,
                                            int storage_ready)
{
    storage_sync_counts_snapshot(pending, transferred);
    if (storage_ready) {
        sync_inventory_worker_request_nonblocking();
    }
}

static void refresh_ui_sync_state(pj_ui_context_t *ui)
{
    if (ui == NULL || pj_ui_current_state(ui) == PJ_UI_STATE_SYNC) {
        return;
    }
    int pending = 0;
    int transferred = 0;
    collect_sync_counts(&pending, &transferred);
    pj_board_status_t status = board_status_snapshot_base();
    pj_ui_set_sync_state(ui, pending, transferred,
                         status.wifi == PJ_BOARD_SERVICE_READY);
}

static size_t delete_dir_entries(const char *dir_path, int (*matches)(const char *name),
                                 int *incomplete)
{
    size_t deleted = 0U;
    for (size_t batch = 0; batch < PJ_STORAGE_WIPE_MAX_BATCHES; batch++) {
        pj_storage_delete_result_t result =
            pj_storage_delete_matching(dir_path, matches, PJ_STORAGE_WIPE_BATCH_ENTRIES);
        if (result.open_errno == ENOENT) {
            return deleted;
        }
        deleted += result.deleted;
        int failed = result.open_errno != 0 || result.scan_errno != 0 ||
                     result.close_errno != 0 || result.allocation_errno != 0 ||
                     result.remove_failures != 0U;
        if (failed || result.truncated != 0U) {
            ESP_LOGW(TAG,
                     "Delete batch incomplete: dir=%s matched=%zu snapshotted=%zu deleted=%zu "
                     "failures=%zu truncated=%zu open=%d scan=%d close=%d alloc=%d remove=%d",
                     dir_path, result.matched, result.snapshotted, result.deleted,
                     result.remove_failures, result.truncated, result.open_errno,
                     result.scan_errno, result.close_errno, result.allocation_errno,
                     result.first_remove_errno);
        }
        if (failed) {
            *incomplete = 1;
            return deleted;
        }
        if (result.truncated == 0U) {
            return deleted;
        }
        vTaskDelay(1);
    }
    ESP_LOGW(TAG, "Delete stopped at batch limit: dir=%s batches=%u", dir_path,
             (unsigned)PJ_STORAGE_WIPE_MAX_BATCHES);
    *incomplete = 1;
    return deleted;
}

static void recording_wipe_worker(void *arg)
{
    (void)arg;
    /* xTaskCreate can schedule this task on the other core before returning. */
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    portENTER_CRITICAL(&g_storage_coordinator_lock);
    pj_storage_wipe_mark_running(&g_storage_coordinator);
    pj_wipe_status_t wipe_status = pj_storage_wipe_status(&g_storage_coordinator);
    portEXIT_CRITICAL(&g_storage_coordinator_lock);

    ESP_LOGI(TAG, "Recording wipe worker started: operation=%" PRIu32
                  " stack_hwm=%u",
             wipe_status.id, (unsigned)uxTaskGetStackHighWaterMark(NULL));

    pj_board_sync_inventory_mutation_t sync_mutation;
    if (!pj_board_companion_sync_inventory_mutation_begin(
            &sync_mutation, 0)) {
        board_status_set_error(
            "recording wipe blocked by unavailable Sync state");
        portENTER_CRITICAL(&g_storage_coordinator_lock);
        pj_storage_wipe_finish(
            &g_storage_coordinator, 0U, 0U, 0U,
            PJ_WIPE_CODE_SYNC_STATE_FAILED, 1);
        g_storage_wipe_task = NULL;
        portEXIT_CRITICAL(&g_storage_coordinator_lock);
        ESP_LOGE(TAG,
                 "Recording wipe blocked before deletion: operation=%" PRIu32,
                 wipe_status.id);
        vTaskDelete(NULL);
        return;
    }

    int incomplete = 0;
    size_t audio_deleted = delete_dir_entries(
        PJ_AUDIO_DIR, pj_storage_audio_wipe_artifact, &incomplete);
    size_t transcripts_deleted =
        delete_dir_entries(
            PJ_TRANSCRIPT_DIR, pj_storage_json_wipe_artifact, &incomplete);
    size_t notes_deleted = delete_dir_entries(
        PJ_NOTE_DIR, pj_storage_json_wipe_artifact, &incomplete);
    int sync_resume_needed = sync_mutation.start_pending;
    if (!pj_board_companion_sync_inventory_mutation_finish(
            &sync_mutation)) {
        ESP_LOGE(TAG,
                 "Recording wipe could not release the Sync mutation barrier");
    }

    if (incomplete) {
        board_status_set_error("recording wipe incomplete");
    } else {
        storage_sync_counts_cache(0, 0);
        board_status_clear_error_if_equal("recording wipe incomplete");
        board_status_clear_error_if_equal(
            "recording wipe blocked by unavailable Sync state");
    }
    ESP_LOGI(TAG, "Wiped recordings: operation=%" PRIu32
                  " audio=%zu transcripts=%zu metadata=%zu complete=%d stack_hwm=%u",
             wipe_status.id, audio_deleted, transcripts_deleted, notes_deleted,
             !incomplete, (unsigned)uxTaskGetStackHighWaterMark(NULL));

    portENTER_CRITICAL(&g_storage_coordinator_lock);
    pj_storage_wipe_finish(&g_storage_coordinator, audio_deleted, transcripts_deleted,
                           notes_deleted,
                           incomplete ? PJ_WIPE_CODE_INCOMPLETE : PJ_WIPE_CODE_NONE,
                           incomplete);
    g_storage_wipe_task = NULL;
    portEXIT_CRITICAL(&g_storage_coordinator_lock);

    if (sync_resume_needed && !pj_board_companion_sync_resume()) {
        ESP_LOGW(TAG, "Unable to resume queued Sync after recording wipe");
    }
    g_ui_note_audio_count = 0;
    g_ui_note_transcript_view = 0;
    board_update_publish(BOARD_UPDATE_NOTES);
    vTaskDelete(NULL);
}

static int recording_wipe_release(uint32_t operation_id)
{
    TaskHandle_t task = NULL;
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    pj_wipe_status_t status = pj_storage_wipe_status(&g_storage_coordinator);
    if (status.id == operation_id && status.state == PJ_WIPE_STATE_QUEUED) {
        task = g_storage_wipe_task;
    }
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
    if (task == NULL) {
        return 0;
    }
    xTaskNotifyGive(task);
    return 1;
}

static pj_wipe_start_result_t recording_wipe_start(const char *request_id,
                                                    pj_wipe_status_t *status,
                                                    recording_wipe_release_mode_t release_mode)
{
    pj_wipe_status_t local_status;
    if (status == NULL) {
        status = &local_status;
    }
    int audio_active = audio_lifecycle_active();
    pj_board_status_t board_status = board_status_snapshot_base();
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    pj_wipe_start_result_t result = pj_storage_wipe_request(
        &g_storage_coordinator,
        board_status.storage == PJ_BOARD_SERVICE_READY,
        audio_active, request_id, status);
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
    if (result != PJ_WIPE_START_STARTED) {
        return result;
    }

    TaskHandle_t task = NULL;
    BaseType_t created = xTaskCreate(recording_wipe_worker, "pj-wipe",
                                     PJ_STORAGE_WIPE_TASK_STACK, NULL, 2, &task);
    if (created != pdPASS) {
        portENTER_CRITICAL(&g_storage_coordinator_lock);
        pj_storage_wipe_finish(&g_storage_coordinator, 0U, 0U, 0U,
                               PJ_WIPE_CODE_TASK_START_FAILED, 1);
        *status = pj_storage_wipe_status(&g_storage_coordinator);
        portEXIT_CRITICAL(&g_storage_coordinator_lock);
        return PJ_WIPE_START_TASK_FAILED;
    }
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    g_storage_wipe_task = task;
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
    if (release_mode == PJ_WIPE_WORKER_RELEASE_NOW &&
        !recording_wipe_release(status->id)) {
        ESP_LOGE(TAG, "Recording wipe worker release failed: operation=%" PRIu32,
                 status->id);
    }
    return PJ_WIPE_START_STARTED;
}

static int storage_recovery_wav_validate(const char *path, void *context)
{
    (void)context;
    struct stat st;
    errno = 0;
    if (stat(path, &st) != 0) {
        return errno == ENOENT ? 0 : -1;
    }
    if (st.st_size < (off_t)PJ_STORAGE_WAV_HEADER_BYTES) {
        return 0;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return errno == ENOENT ? 0 : -1;
    }
    uint8_t header[PJ_STORAGE_WAV_HEADER_BYTES];
    size_t read = fread(header, 1, sizeof(header), file);
    int read_failed = ferror(file);
    int close_failed = fclose(file) != 0;
    if (read_failed || close_failed) {
        return -1;
    }
    if (read != sizeof(header)) {
        return 0;
    }

    pj_storage_wav_info_t wav;
    int valid = pj_storage_wav_validate(header, sizeof(header),
                                        (uint64_t)st.st_size,
                                        PJ_AUDIO_SAMPLE_RATE,
                                        PJ_AUDIO_BITS_PER_SAMPLE, &wav) &&
                wav.channels == PJ_AUDIO_CHANNELS;
    return valid ? 1 : 0;
}

static int recover_dir_artifacts(const char *dir_path)
{
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        return 0;
    }
    int recovered = 0;
    for (int recovery_pass = 0; recovery_pass < 2; recovery_pass++) {
        rewinddir(dir);
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            size_t name_len = strlen(entry->d_name);
            if (entry->d_name[0] == '.' || name_len < 5U) {
                continue;
            }
            char artifact_path[384];
            char target_path[384];
            int artifact_len = snprintf(
                artifact_path, sizeof(artifact_path), "%s/%s",
                dir_path, entry->d_name);
            if (artifact_len < 0 ||
                artifact_len >= (int)sizeof(artifact_path)) {
                continue;
            }
            size_t suffix_len = 4U;
            if (name_len - suffix_len + strlen(dir_path) + 2U >
                sizeof(target_path)) {
                continue;
            }
            (void)snprintf(
                target_path, sizeof(target_path), "%s/%.*s", dir_path,
                (int)(name_len - suffix_len), entry->d_name);
            struct stat target_stat;
            int target_exists = stat(target_path, &target_stat) == 0;
            pj_storage_recovery_action_t action =
                pj_storage_recovery_action(entry->d_name, target_exists);
            int backup_action =
                action == PJ_STORAGE_RECOVERY_DELETE_BACKUP ||
                action == PJ_STORAGE_RECOVERY_RESTORE_BACKUP ||
                action == PJ_STORAGE_RECOVERY_VALIDATE_BACKUP;
            if ((recovery_pass == 0) != backup_action) {
                continue;
            }
            if (action == PJ_STORAGE_RECOVERY_VALIDATE_BACKUP ||
                action == PJ_STORAGE_RECOVERY_VALIDATE_TEMP) {
                pj_storage_backup_recovery_result_t result =
                    action == PJ_STORAGE_RECOVERY_VALIDATE_BACKUP ?
                    pj_storage_recover_backup(
                        artifact_path, target_path,
                        storage_recovery_wav_validate, NULL) :
                    pj_storage_recover_temporary(
                        artifact_path, target_path,
                        storage_recovery_wav_validate, NULL);
                if (result == PJ_STORAGE_BACKUP_RECOVERY_RESTORED ||
                    result == PJ_STORAGE_BACKUP_RECOVERY_REMOVED) {
                    recovered++;
                } else if (result == PJ_STORAGE_BACKUP_RECOVERY_FAILED) {
                    ESP_LOGW(
                        TAG, "Leaving uncertain WAV recovery artifacts: %s",
                        artifact_path);
                }
                continue;
            }
            if ((action == PJ_STORAGE_RECOVERY_DELETE_TEMP ||
                 action == PJ_STORAGE_RECOVERY_DELETE_BACKUP) &&
                remove(artifact_path) == 0) {
                recovered++;
            } else if (action == PJ_STORAGE_RECOVERY_RESTORE_BACKUP &&
                       rename(artifact_path, target_path) == 0) {
                recovered++;
            }
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
    pj_audio_entry_t *entries = NULL;
    int count = collect_audio_entries(&entries);
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

static int recording_path_available(const char *path)
{
    char temporary_path[160];
    char backup_path[160];
    int temporary_length = snprintf(
        temporary_path, sizeof(temporary_path), "%s.tmp", path);
    int backup_length = snprintf(
        backup_path, sizeof(backup_path), "%s.bak", path);
    if (temporary_length < 0 ||
        temporary_length >= (int)sizeof(temporary_path) ||
        backup_length < 0 || backup_length >= (int)sizeof(backup_path)) {
        return 0;
    }
    struct stat st;
    errno = 0;
    if (stat(path, &st) == 0 || errno != ENOENT) {
        return 0;
    }
    errno = 0;
    if (stat(temporary_path, &st) == 0 || errno != ENOENT) {
        return 0;
    }
    errno = 0;
    return stat(backup_path, &st) != 0 && errno == ENOENT;
}

static int next_recording_path(char *out, size_t out_size)
{
    if (out_size == 0) {
        return 0;
    }
    board_time_snapshot_t time = board_time_snapshot();
    for (int attempt = 0; attempt < 1000; attempt++) {
        uint32_t sequence = g_record_sequence++;
        (void)snprintf(out, out_size,
                       PJ_AUDIO_DIR "/rec-%04d%02d%02d-%02d%02d-%03u.wav",
                       time.year, time.month, time.day,
                       time.hour, time.minute,
                       (unsigned)(sequence % 1000u));
        if (recording_path_available(out)) {
            return 1;
        }
    }
    out[0] = '\0';
    return 0;
}

static void refresh_ui_notes_from_sd(pj_ui_context_t *ui)
{
    char labels[PJ_UI_MAX_NOTES][PJ_UI_NOTE_LABEL_LEN];
    pj_audio_entry_t *entries = NULL;
    memset(labels, 0, sizeof(labels));
    int count = collect_audio_entries(&entries);
    if (count == PJ_AUDIO_COLLECT_NO_MEMORY) {
        g_ui_note_audio_count = 0;
        pj_ui_set_notes(ui, 0, labels);
        return;
    }
    if (count < 0) {
        free(entries);
        return;
    }
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
    int temperature_valid = 0;
    int temperature_c = 0;
    int humidity_percent = -1;
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
                temperature_c =
                    (int)(temp + (temp >= 0 ? 0.5f : -0.5f));
                temperature_valid = 1;
            }
            if (shtc3_crc(bytes + 3, 2) == bytes[5]) {
                uint16_t raw_humidity = ((uint16_t)bytes[3] << 8) | bytes[4];
                int humidity = (int)(100.0f * (float)raw_humidity / 65536.0f + 0.5f);
                humidity_percent = humidity < 0 ? 0 :
                                   humidity > 100 ? 100 : humidity;
                humidity_valid = 1;
            }
        }
    }
    board_status_take();
    if (temperature_valid) {
        g_status.temperature_c = temperature_c;
    }
    g_status.humidity_percent = humidity_valid ? humidity_percent : -1;
    board_status_give();
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
    esp_err_t aux_err = aux_task_start();
    if (aux_err != ESP_OK) {
        ESP_LOGW(TAG, "AUX poll task failed to start: %s", esp_err_to_name(aux_err));
    }
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

static int touch_poll_filtered_event(pj_board_event_t *event, TickType_t now,
                                     uint64_t now_ms)
{
    uint16_t x = 0;
    uint16_t y = 0;
    int touch_active = touch_read(&x, &y);
    if (touch_active) {
        uint64_t candidate_started_ms = 0;
        int candidate_stable = pj_touch_candidate_update(
            &g_touch_candidate, x, y, now_ms, PJ_TOUCH_MOVE_TOLERANCE,
            PJ_TOUCH_STABLE_SAMPLES, &candidate_started_ms);
        if (!g_touch_pressed && candidate_stable) {
            uint32_t since_last_ms = (uint32_t)((now - g_touch_last_event_tick) * portTICK_PERIOD_MS);
            if (since_last_ms >= PJ_TOUCH_EVENT_GUARD_MS) {
                g_touch_pressed = 1;
                g_touch_press_x = x;
                g_touch_press_y = y;
                g_touch_last_event_tick = now;
                event->type = PJ_BOARD_EVENT_TOUCH_TAP;
                event->x = (int)g_touch_press_x;
                event->y = (int)g_touch_press_y;
                event->captured_at_ms = candidate_started_ms;
                g_display_warning_logged = 1;
                ESP_LOGI(TAG, "Touch tap x=%u y=%u stable=%d", (unsigned)event->x, (unsigned)event->y,
                         g_touch_candidate.samples);
                return 1;
            }
        } else if (g_touch_pressed) {
            g_touch_press_x = x;
            g_touch_press_y = y;
        }
    } else if (g_touch_pressed) {
        g_touch_pressed = 0;
        pj_touch_candidate_reset(&g_touch_candidate);
    } else {
        pj_touch_candidate_reset(&g_touch_candidate);
    }
    return 0;
}

static esp_err_t board_event_queue_ensure(void)
{
    if (g_board_event_queue != NULL) {
        return ESP_OK;
    }
    g_board_event_queue = xQueueCreate(PJ_TOUCH_EVENT_QUEUE_DEPTH, sizeof(pj_board_event_t));
    return g_board_event_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

static void board_queue_event(const pj_board_event_t *event)
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

static void aux_released_set(int released)
{
    portENTER_CRITICAL(&g_aux_state_lock);
    g_aux_released = released != 0;
    portEXIT_CRITICAL(&g_aux_state_lock);
}

static void aux_queue_event(const pj_board_event_t *event)
{
    if (g_aux_event_queue == NULL || event == NULL) {
        return;
    }
    if (event->type == PJ_BOARD_EVENT_AUX_LONG) {
        if (xQueueSendToFront(g_aux_event_queue, event, 0) == pdTRUE) {
            return;
        }
        pj_board_event_t dropped;
        if (xQueueReceive(g_aux_event_queue, &dropped, 0) != pdTRUE ||
            xQueueSendToFront(g_aux_event_queue, event, 0) != pdTRUE) {
            ESP_LOGE(TAG, "AUX long gesture could not replace a queued event");
        } else {
            ESP_LOGW(TAG, "AUX event queue full; long gesture replaced gesture=%d",
                     dropped.type);
        }
        return;
    }

    if (xQueueSend(g_aux_event_queue, event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "AUX event queue full; dropped gesture=%d", event->type);
    }
}

static void aux_poll_task(void *arg)
{
    (void)arg;
    while (1) {
        int level = gpio_get_level(BOOT_BUTTON_PIN);
        uint64_t now_ms = board_monotonic_ms();
        pj_aux_gesture_t gesture = pj_aux_input_update(&g_aux_input, level, now_ms);
        aux_released_set(pj_aux_input_is_released(&g_aux_input));

        int power_toggle;
        portENTER_CRITICAL(&g_aux_state_lock);
        power_toggle = pj_power_input_update(
            &g_power_input, gpio_get_level(PWR_BUTTON_PIN), now_ms);
        g_power_released = pj_power_input_is_released(&g_power_input);
        portEXIT_CRITICAL(&g_aux_state_lock);
        if (power_toggle) {
            pj_board_event_t power_event = {
                .type = PJ_BOARD_EVENT_POWER,
                .x = 0,
                .y = 0,
                .captured_at_ms = 0,
            };
            ESP_LOGI(TAG, "PWR press");
            board_queue_event(&power_event);
        }

        pj_board_event_t event = {
            .type = gesture == PJ_AUX_GESTURE_SHORT ? PJ_BOARD_EVENT_AUX_SHORT :
                    gesture == PJ_AUX_GESTURE_LONG ? PJ_BOARD_EVENT_AUX_LONG :
                    gesture == PJ_AUX_GESTURE_DOUBLE ? PJ_BOARD_EVENT_AUX_DOUBLE :
                    PJ_BOARD_EVENT_NONE,
            .x = 0,
            .y = 0,
            .captured_at_ms = gesture == PJ_AUX_GESTURE_SHORT ||
                    gesture == PJ_AUX_GESTURE_DOUBLE ?
                pj_aux_input_gesture_started_ms(&g_aux_input) : 0,
        };
        if (event.type != PJ_BOARD_EVENT_NONE) {
            ESP_LOGI(TAG, "AUX %s", event.type == PJ_BOARD_EVENT_AUX_SHORT ? "short" :
                     event.type == PJ_BOARD_EVENT_AUX_LONG ? "long" : "double");
            aux_queue_event(&event);
        }
        vTaskDelay(pdMS_TO_TICKS(PJ_AUX_POLL_MS));
    }
}

static esp_err_t aux_task_start(void)
{
    if (g_aux_task_started) {
        return ESP_OK;
    }
    if (g_aux_event_queue == NULL) {
        g_aux_event_queue = xQueueCreate(PJ_AUX_EVENT_QUEUE_DEPTH,
                                         sizeof(pj_board_event_t));
        if (g_aux_event_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_RETURN_ON_ERROR(board_event_queue_ensure(), TAG,
                        "board event queue allocation failed");
    int level = gpio_get_level(BOOT_BUTTON_PIN);
    uint64_t aux_now_ms = board_monotonic_ms();
    uint32_t button_now_ms =
        (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    pj_aux_input_init(&g_aux_input, level, aux_now_ms);
    aux_released_set(pj_aux_input_is_released(&g_aux_input));
    portENTER_CRITICAL(&g_aux_state_lock);
    pj_power_input_init(&g_power_input, gpio_get_level(PWR_BUTTON_PIN),
                        button_now_ms);
    g_power_released = pj_power_input_is_released(&g_power_input);
    portEXIT_CRITICAL(&g_aux_state_lock);
    if (xTaskCreate(aux_poll_task, "pj-aux", 3072, NULL, 7, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    g_aux_task_started = 1;
    ESP_LOGI(TAG, "AUX polling at %dms", PJ_AUX_POLL_MS);
    return ESP_OK;
}

static void touch_poll_task(void *arg)
{
    (void)arg;
    while (1) {
        pj_board_event_t event = {
            .type = PJ_BOARD_EVENT_NONE,
            .x = 0,
            .y = 0,
            .captured_at_ms = 0,
        };
        TickType_t now = xTaskGetTickCount();
        if (touch_poll_filtered_event(
                &event, now, board_monotonic_ms())) {
            board_queue_event(&event);
        }
        vTaskDelay(pdMS_TO_TICKS(PJ_TOUCH_POLL_MS));
    }
}

static esp_err_t touch_task_start(void)
{
    if (!g_touch_ready || g_touch_task_started) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(board_event_queue_ensure(), TAG,
                        "board event queue allocation failed");
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
    board_status_take();
    g_status.battery_percent = percent;
    board_status_give();
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

static pj_reset_reason_t runtime_reset_reason(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return PJ_RESET_REASON_POWER_ON;
    case ESP_RST_EXT: return PJ_RESET_REASON_EXTERNAL;
    case ESP_RST_SW: return PJ_RESET_REASON_SOFTWARE;
    case ESP_RST_PANIC: return PJ_RESET_REASON_PANIC;
    case ESP_RST_INT_WDT: return PJ_RESET_REASON_INTERRUPT_WATCHDOG;
    case ESP_RST_TASK_WDT: return PJ_RESET_REASON_TASK_WATCHDOG;
    case ESP_RST_WDT: return PJ_RESET_REASON_WATCHDOG;
    case ESP_RST_DEEPSLEEP: return PJ_RESET_REASON_DEEP_SLEEP;
    case ESP_RST_BROWNOUT: return PJ_RESET_REASON_BROWNOUT;
    case ESP_RST_SDIO: return PJ_RESET_REASON_SDIO;
    case ESP_RST_USB: return PJ_RESET_REASON_USB;
    case ESP_RST_JTAG: return PJ_RESET_REASON_JTAG;
    case ESP_RST_EFUSE: return PJ_RESET_REASON_EFUSE;
    case ESP_RST_PWR_GLITCH: return PJ_RESET_REASON_POWER_GLITCH;
    case ESP_RST_CPU_LOCKUP: return PJ_RESET_REASON_CPU_LOCKUP;
    case ESP_RST_UNKNOWN:
    default: return PJ_RESET_REASON_UNKNOWN;
    }
}

static board_time_snapshot_t board_time_snapshot(void)
{
    board_time_snapshot_t snapshot;
    pj_time_clock_anchor_t anchor;
    portENTER_CRITICAL(&g_time_lock);
    anchor = g_time_clock_anchor;
    snapshot = (board_time_snapshot_t) {
        .time_set = g_time_known,
        .generation = g_time_generation,
    };
    portEXIT_CRITICAL(&g_time_lock);
    uint64_t now_ms = board_monotonic_ms();
    pj_time_clock_t clock;
    if (snapshot.time_set && pj_time_clock_snapshot(&anchor, 1, now_ms, &clock) &&
        pj_time_clock_civil_from_day(clock.local_day, &snapshot.year,
                                     &snapshot.month, &snapshot.day)) {
        snapshot.hour = (int)(clock.local_second / 3600u);
        snapshot.minute = (int)(clock.local_second % 3600u / 60u);
        snapshot.second = (int)(clock.local_second % 60u);
    } else if (snapshot.time_set) {
        snapshot.time_set = 0;
    }
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
    g_time_clock_anchor = anchor;
    g_time_wall_trusted = trusted != 0;
    g_time_known = 1;
    g_time_generation++;
    portEXIT_CRITICAL(&g_time_lock);
    return 1;
}

static void board_time_publish_prevalidated(
    const pj_time_clock_anchor_t *anchor, int trusted)
{
    portENTER_CRITICAL(&g_time_lock);
    g_time_clock_anchor = *anchor;
    g_time_wall_trusted = trusted != 0;
    g_time_known = 1;
    g_time_generation++;
    portEXIT_CRITICAL(&g_time_lock);
}

static int board_time_initialize_unknown(void)
{
    pj_time_clock_anchor_t anchor;
    if (!pj_time_clock_anchor_set(&anchor, 2024, 1, 1, 0, 0, 0,
                                  board_monotonic_ms())) {
        return 0;
    }
    portENTER_CRITICAL(&g_time_lock);
    g_time_clock_anchor = anchor;
    g_time_wall_trusted = 0;
    g_time_known = 0;
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
    board_update_publish(BOARD_UPDATE_TIME);
}

static int board_time_take_pending(board_time_snapshot_t *snapshot)
{
    if (!board_update_take(BOARD_UPDATE_TIME)) {
        return 0;
    }
    board_time_snapshot_t current = board_time_snapshot();
    if (!current.time_set) {
        return 0;
    }
    *snapshot = current;
    return 1;
}

static int rtc_read_status_time_locked(void)
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

static int rtc_read_status_time(void)
{
    if (!time_transaction_take()) {
        return 0;
    }
    int published = rtc_read_status_time_locked();
    time_transaction_give();
    return published;
}

typedef struct {
    uint8_t control1;
    uint8_t civil[7];
} rtc_raw_time_t;

typedef struct {
    rtc_raw_time_t previous_rtc;
    pj_time_clock_anchor_t target_anchor;
    int hour;
    int minute;
    int year;
    int month;
    int day;
    int second;
    int anchor_at_rtc_write;
    int update_utc_offset;
    int utc_offset_minutes;
    int publish_time_sync_known;
    unsigned offset_store_calls;
    esp_err_t rtc_snapshot_error;
    esp_err_t offset_store_error;
    esp_err_t rtc_write_error;
    esp_err_t rtc_restore_error;
    esp_err_t offset_restore_error;
} board_time_transaction_context_t;

static esp_err_t rtc_snapshot_raw_locked(rtc_raw_time_t *snapshot)
{
    if (snapshot == NULL || !g_rtc_ready || g_rtc_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!i2c_take(pdMS_TO_TICKS(50))) {
        return ESP_ERR_TIMEOUT;
    }
    uint8_t control_register = 0x00;
    esp_err_t err = i2c_master_transmit_receive(
        g_rtc_dev, &control_register, 1, &snapshot->control1, 1, 100);
    if (err == ESP_OK) {
        uint8_t civil_register = 0x04;
        err = i2c_master_transmit_receive(
            g_rtc_dev, &civil_register, 1, snapshot->civil,
            sizeof(snapshot->civil), 100);
    }
    i2c_give();
    return err;
}

static esp_err_t rtc_write_civil_time_locked(
    int hour, int minute, int year, int month, int day, int second)
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
    if (!i2c_take(pdMS_TO_TICKS(50))) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_transmit(g_rtc_dev, data, sizeof(data), 100);
    if (err == ESP_OK) {
        uint8_t ctrl[2] = {0x00, 0x00};
        err = i2c_master_transmit(g_rtc_dev, ctrl, sizeof(ctrl), 100);
    }
    i2c_give();
    return err;
}

static esp_err_t rtc_restore_raw_locked(const rtc_raw_time_t *snapshot)
{
    if (snapshot == NULL || !g_rtc_ready || g_rtc_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!i2c_take(pdMS_TO_TICKS(50))) {
        return ESP_ERR_TIMEOUT;
    }
    uint8_t civil[1 + sizeof(snapshot->civil)];
    civil[0] = 0x04;
    memcpy(civil + 1, snapshot->civil, sizeof(snapshot->civil));
    esp_err_t civil_err = i2c_master_transmit(
        g_rtc_dev, civil, sizeof(civil), 100);
    uint8_t control[2] = {0x00, snapshot->control1};
    esp_err_t control_err = i2c_master_transmit(
        g_rtc_dev, control, sizeof(control), 100);
    i2c_give();
    return civil_err != ESP_OK ? civil_err : control_err;
}

static int board_time_transaction_snapshot_rtc(void *opaque)
{
    board_time_transaction_context_t *context = opaque;
    context->rtc_snapshot_error =
        rtc_snapshot_raw_locked(&context->previous_rtc);
    return context->rtc_snapshot_error == ESP_OK;
}

static int board_time_transaction_store_offset(void *opaque, int known,
                                               int offset_minutes)
{
    board_time_transaction_context_t *context = opaque;
    esp_err_t err = time_offset_store_state(known, offset_minutes);
    if (context->offset_store_calls++ == 0) {
        context->offset_store_error = err;
    } else {
        context->offset_restore_error = err;
    }
    return err == ESP_OK;
}

static int board_time_transaction_write_rtc(void *opaque)
{
    board_time_transaction_context_t *context = opaque;
    context->rtc_write_error = rtc_write_civil_time_locked(
        context->hour, context->minute, context->year, context->month,
        context->day, context->second);
    if (context->rtc_write_error == ESP_OK && context->anchor_at_rtc_write) {
        context->target_anchor.monotonic_ms = board_monotonic_ms();
    }
    return context->rtc_write_error == ESP_OK;
}

static int board_time_transaction_restore_rtc(void *opaque)
{
    board_time_transaction_context_t *context = opaque;
    context->rtc_restore_error =
        rtc_restore_raw_locked(&context->previous_rtc);
    return context->rtc_restore_error == ESP_OK;
}

static void board_time_transaction_publish(void *opaque)
{
    board_time_transaction_context_t *context = opaque;
    if (context->update_utc_offset) {
        time_offset_publish(context->utc_offset_minutes);
    }
    board_time_publish_prevalidated(&context->target_anchor, 1);
    if (context->publish_time_sync_known) {
        portENTER_CRITICAL(&g_connectivity_lock);
        g_time_sync_state.time_known = 1;
        portEXIT_CRITICAL(&g_connectivity_lock);
    }
    board_time_mark_pending();
}

/* Caller holds g_time_transaction_lock. This is the only path that spans
 * RTC and UTC-offset durability, and it owns g_rtc_sequence_lock throughout. */
static board_time_commit_result_t board_time_commit_locked(
    int hour, int minute, int year, int month, int day, int second,
    const pj_time_clock_anchor_t *target_anchor, int anchor_at_rtc_write,
    int update_utc_offset, int utc_offset_minutes,
    int publish_time_sync_known)
{
    board_time_commit_result_t outcome = {0};
    if (target_anchor == NULL || !target_anchor->valid) {
        outcome.transaction.status = PJ_TIME_TRANSACTION_INVALID;
        return outcome;
    }

    int old_offset_known;
    int old_offset_minutes;
    portENTER_CRITICAL(&g_connectivity_lock);
    old_offset_known = g_utc_offset_known;
    old_offset_minutes = g_utc_offset_minutes;
    portEXIT_CRITICAL(&g_connectivity_lock);

    if (!rtc_sequence_take(pdMS_TO_TICKS(500))) {
        outcome.transaction.status =
            PJ_TIME_TRANSACTION_RTC_SNAPSHOT_FAILED;
        outcome.rtc_snapshot_error = ESP_ERR_TIMEOUT;
        return outcome;
    }

    board_time_transaction_context_t context = {
        .target_anchor = *target_anchor,
        .hour = hour,
        .minute = minute,
        .year = year,
        .month = month,
        .day = day,
        .second = second,
        .anchor_at_rtc_write = anchor_at_rtc_write,
        .update_utc_offset = update_utc_offset,
        .utc_offset_minutes = utc_offset_minutes,
        .publish_time_sync_known = publish_time_sync_known,
        .rtc_snapshot_error = ESP_OK,
        .offset_store_error = ESP_OK,
        .rtc_write_error = ESP_OK,
        .rtc_restore_error = ESP_OK,
        .offset_restore_error = ESP_OK,
    };
    pj_time_transaction_request_t request = {
        .update_offset = update_utc_offset,
        .old_offset_known = old_offset_known,
        .old_offset_minutes = old_offset_minutes,
        .new_offset_minutes = utc_offset_minutes,
    };
    const pj_time_transaction_ops_t ops = {
        .context = &context,
        .snapshot_rtc = board_time_transaction_snapshot_rtc,
        .store_offset = board_time_transaction_store_offset,
        .write_rtc = board_time_transaction_write_rtc,
        .restore_rtc = board_time_transaction_restore_rtc,
        .publish = board_time_transaction_publish,
    };
    outcome.transaction = pj_time_transaction_execute(&request, &ops);
    rtc_sequence_give();
    outcome.rtc_snapshot_error = context.rtc_snapshot_error;
    outcome.offset_store_error = context.offset_store_error;
    outcome.rtc_write_error = context.rtc_write_error;
    outcome.rtc_restore_error = context.rtc_restore_error;
    outcome.offset_restore_error = context.offset_restore_error;
    return outcome;
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
    if (!time_transaction_take()) {
        return PJ_TIME_CONTROLLER_WAKE_ERROR;
    }
    pj_time_controller_wake_result_t outcome;
    if (deadline == NULL) {
        outcome = rtc_wake_disarm_board(NULL, 0) == PJ_RTC_WAKE_OK
            ? PJ_TIME_CONTROLLER_WAKE_OK
            : PJ_TIME_CONTROLLER_WAKE_ERROR;
    } else {
        outcome = rtc_wake_sync_locked() >= 0 ? PJ_TIME_CONTROLLER_WAKE_OK
                                              : PJ_TIME_CONTROLLER_WAKE_ERROR;
    }
    time_transaction_give();
    return outcome;
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
        board_status_set_error("%s", message);
        ESP_LOGW(TAG, "%s", message);
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

/* Caller owns g_time_transaction_lock so the plan and RTC share one time basis. */
static int rtc_wake_sync_locked(void)
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
        board_status_set_error("RTC wake setup failed at %s",
                               rtc_wake_result_name(result));
        ESP_LOGW(TAG, "RTC wake setup failed at %s",
                 rtc_wake_result_name(result));
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
        board_status_set_error("RTC GPIO5 wake setup failed: %s",
                               esp_err_to_name(err));
        ESP_LOGW(TAG, "RTC GPIO5 wake setup failed: %s",
                 esp_err_to_name(err));
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

static int rtc_wake_sync(void)
{
    if (!time_transaction_take()) {
        return -1;
    }
    int result = rtc_wake_sync_locked();
    time_transaction_give();
    return result;
}

static void rtc_wake_restore(void)
{
    pj_rtc_wake_plan_t persisted;
    rtc_wake_load_result_t load_result = rtc_wake_plan_load(&persisted);
    if (load_result == RTC_WAKE_LOAD_IO_ERROR) {
        board_status_set_error(
            "RTC wake metadata read failed; persistence left untouched");
        ESP_LOGW(TAG,
                 "RTC wake metadata read failed; persistence left untouched");
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
        board_status_set_error(
            "RTC wake metadata was invalid; clearing hardware alarm");
        ESP_LOGW(TAG,
                 "RTC wake metadata was invalid; clearing hardware alarm");
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

static int time_state_apply_audio_ack(void)
{
    uint64_t alert_id = alert_audio_take_ack();
    const pj_time_state_t *state = pj_time_controller_state(&g_time_controller);
    if (alert_id == 0 || state == NULL || state->active_alert.id != alert_id) {
        return 0;
    }
    pj_time_controller_command_t command = {
        .type = PJ_TIME_CONTROLLER_COMMAND_ALERT_DISMISS,
        .alert_id = alert_id,
    };
    pj_time_controller_result_t result;
    (void)pj_time_controller_apply(&g_time_controller, &command, &result);
    return result.command_applied;
}

static int time_state_interval_active(void)
{
    const pj_time_state_t *state =
        pj_time_controller_state(&g_time_controller);
    if (state == NULL) {
        return 0;
    }
    if (state->interval.running || state->interval.remaining_ms != 0 ||
        state->active_alert.source == PJ_TIME_ALERT_INTERVAL) {
        return 1;
    }
    for (size_t i = 0; i < state->pending_count; ++i) {
        if (state->pending[i].source == PJ_TIME_ALERT_INTERVAL) {
            return 1;
        }
    }
    return 0;
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

static int recording_copy_for_processing(const char *source_path,
                                         const char *temporary_path,
                                         uint32_t data_bytes)
{
    if (!recording_file_valid(source_path, data_bytes)) {
        ESP_LOGE(TAG, "Audio processing source is not a valid recording: %s",
                 source_path);
        return 0;
    }
    if (remove(temporary_path) != 0 && errno != ENOENT) {
        ESP_LOGE(TAG, "Audio processing stale temp removal failed: %s errno=%d",
                 temporary_path, errno);
        return 0;
    }
    FILE *source = fopen(source_path, "rb");
    FILE *temporary = source != NULL ? fopen(temporary_path, "wb") : NULL;
    if (source == NULL || temporary == NULL) {
        ESP_LOGE(TAG, "Audio processing copy open failed: %s -> %s",
                 source_path, temporary_path);
        if (source != NULL) {
            fclose(source);
        }
        if (temporary != NULL) {
            fclose(temporary);
        }
        remove(temporary_path);
        return 0;
    }

    uint8_t buffer[PJ_AUDIO_IO_BUFFER_BYTES];
    int copied = 1;
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), source)) > 0U) {
        if (fwrite(buffer, 1, bytes_read, temporary) != bytes_read) {
            copied = 0;
            break;
        }
        taskYIELD();
    }
    copied = copied && !ferror(source) && fflush(temporary) == 0 &&
             fsync(fileno(temporary)) == 0;
    copied = fclose(source) == 0 && copied;
    copied = fclose(temporary) == 0 && copied;
    if (!copied || !recording_file_valid(temporary_path, data_bytes)) {
        ESP_LOGE(TAG, "Audio processing copy failed validation: %s",
                 temporary_path);
        remove(temporary_path);
        return 0;
    }
    return 1;
}

static int recording_file_valid_for_replace(const char *path, void *context)
{
    return context != NULL &&
           recording_file_valid(path, *(const uint32_t *)context);
}

static int recording_replace_with_processed(const char *temporary_path,
                                            const char *final_path,
                                            uint32_t data_bytes,
                                            int *sync_resume_needed)
{
    if (sync_resume_needed == NULL) {
        return 0;
    }
    *sync_resume_needed = 0;
    const char *filename = strrchr(final_path, '/');
    char transcript_path[PJ_NOTE_TRANSCRIPT_PATH_LEN];
    if (filename == NULL || filename[1] == '\0' ||
        !transcript_path_for_audio(transcript_path, sizeof(transcript_path),
                                   filename + 1)) {
        return 0;
    }
    char transcript_label[PJ_UI_NOTE_LABEL_LEN];
    int invalidated_synced_note = pj_transcript_marker_load(
        transcript_path, transcript_label, sizeof(transcript_label));
    pj_board_sync_inventory_mutation_t sync_mutation;
    if (!pj_board_companion_sync_inventory_mutation_begin(
            &sync_mutation, invalidated_synced_note)) {
        ESP_LOGE(TAG, "Audio processing publication blocked by Sync barrier: %s",
                 final_path);
        return 0;
    }
    memset(&g_usb_audio_identity, 0, sizeof(g_usb_audio_identity));
    uint32_t expected_data_bytes = data_bytes;
    int replaced = pj_recording_replace_processed(
            temporary_path, final_path, transcript_path,
            recording_file_valid_for_replace, &expected_data_bytes);
    *sync_resume_needed = sync_mutation.start_pending;
    int sync_ready =
        pj_board_companion_sync_inventory_mutation_finish(&sync_mutation);
    if (!replaced) {
        ESP_LOGE(TAG, "Audio processing publication failed: %s", final_path);
        return 0;
    }
    if (!sync_ready) {
        ESP_LOGW(TAG, "Processed audio published with durable Sync retry pending: %s",
                 final_path);
    }
    return 1;
}

static int audio_process_wait_for_publication(void)
{
    TickType_t started = xTaskGetTickCount();
    TickType_t max_wait = pdMS_TO_TICKS(PJ_AUDIO_PROCESS_SWAP_WAIT_MS);
    do {
        if (storage_audio_publication_try_begin()) {
            return 1;
        }
        vTaskDelay(pdMS_TO_TICKS(PJ_AUDIO_PROCESS_SWAP_POLL_MS));
    } while ((TickType_t)(xTaskGetTickCount() - started) < max_wait);
    return storage_audio_publication_try_begin();
}

static int audio_process_recording(const audio_process_args_t *args,
                                   int *publication_owned,
                                   int *sync_resume_needed)
{
    char temporary_path[144];
    int path_length = snprintf(temporary_path, sizeof(temporary_path),
                               "%s.tmp", args->final_path);
    if (publication_owned == NULL || sync_resume_needed == NULL ||
        path_length < 0 ||
        path_length >= (int)sizeof(temporary_path) ||
        !recording_copy_for_processing(args->final_path, temporary_path,
                                       args->data_bytes)) {
        return 0;
    }
    *publication_owned = 0;
    *sync_resume_needed = 0;

    FILE *file = fopen(temporary_path, "rb+");
    if (file == NULL) {
        ESP_LOGE(TAG, "Audio processing open failed: %s", temporary_path);
        remove(temporary_path);
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
                recording_file_valid(temporary_path, args->data_bytes);
    if (!ready) {
        ESP_LOGE(TAG, "Audio processing failed: %s", temporary_path);
        remove(temporary_path);
        return 0;
    }
    if (!audio_process_wait_for_publication()) {
        ESP_LOGW(TAG, "Audio processing publication timed out; raw retained: %s",
                 args->final_path);
        remove(temporary_path);
        return 0;
    }
    *publication_owned = 1;
    if (!recording_replace_with_processed(
            temporary_path, args->final_path, args->data_bytes,
            sync_resume_needed)) {
        remove(temporary_path);
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
    (void)arg;
    audio_process_args_t args;
    for (;;) {
        if (xQueueReceive(g_audio_process_queue, &args, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        int lifecycle_started = 0;
        if (audio_lifecycle_take()) {
            lifecycle_started =
                pj_audio_lifecycle_begin_processing(&g_audio_lifecycle);
            audio_lifecycle_give();
        }
        if (!lifecycle_started) {
            ESP_LOGW(TAG, "Audio processing job skipped: lifecycle unavailable; raw retained: %s",
                     args.final_path);
            continue;
        }

        int storage_owned = storage_shared_try_acquire();
        int publication_owned = 0;
        int sync_resume_needed = 0;
        int enhanced = storage_owned &&
                       audio_process_recording(
                           &args, &publication_owned, &sync_resume_needed);
        if (publication_owned) {
            storage_audio_publication_finish();
            if (sync_resume_needed &&
                !pj_board_companion_sync_resume()) {
                ESP_LOGW(TAG, "Unable to resume queued Sync after audio publication");
            }
            board_update_publish(BOARD_UPDATE_NOTES);
        } else if (storage_owned) {
            storage_shared_release();
        }
        if (!enhanced) {
            ESP_LOGW(TAG, "Audio enhancement skipped or failed; raw note retained: %s",
                     args.final_path);
        }
        if (audio_lifecycle_take()) {
            pj_audio_lifecycle_finish_processing(&g_audio_lifecycle);
            audio_lifecycle_give();
        }
    }
}

static int audio_process_worker_start(void)
{
    if (g_audio_process_queue == NULL) {
        g_audio_process_queue = xQueueCreateStatic(
            PJ_AUDIO_PROCESS_QUEUE_LENGTH, sizeof(audio_process_args_t),
            g_audio_process_queue_buffer, &g_audio_process_queue_storage);
    }
    if (g_audio_process_queue == NULL) {
        return 0;
    }
    if (g_audio_process_task == NULL) {
        BaseType_t created = xTaskCreate(
            audio_process_task, "pj-audio-process", PJ_AUDIO_PROCESS_TASK_STACK,
            NULL, 1, &g_audio_process_task);
        if (created != pdPASS) {
            g_audio_process_task = NULL;
            return 0;
        }
    }
    return 1;
}

static int audio_process_enqueue(const audio_process_args_t *args)
{
    return args != NULL && audio_process_worker_start() &&
           xQueueSend(g_audio_process_queue, args, 0) == pdTRUE;
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
            .pa_gain = PJ_AUDIO_CODEC_PA_GAIN_COMPENSATION_DB,
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
    if (!audio_process_worker_start()) {
        ESP_LOGW(TAG, "Audio processing worker unavailable; raw recordings will be retained");
    }
    g_audio_ready = 1;
    ESP_LOGI(TAG, "ES8311 audio initialized via esp_codec_dev");
    return ESP_OK;
}

static void record_task_exit(int sync_resume_needed)
{
    if (g_record_audio_owned) {
        g_record_audio_owned = 0;
        xSemaphoreGive(g_audio_lock);
    }
    record_storage_release();
    if (sync_resume_needed && !pj_board_companion_sync_resume()) {
        ESP_LOGW(TAG,
                 "Unable to resume queued Sync after recording publication");
    }
    recording_publish_completion();
    board_audio_state_set(0, -1);
    alert_audio_set_recording(0);
    board_update_publish(BOARD_UPDATE_AUDIO);
    if (audio_lifecycle_take()) {
        g_record_task = NULL;
        pj_audio_lifecycle_finish_record(&g_audio_lifecycle);
        audio_lifecycle_give();
    }
    vTaskDelete(NULL);
}

static void playback_task_exit(void)
{
    board_audio_state_set(-1, 0);
    alert_audio_notify();
    board_update_publish(BOARD_UPDATE_AUDIO);
    playback_storage_release();
    if (audio_lifecycle_take()) {
        g_playback_task = NULL;
        pj_audio_lifecycle_finish_playback(&g_audio_lifecycle);
        audio_lifecycle_give();
    }
    vTaskDelete(NULL);
}

static void record_task(void *arg)
{
    (void)arg;
    if (g_audio_lock == NULL || xSemaphoreTake(g_audio_lock, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Recording could not acquire audio ownership");
        board_status_set_error("recording could not acquire audio ownership");
        recording_state_finish_capture(0);
        record_task_exit(0);
        return;
    }
    g_record_audio_owned = 1;
    int16_t *stereo = g_record_stereo_buffer;
    int16_t *mono = g_record_mono_buffer;
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
    FILE *file = fopen(temporary_path, "wb+");
    if (file == NULL) {
        ESP_LOGE(TAG, "Recording open failed: %s", temporary_path);
        board_status_set_error("recording file open failed");
        recording_state_finish_capture(0);
        record_task_exit(0);
        return;
    }
    if (!wav_write_header(file, 0, PJ_AUDIO_CHANNELS, PJ_AUDIO_SAMPLE_RATE)) {
        ESP_LOGE(TAG, "Recording initial WAV header failed: %s", temporary_path);
        fclose(file);
        remove(temporary_path);
        board_status_set_error("recording WAV header creation failed");
        recording_state_finish_capture(0);
        record_task_exit(0);
        return;
    }
    int gain_ret = esp_codec_dev_set_in_gain(g_audio_record_codec, PJ_AUDIO_MIC_GAIN_DB);
    if (gain_ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Recording input gain setup failed: %s", esp_err_to_name((esp_err_t)gain_ret));
        fclose(file);
        remove(temporary_path);
        board_status_set_error("recording input gain setup failed");
        recording_state_finish_capture(0);
        record_task_exit(0);
        return;
    }
    ESP_LOGI(TAG,
             "Recording capture started: %s workspace=%u internal_free=%u largest=%u",
             g_active_recording_path,
             (unsigned)(sizeof(g_record_stereo_buffer) + sizeof(g_record_mono_buffer)),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    int capture_failed = 0;
    while (ulTaskNotifyTake(pdTRUE, 0) == 0U) {
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
    if (!finalized) {
        recording_state_finish_capture(0);
        ESP_LOGE(TAG, "Recording finalization failed or produced no audio: %s", temporary_path);
        board_status_set_error(
            "recording capture or WAV finalization failed");
        remove(temporary_path);
        record_task_exit(0);
        return;
    }

    int sync_resume_needed = 0;
    pj_recording_raw_publish_result_t publish_result =
        recording_publish_file(temporary_path, g_active_recording_path,
                               data_bytes, &sync_resume_needed);
    int note_ready = publish_result == PJ_RECORDING_RAW_PUBLISH_SUCCEEDED;
    recording_state_finish_capture(note_ready);
    if (!note_ready) {
        board_status_set_error(
            publish_result == PJ_RECORDING_RAW_PUBLISH_RETRYABLE ?
            "recording publication deferred; finalized audio retained for recovery" :
            "recording raw publication failed");
        record_task_exit(sync_resume_needed);
        return;
    }

    struct stat st;
    long file_size = stat(g_active_recording_path, &st) == 0 ?
                     (long)st.st_size : -1;
    ESP_LOGI(TAG, "Recording raw note published: %s bytes=%u file_size=%ld read_errors=%u "
             "raw_peak=%u raw_avg_abs=%u raw_clipped=%u",
             g_active_recording_path, (unsigned)data_bytes, file_size, (unsigned)read_errors,
             (unsigned)peak, (unsigned)raw_avg, (unsigned)clipped_samples);

    audio_process_args_t process_args = {
        .data_bytes = data_bytes,
        .raw_peak = peak,
        .raw_avg = raw_avg,
        .raw_clipped = clipped_samples,
        .input_channel = input_channel,
    };
    (void)snprintf(process_args.final_path, sizeof(process_args.final_path),
                   "%s", g_active_recording_path);
    if (audio_process_enqueue(&process_args)) {
        ESP_LOGI(TAG, "Audio enhancement queued: %s", process_args.final_path);
    } else {
        ESP_LOGW(TAG, "Audio enhancement queue unavailable or full; raw note retained: %s",
                 process_args.final_path);
    }
    record_task_exit(sync_resume_needed);
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
    if (ulTaskNotifyTake(pdTRUE, 0) != 0U) {
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
    while (ulTaskNotifyTake(pdTRUE, 0) == 0U) {
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
    if (audio_lifecycle_active() || alert_audio_desired()) {
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
    if (audio_lifecycle_active() || alert_audio_desired()) {
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

static void init_default_status(const pj_board_profile_t *profile)
{
#ifdef ESP_PLATFORM
    board_status_take();
#endif
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
    (void)board_time_initialize_unknown();
#else
    g_status.hour = 9;
    g_status.minute = 41;
    g_status.year = 2026;
    g_status.month = 6;
    g_status.day = 6;
    g_status.time_set = 1;
#endif
    (void)snprintf(g_status.device_id, sizeof(g_status.device_id), "pj-%s", profile->sku);
    (void)snprintf(g_status.token, sizeof(g_status.token), "%s", PJ_DEFAULT_TOKEN);
    (void)snprintf(g_status.ip_addr, sizeof(g_status.ip_addr), "0.0.0.0");
    (void)snprintf(g_status.storage_path, sizeof(g_status.storage_path), "/sdcard");
    (void)snprintf(g_status.last_error, sizeof(g_status.last_error),
                   "BLE/Wi-Fi connect path still requires provisioning integration");
#ifdef ESP_PLATFORM
    board_status_give();
#endif
}

void pj_board_init(const pj_board_profile_t *profile)
{
#ifdef ESP_PLATFORM
    g_status_lock = xSemaphoreCreateRecursiveMutexStatic(&g_status_lock_storage);
    g_audio_lifecycle_lock =
        xSemaphoreCreateMutexStatic(&g_audio_lifecycle_lock_storage);
    g_provisioning_lock =
        xSemaphoreCreateMutexStatic(&g_provisioning_lock_storage);
    g_board_update_events =
        xEventGroupCreateStatic(&g_board_update_events_storage);
    if (g_status_lock == NULL || g_audio_lifecycle_lock == NULL ||
        g_provisioning_lock == NULL || g_board_update_events == NULL) {
        ESP_LOGE(TAG, "Board synchronization primitive initialization failed");
        return;
    }
    pj_audio_lifecycle_init(&g_audio_lifecycle);
#endif
    init_default_status(profile);
    pj_settings_defaults(&g_settings);

#ifdef ESP_PLATFORM
    (void)snprintf(g_provisioned_token, sizeof(g_provisioned_token), "%s",
                   PJ_DEFAULT_TOKEN);
    g_runtime_boot_id = esp_random();
    if (g_runtime_boot_id == 0U) {
        g_runtime_boot_id = 1U;
    }
    ESP_LOGI(TAG, "Runtime identity: boot_id=%" PRIu32 " reset_reason=%s",
             g_runtime_boot_id, pj_reset_reason_name(runtime_reset_reason()));
    pj_storage_coordinator_init(&g_storage_coordinator);
    pj_recording_init(&g_recording);
    pj_usb_upload_init(&g_usb_upload);
    memset(&g_usb_audio_identity, 0, sizeof(g_usb_audio_identity));
    g_json_write_lock = xSemaphoreCreateMutex();
    if (g_json_write_lock == NULL) {
        ESP_LOGW(TAG, "JSON write mutex allocation failed");
    }
    g_time_transaction_lock = xSemaphoreCreateMutexStatic(
        &g_time_transaction_lock_storage);
    if (g_time_transaction_lock == NULL) {
        ESP_LOGE(TAG, "Time transaction mutex initialization failed");
    }
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        board_status_set_service(BOARD_SERVICE_STORAGE, PJ_BOARD_SERVICE_ERROR);
        board_status_set_error("NVS init failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return;
    }
    time_offset_load();

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
        board_status_set_error("settings load failed: %s",
                               esp_err_to_name(settings_err));
        ESP_LOGW(TAG, "Settings load failed: %s; preserving in-memory defaults "
                      "without writing NVS",
                 esp_err_to_name(settings_err));
    }
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        board_status_take();
        (void)snprintf(g_status.device_id, sizeof(g_status.device_id), "pj-%02x%02x%02x", mac[3], mac[4], mac[5]);
        board_status_give();
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
                board_time_snapshot_t time = board_time_snapshot();
                ESP_LOGI(TAG, "PCF85063 RTC initialized: %04d-%02d-%02d %02d:%02d",
                         time.year, time.month, time.day, time.hour,
                         time.minute);
            } else {
                ESP_LOGW(TAG,
                         "PCF85063 RTC initialized but time is unknown; waiting for host or SNTP");
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
        board_status_set_service(BOARD_SERVICE_DISPLAY, PJ_BOARD_SERVICE_READY);
        ESP_LOGI(TAG, "e-paper display initialized");
    } else {
        board_status_set_service(BOARD_SERVICE_DISPLAY, PJ_BOARD_SERVICE_ERROR);
        board_status_set_error("display init failed: %s",
                               esp_err_to_name(display_err));
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(display_err));
    }

    esp_err_t storage_err = storage_init();
    if (storage_err == ESP_OK) {
        board_status_set_service(BOARD_SERVICE_STORAGE, PJ_BOARD_SERVICE_READY);
        pj_board_status_t status = board_status_snapshot_base();
        ESP_LOGI(TAG, "microSD mounted at %s", status.storage_path);
        remove(PJ_USB_TRANSCRIPT_TEMP_PATH);
    } else {
        pj_board_service_state_t storage_state =
            storage_err == ESP_ERR_NOT_FOUND || storage_err == ESP_ERR_TIMEOUT ||
            storage_err == ESP_ERR_INVALID_RESPONSE ?
                PJ_BOARD_SERVICE_UNAVAILABLE : PJ_BOARD_SERVICE_ERROR;
        board_status_set_service(BOARD_SERVICE_STORAGE, storage_state);
        board_status_set_error(
            "microSD mount failed: %s; check card inserted and FAT32/exFAT formatting",
            esp_err_to_name(storage_err));
        ESP_LOGW(TAG, "microSD mount failed: %s", esp_err_to_name(storage_err));
    }
    esp_err_t audio_err = audio_init();
    if (audio_err == ESP_OK) {
        board_status_set_service(BOARD_SERVICE_AUDIO, PJ_BOARD_SERVICE_READY);
        esp_err_t alert_err = alert_audio_start();
        if (alert_err != ESP_OK) {
            board_status_set_error("time alert audio task failed: %s",
                                   esp_err_to_name(alert_err));
            ESP_LOGW(TAG, "Time alert audio task failed: %s",
                     esp_err_to_name(alert_err));
        }
    } else {
        board_status_set_service(BOARD_SERVICE_AUDIO, PJ_BOARD_SERVICE_ERROR);
        board_status_set_error("audio init failed: %s",
                               esp_err_to_name(audio_err));
        ESP_LOGW(TAG, "Audio init failed: %s", esp_err_to_name(audio_err));
    }

    if (wifi_credentials_stored_snapshot()) {
        wifi_apply_provisioning_status();
    } else {
        board_status_set_service(BOARD_SERVICE_BLE,
                                 PJ_BOARD_SERVICE_UNAVAILABLE);
        board_status_set_service(BOARD_SERVICE_WIFI,
                                 PJ_BOARD_SERVICE_UNAVAILABLE);
    }
#else
    (void)profile;
#endif
}

int pj_board_start_services(const pj_board_profile_t *profile)
{
    int services_ready = 1;
#ifdef ESP_PLATFORM
    pj_board_status_t status = board_status_snapshot_base();
    pj_ota_init(status.device_id, provisioned_token_read, profile->name,
                ota_mutations_reserve, ota_mutations_release);
#else
    (void)profile;
#endif
    if (!pj_board_http_start()) {
        services_ready = 0;
    }
#ifdef ESP_PLATFORM
    int wifi_provisioned = wifi_credentials_stored_snapshot();
    if (!wifi_provisioned) {
        esp_err_t ble_err = ble_provisioning_start();
        if (ble_err != ESP_OK) {
            board_status_set_service(BOARD_SERVICE_BLE,
                                     PJ_BOARD_SERVICE_ERROR);
            board_status_set_error("BLE provisioning start failed: %s",
                                   esp_err_to_name(ble_err));
            ESP_LOGE(TAG, "BLE provisioning start failed: %s",
                     esp_err_to_name(ble_err));
            services_ready = 0;
        }
    } else {
        board_status_set_service(BOARD_SERVICE_BLE,
                                 PJ_BOARD_SERVICE_UNAVAILABLE);
        ESP_LOGI(TAG, "BLE provisioning skipped because Wi-Fi credentials are stored");
    }
    if (wifi_provisioned) {
        esp_err_t wifi_err = wifi_start_or_reconfigure();
        if (wifi_err != ESP_OK) {
            board_status_set_service(BOARD_SERVICE_WIFI,
                                     PJ_BOARD_SERVICE_ERROR);
            board_status_set_error("Wi-Fi start failed: %s",
                                   esp_err_to_name(wifi_err));
            ESP_LOGE(TAG, "Wi-Fi start failed: %s",
                     esp_err_to_name(wifi_err));
            services_ready = 0;
        }
    }
    if (!sync_inventory_worker_ensure()) {
        ESP_LOGE(TAG, "Sync inventory worker failed to start");
        services_ready = 0;
    }
    esp_err_t serial_err = serial_command_task_start();
    if (serial_err != ESP_OK) {
        ESP_LOGE(TAG, "USB-C command task failed to start: %s",
                 esp_err_to_name(serial_err));
        services_ready = 0;
    }
    status = board_status_snapshot_base();
    services_ready = services_ready && g_aux_task_started &&
        (!g_touch_ready || g_touch_task_started) &&
        (status.audio != PJ_BOARD_SERVICE_READY ||
         g_alert_audio_task != NULL);
#endif
    return services_ready;
}

void pj_board_confirm_boot_health(int startup_and_ui_ready)
{
#ifdef ESP_PLATFORM
    pj_board_status_t status = pj_board_status();
    pj_ota_confirm_boot_health(
        startup_and_ui_ready &&
        status.display == PJ_BOARD_SERVICE_READY &&
        status.storage != PJ_BOARD_SERVICE_ERROR &&
        status.audio == PJ_BOARD_SERVICE_READY &&
        status.http == PJ_BOARD_SERVICE_READY);
#else
    (void)startup_and_ui_ready;
#endif
}

pj_board_status_t pj_board_status(void)
{
#ifdef ESP_PLATFORM
    int transaction_locked = time_transaction_take();
    /* Each independently owned subsystem contributes one coherent snapshot. */
    pj_board_status_t status = board_status_snapshot_base();
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
    if (transaction_locked) {
        time_transaction_give();
    }
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
    uint32_t before = pj_ui_visual_revision(ui);
    const pj_ui_preferences_t preferences = {
        .volume = settings->volume,
        .dark_mode = settings->dark_mode,
        .alarm_enabled = settings->alarm_enabled,
        .alarm_hour = settings->alarm_hour,
        .alarm_minute = settings->alarm_minute,
        .timer_seconds = settings->timer_seconds,
        .interval_seconds = settings->interval_seconds,
        .clock_24h = settings->clock_24h,
        .temperature_fahrenheit = settings->temperature_fahrenheit,
        .transcript_font_size = settings->transcript_font_size,
    };
    pj_ui_apply_preferences(ui, &preferences);
    return pj_ui_visual_revision(ui) != before;
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
    if (board_update_pending(BOARD_UPDATE_SETTINGS)) {
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
        board_update_publish(BOARD_UPDATE_SETTINGS);
    }
    settings_give();
    if (err != ESP_OK) {
        board_status_set_error("settings save failed: %s",
                               esp_err_to_name(err));
        ESP_LOGW(TAG, "Settings save failed: %s", esp_err_to_name(err));
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
#ifdef ESP_PLATFORM
    if (!board_update_take(BOARD_UPDATE_SETTINGS)) {
        return 0;
    }
#else
    if (!g_settings_update_pending) {
        return 0;
    }
    g_settings_update_pending = 0;
#endif
    pj_board_refresh_settings(ui);
    pj_board_refresh_time_state(ui);
    return 1;
}

int pj_board_sync_inventory_snapshot(pj_board_sync_inventory_t *inventory)
{
    if (inventory == NULL) {
        return 0;
    }
    *inventory = (pj_board_sync_inventory_t) {
        .state = PJ_BOARD_SYNC_INVENTORY_ERROR,
    };
#ifdef ESP_PLATFORM
    pj_board_status_t status = board_status_snapshot_base();
    inventory->online = status.wifi == PJ_BOARD_SERVICE_READY;
    if (status.storage != PJ_BOARD_SERVICE_READY) {
        pj_board_sync_inventory_cancel();
        return 1;
    }
    if (!sync_inventory_worker_ensure()) {
        return 1;
    }

    int notify_worker = 0;
    xSemaphoreTake(g_sync_inventory_lock, portMAX_DELAY);
    if (g_sync_inventory_worker_state == PJ_SYNC_INVENTORY_RESULT) {
        *inventory = g_sync_inventory_result;
        inventory->online = status.wifi == PJ_BOARD_SERVICE_READY;
        g_sync_inventory_worker_state = PJ_SYNC_INVENTORY_IDLE;
    } else {
        inventory->state = PJ_BOARD_SYNC_INVENTORY_BUSY;
        if (g_sync_inventory_worker_state == PJ_SYNC_INVENTORY_IDLE) {
            g_sync_inventory_discard = 0;
            g_sync_inventory_worker_state = PJ_SYNC_INVENTORY_REQUESTED;
            notify_worker = 1;
        }
    }
    xSemaphoreGive(g_sync_inventory_lock);
    if (notify_worker) {
        xTaskNotifyGive(g_sync_inventory_task);
    }
#endif
    return 1;
}

void pj_board_sync_inventory_cancel(void)
{
#ifdef ESP_PLATFORM
    if (g_sync_inventory_lock == NULL) {
        return;
    }
    xSemaphoreTake(g_sync_inventory_lock, portMAX_DELAY);
    if (g_sync_inventory_worker_state == PJ_SYNC_INVENTORY_RUNNING) {
        g_sync_inventory_discard = 1;
    } else {
        g_sync_inventory_discard = 0;
        g_sync_inventory_worker_state = PJ_SYNC_INVENTORY_IDLE;
    }
    xSemaphoreGive(g_sync_inventory_lock);
#endif
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
    pj_board_status_t before = board_status_snapshot_base();
    if (before.storage_mounted && storage_shared_try_acquire()) {
        (void)storage_refresh_capacity();
        storage_shared_release();
    }
#endif
    pj_board_status_t status = pj_board_status();
    if (status.time_set) {
        pj_ui_set_time(ui, status.hour, status.minute, status.year, status.month, status.day);
    }
    pj_ui_set_audio_state(ui, status.recording, status.playback_active);
    pj_ui_set_status(ui, status.battery_percent, status.temperature_c,
                     status.humidity_percent);
#ifdef ESP_PLATFORM
    if (status.storage == PJ_BOARD_SERVICE_READY) {
        refresh_ui_notes_from_sd(ui);
        refresh_ui_sync_state(ui);
    }
#endif
}

void pj_board_refresh_notes(pj_ui_context_t *ui)
{
#ifdef ESP_PLATFORM
    pj_board_status_t status = pj_board_status();
    if (ui != NULL && status.storage == PJ_BOARD_SERVICE_READY) {
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
    if (ui == NULL || !board_update_take(BOARD_UPDATE_AUDIO)) {
        return 0;
    }
    pj_board_status_t status = pj_board_status();
    pj_ui_set_audio_state(ui, status.recording, status.playback_active);
    if (time_state_apply_audio_ack()) {
        (void)time_state_project(ui);
    }
    return 1;
#else
    (void)ui;
    return 0;
#endif
}

int pj_board_consume_notes_update(pj_ui_context_t *ui)
{
#ifdef ESP_PLATFORM
    if (ui == NULL || !board_update_take(BOARD_UPDATE_NOTES)) {
        return 0;
    }
    pj_board_status_t status = pj_board_status();
    if (status.storage != PJ_BOARD_SERVICE_READY) {
        board_update_publish(BOARD_UPDATE_NOTES);
        return 0;
    }
    refresh_ui_notes_from_sd(ui);
    refresh_ui_sync_state(ui);
    return 1;
#else
    (void)ui;
    return 0;
#endif
}

int pj_board_tick_time(pj_ui_context_t *ui)
{
    if (ui == NULL) {
        return 0;
    }
    board_time_snapshot_t time = board_time_snapshot();
    if (!time.time_set ||
        (ui->hour == time.hour && ui->minute == time.minute &&
         ui->year == time.year && ui->month == time.month &&
         ui->day == time.day)) {
        return 0;
    }
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
    (void)time_state_apply_audio_ack();
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
        [PJ_UI_TIME_COMMAND_TIMER_SET] =
            PJ_TIME_CONTROLLER_COMMAND_TIMER_SET,
        [PJ_UI_TIME_COMMAND_INTERVAL_START] =
            PJ_TIME_CONTROLLER_COMMAND_INTERVAL_START,
        [PJ_UI_TIME_COMMAND_INTERVAL_PAUSE] =
            PJ_TIME_CONTROLLER_COMMAND_INTERVAL_PAUSE,
        [PJ_UI_TIME_COMMAND_INTERVAL_RESET] =
            PJ_TIME_CONTROLLER_COMMAND_INTERVAL_RESET,
        [PJ_UI_TIME_COMMAND_INTERVAL_SET] =
            PJ_TIME_CONTROLLER_COMMAND_INTERVAL_SET,
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
    uint32_t visual_before = pj_ui_visual_revision(ui);
    pj_time_controller_result_t result;
    uint32_t reset_generation = interval_reset_take_pending();
    if (reset_generation != 0U) {
        int interval_active_before = time_state_interval_active();
        (void)pj_ui_discard_pending_interval_command(ui);
        const pj_time_controller_command_t command = {
            .type = PJ_TIME_CONTROLLER_COMMAND_INTERVAL_RESET,
        };
        if (!pj_time_controller_apply(&g_time_controller, &command, &result)) {
            memset(&result, 0, sizeof(result));
        }
        (void)time_state_apply_audio_ack();
        (void)time_state_project(ui);
        interval_reset_complete(reset_generation, &result,
                                interval_active_before,
                                time_state_interval_active());
    } else {
        (void)pj_time_controller_update(&g_time_controller, &result);
        (void)time_state_apply_audio_ack();
        (void)time_state_project(ui);
    }
    return pj_ui_visual_revision(ui) != visual_before;
#else
    (void)ui;
    return 0;
#endif
}

int pj_board_display_framebuffer_ex(const pj_framebuffer_t *fb,
                                    const pj_ui_dirty_region_t *dirty,
                                    int defer_cleanup)
{
#ifdef ESP_PLATFORM
    if (fb == NULL || g_epd_spi == NULL) {
        if (!g_display_warning_logged) {
            ESP_LOGW(TAG, "Display refresh skipped: display not initialized");
            g_display_warning_logged = 1;
        }
        return 0;
    }
    if (!g_display_ready) {
        esp_err_t recovery_err = epd_controller_configure();
        if (recovery_err != ESP_OK) {
            if (!g_display_warning_logged) {
                ESP_LOGE(TAG, "Display recovery retry failed: %s",
                         esp_err_to_name(recovery_err));
                g_display_warning_logged = 1;
            }
            return 0;
        }
        g_display_ready = 1;
        g_display_warning_logged = 0;
        board_status_set_service(BOARD_SERVICE_DISPLAY,
                                 PJ_BOARD_SERVICE_READY);
        ESP_LOGI(TAG, "Display controller recovered; retrying with full base refresh");
    }

    int64_t refresh_started_us = esp_timer_get_time();
    g_epd_refresh_busy_us = 0;
    pj_display_refresh_set_cleanup_deferred(&g_epd_refresh_policy,
                                            defer_cleanup);
    pj_display_refresh_plan_t plan = pj_display_refresh_plan(
        &g_epd_refresh_policy, fb, &g_epd_shadow_fb, g_epd_shadow_valid, dirty);
    if (plan.kind == PJ_DISPLAY_REFRESH_NOOP) {
        (void)pj_display_refresh_complete(&g_epd_refresh_policy, &g_epd_shadow_fb,
                                          &g_epd_shadow_valid, fb, &plan,
                                          1, 0, 0);
        ESP_LOGI(TAG, "Display refresh no-op requests=%" PRIu64 " noops=%" PRIu64,
                 g_epd_refresh_policy.metrics.requests,
                 g_epd_refresh_policy.metrics.noops);
        return 1;
    }
    esp_err_t refresh_err;
    if (plan.kind == PJ_DISPLAY_REFRESH_PARTIAL) {
        refresh_err = epd_refresh_partial(fb, &plan);
    } else {
        ESP_LOGI(TAG, "Display full refresh%s",
                 plan.promoted_to_full ? " (partial cadence)" : "");
        refresh_err = epd_refresh_full(fb);
    }
    uint32_t latency_us = (uint32_t)(esp_timer_get_time() - refresh_started_us);
    uint32_t busy_time_us = g_epd_refresh_busy_us;
    if (refresh_err != ESP_OK) {
        (void)pj_display_refresh_complete(&g_epd_refresh_policy, &g_epd_shadow_fb,
                                          &g_epd_shadow_valid, fb, &plan,
                                          0, latency_us, busy_time_us);
        g_epd_partial_ready = 0;
        g_display_ready = 0;
        board_status_set_service(BOARD_SERVICE_DISPLAY,
                                 PJ_BOARD_SERVICE_ERROR);
        const char *refresh_kind =
            plan.kind == PJ_DISPLAY_REFRESH_PARTIAL ? "partial" : "full";
        board_status_set_error("display %s refresh failed: %s", refresh_kind,
                               esp_err_to_name(refresh_err));
        size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        size_t dma_largest = heap_caps_get_largest_free_block(
            MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        ESP_LOGE(TAG, "Display %s refresh failed: %s; dma_free=%zu "
                 "dma_largest=%zu latency_us=%" PRIu32
                 " busy_us=%" PRIu32 " errors=%" PRIu64,
                 refresh_kind, esp_err_to_name(refresh_err), dma_free,
                 dma_largest, latency_us,
                 busy_time_us, g_epd_refresh_policy.metrics.errors);

        esp_err_t recovery_err = epd_controller_configure();
        if (recovery_err == ESP_OK) {
            g_display_ready = 1;
            g_display_warning_logged = 0;
            board_status_set_service(BOARD_SERVICE_DISPLAY,
                                     PJ_BOARD_SERVICE_READY);
            ESP_LOGW(TAG, "Display controller recovered; UI frame remains dirty for full retry");
        } else {
            ESP_LOGE(TAG, "Display controller recovery failed: %s; retry deferred",
                     esp_err_to_name(recovery_err));
        }
        return 0;
    }

    (void)pj_display_refresh_complete(&g_epd_refresh_policy, &g_epd_shadow_fb,
                                      &g_epd_shadow_valid, fb, &plan,
                                      1, latency_us, busy_time_us);
    ESP_LOGI(TAG, "Display metrics full=%" PRIu64 " partial=%" PRIu64
             " noop=%" PRIu64 " errors=%" PRIu64 " changed=%" PRIu32
             " latency_us=%" PRIu32 " busy_us=%" PRIu32
             " cleanup_deferrals=%" PRIu64 " cleanup_pending=%d"
             " partial_since_full=%" PRIu32,
             g_epd_refresh_policy.metrics.applied_full,
             g_epd_refresh_policy.metrics.applied_partial,
             g_epd_refresh_policy.metrics.noops,
             g_epd_refresh_policy.metrics.errors,
             plan.changed_pixels, latency_us, busy_time_us,
             g_epd_refresh_policy.metrics.cleanup_deferrals,
             g_epd_refresh_policy.cleanup_pending,
             g_epd_refresh_policy.partial_since_full);
    return 1;
#else
    (void)fb;
    (void)dirty;
    (void)defer_cleanup;
    return 0;
#endif
}

int pj_board_display_framebuffer(const pj_framebuffer_t *fb,
                                 const pj_ui_dirty_region_t *dirty)
{
    return pj_board_display_framebuffer_ex(fb, dirty, 0);
}

int pj_board_display_cleanup_pending(void)
{
#ifdef ESP_PLATFORM
    return pj_display_refresh_cleanup_pending(&g_epd_refresh_policy);
#else
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
    event->captured_at_ms = 0;
#ifdef ESP_PLATFORM
    if (!g_aux_task_started) {
        esp_err_t aux_err = aux_task_start();
        if (aux_err != ESP_OK) {
            ESP_LOGW(TAG, "AUX poll task retry failed: %s", esp_err_to_name(aux_err));
        }
    }
    if (g_aux_event_queue != NULL && xQueueReceive(g_aux_event_queue, event, 0) == pdTRUE) {
        return 1;
    }
    if (g_board_event_queue != NULL && xQueueReceive(g_board_event_queue, event, 0) == pdTRUE) {
        return 1;
    }
#endif
    return 0;
}

int pj_board_aux_released(void)
{
#ifdef ESP_PLATFORM
    int released;
    portENTER_CRITICAL(&g_aux_state_lock);
    released = g_aux_released;
    portEXIT_CRITICAL(&g_aux_state_lock);
    return released;
#else
    return 1;
#endif
}

int pj_board_power_released(void)
{
#ifdef ESP_PLATFORM
    int released;
    portENTER_CRITICAL(&g_aux_state_lock);
    released = g_power_released;
    portEXIT_CRITICAL(&g_aux_state_lock);
    return released;
#else
    return 1;
#endif
}

int pj_board_enter_sleep(void)
{
#ifdef ESP_PLATFORM
    if (!storage_sleep_try_begin()) {
        ESP_LOGI(TAG, "Sleep deferred because storage work is active");
        return 0;
    }
    if (!time_transaction_take()) {
        storage_sleep_finish();
        return -1;
    }
    int rtc_schedule = rtc_wake_sync_locked();
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
        time_transaction_give();
        storage_sleep_finish();
        return 0;
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
            time_transaction_give();
            storage_sleep_finish();
            return -1;
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
        time_transaction_give();
        storage_sleep_finish();
        return 0;
    }
    if (g_display_ready) {
        gpio_set_level(EPD_PWR_PIN, 1);
        g_epd_shadow_valid = 0;
        g_epd_partial_ready = 0;
    }
    ESP_LOGI(TAG, "Entering light sleep; PWR, RTC_INT, or timer wake the device");
    esp_err_t sleep_err = gpio_wakeup_enable(PWR_BUTTON_PIN, GPIO_INTR_LOW_LEVEL);
    if (sleep_err == ESP_OK) {
        sleep_err = esp_sleep_enable_gpio_wakeup();
    }
    if (sleep_err == ESP_OK) {
        sleep_err = esp_light_sleep_start();
    }
    time_transaction_give();
    if (sleep_err != ESP_OK) {
        gpio_set_level(EPD_PWR_PIN, 0);
        ESP_LOGW(TAG, "Light sleep rejected: %s", esp_err_to_name(sleep_err));
        storage_sleep_finish();
        return -1;
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
    gpio_set_level(EPD_PWR_PIN, 0);
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    portENTER_CRITICAL(&g_aux_state_lock);
    pj_power_input_init(&g_power_input, gpio_get_level(PWR_BUTTON_PIN), now_ms);
    g_power_released = pj_power_input_is_released(&g_power_input);
    portEXIT_CRITICAL(&g_aux_state_lock);
    battery_refresh_status();
    shtc3_refresh_status();
    const char *source = rtc_wake ? "RTC_INT" : timer_wake ? "timer" : "GPIO";
    ESP_LOGI(TAG, "Woke from light sleep via %s (RTC flags=0x%02x)%s",
             source, rtc_flags,
             rtc_wake &&
             (rtc_flags & PJ_RTC_WAKE_CONTROL2_AF) == 0 ? " spurious" : "");
    storage_sleep_finish();
    return 1;
#else
    return 1;
#endif
}


int pj_board_record_set_active(int active)
{
    active = active != 0;
    if (!active) {
#ifdef ESP_PLATFORM
        if (!audio_lifecycle_take()) {
            return 0;
        }
        pj_audio_stop_result_t stop =
            pj_audio_lifecycle_request_record_stop(&g_audio_lifecycle);
        TaskHandle_t task = g_record_task;
        if (stop == PJ_AUDIO_STOP_SIGNAL_WORKER && task != NULL) {
            xTaskNotifyGive(task);
        }
        audio_lifecycle_give();
        if (stop == PJ_AUDIO_STOP_INACTIVE) {
            return 1;
        }
        if (stop == PJ_AUDIO_STOP_SIGNAL_WORKER) {
            recording_state_request_stop();
            ESP_LOGI(TAG, "Recording stop requested");
        }
#else
        g_status.recording = 0;
#endif
        return 1;
    }
    pj_board_status_t status = pj_board_status();
    if (status.storage != PJ_BOARD_SERVICE_READY ||
        status.audio != PJ_BOARD_SERVICE_READY) {
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG, "Record command ignored: storage/audio unavailable");
#endif
        return 0;
    }
#ifdef ESP_PLATFORM
    if (!audio_lifecycle_take()) {
        return 0;
    }
    if (g_audio_lifecycle.record_worker) {
        audio_lifecycle_give();
        return 1;
    }
    if (!pj_audio_lifecycle_begin_record(&g_audio_lifecycle) ||
        alert_audio_desired()) {
        pj_audio_lifecycle_finish_record(&g_audio_lifecycle);
        audio_lifecycle_give();
        ESP_LOGW(TAG, "Record start ignored: another audio operation is active");
        return 0;
    }
    if (!record_storage_try_acquire()) {
        pj_audio_lifecycle_finish_record(&g_audio_lifecycle);
        audio_lifecycle_give();
        ESP_LOGW(TAG, "Record start ignored: storage maintenance active");
        return 0;
    }
    if (!storage_preflight(PJ_STORAGE_RECORD_START_BYTES, "recording")) {
        pj_board_status_t failed = board_status_snapshot_base();
        ESP_LOGW(TAG, "Record start ignored: %s", failed.last_error);
        record_storage_release();
        pj_audio_lifecycle_finish_record(&g_audio_lifecycle);
        audio_lifecycle_give();
        return 0;
    }
    if (!next_recording_path(
            g_active_recording_path, sizeof(g_active_recording_path))) {
        board_status_set_error(
            "recording path allocation failed; run storage recovery");
        record_storage_release();
        pj_audio_lifecycle_finish_record(&g_audio_lifecycle);
        audio_lifecycle_give();
        return 0;
    }
    if (!recording_state_start()) {
        ESP_LOGE(TAG, "Record start failed: invalid lifecycle transition");
        board_status_set_error("recording lifecycle was busy");
        record_storage_release();
        pj_audio_lifecycle_finish_record(&g_audio_lifecycle);
        audio_lifecycle_give();
        return 0;
    }
    alert_audio_set_recording(1);
    board_audio_state_set(1, -1);
    BaseType_t created = xTaskCreate(record_task, "pj-record", PJ_AUDIO_RECORD_TASK_STACK, NULL, 5, &g_record_task);
    if (created != pdPASS) {
        board_audio_state_set(0, -1);
        alert_audio_set_recording(0);
        g_record_task = NULL;
        pj_audio_lifecycle_finish_record(&g_audio_lifecycle);
        board_status_set_error("recording task creation failed");
        recording_state_finish_capture(0);
        recording_publish_completion();
        record_storage_release();
        audio_lifecycle_give();
        ESP_LOGE(TAG, "Record start failed: task create failed");
        return 0;
    }
    ESP_LOGI(TAG, "Recording task queued: %s", g_active_recording_path);
    audio_lifecycle_give();
#else
    g_status.recording = 1;
#endif
    return 1;
}

int pj_board_record_toggle(void)
{
    return pj_board_record_set_active(!pj_board_status().recording);
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
        if (!audio_lifecycle_take()) {
            return 0;
        }
        pj_audio_stop_result_t stop =
            pj_audio_lifecycle_request_playback_stop(&g_audio_lifecycle);
        TaskHandle_t task = g_playback_task;
        if (stop == PJ_AUDIO_STOP_SIGNAL_WORKER && task != NULL) {
            xTaskNotifyGive(task);
        }
        audio_lifecycle_give();
        if (stop == PJ_AUDIO_STOP_INACTIVE) {
            return 1;
        }
        if (stop == PJ_AUDIO_STOP_SIGNAL_WORKER) {
            ESP_LOGI(TAG, "Playback stop requested");
        }
#else
        g_status.playback_active = 0;
#endif
        return 1;
    }
    pj_board_status_t status = pj_board_status();
    if (status.storage != PJ_BOARD_SERVICE_READY ||
        status.audio != PJ_BOARD_SERVICE_READY) {
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG, "Playback command ignored: storage/audio unavailable");
#endif
        return 0;
    }
#ifdef ESP_PLATFORM
    if (!audio_lifecycle_take()) {
        return 0;
    }
    if (g_audio_lifecycle.playback_worker) {
        audio_lifecycle_give();
        return 1;
    }
    if (!pj_audio_lifecycle_begin_playback(&g_audio_lifecycle) ||
        alert_audio_desired()) {
        pj_audio_lifecycle_finish_playback(&g_audio_lifecycle);
        audio_lifecycle_give();
        ESP_LOGW(TAG, "Playback start ignored: another audio operation is active");
        return 0;
    }
    if (!playback_storage_try_acquire()) {
        pj_audio_lifecycle_finish_playback(&g_audio_lifecycle);
        audio_lifecycle_give();
        ESP_LOGW(TAG, "Playback start ignored: storage maintenance active");
        return 0;
    }
    if (note_index < 0) {
        note_index = 0;
    }
    char filename[96];
    if (!audio_filename_for_index(note_index, filename, sizeof(filename))) {
        ESP_LOGW(TAG, "Playback start ignored: WAV index %d unavailable", note_index);
        playback_storage_release();
        pj_audio_lifecycle_finish_playback(&g_audio_lifecycle);
        audio_lifecycle_give();
        return 0;
    }
    (void)snprintf(g_active_playback_path, sizeof(g_active_playback_path), PJ_AUDIO_DIR "/%s", filename);
    board_audio_state_set(-1, 1);
    BaseType_t created = xTaskCreate(playback_task, "pj-play", PJ_AUDIO_PLAYBACK_TASK_STACK, NULL, 5, &g_playback_task);
    if (created != pdPASS) {
        board_audio_state_set(-1, 0);
        g_playback_task = NULL;
        pj_audio_lifecycle_finish_playback(&g_audio_lifecycle);
        playback_storage_release();
        audio_lifecycle_give();
        ESP_LOGE(TAG, "Playback start failed: task create failed");
        return 0;
    }
    ESP_LOGI(TAG, "Playback started: %s", g_active_playback_path);
    audio_lifecycle_give();
#else
    (void)note_index;
    g_status.playback_active = 1;
#endif
    return 1;
}

int pj_board_playback_toggle_index(int note_index)
{
    return pj_board_playback_set_active(!pj_board_status().playback_active,
                                        note_index);
}

int pj_board_wipe_recordings(pj_ui_context_t *ui)
{
#ifdef ESP_PLATFORM
    (void)ui;
    pj_wipe_status_t status;
    pj_wipe_start_result_t result =
        recording_wipe_start(NULL, &status, PJ_WIPE_WORKER_RELEASE_NOW);
    if (result == PJ_WIPE_START_STARTED || result == PJ_WIPE_START_ATTACHED) {
        return (int)status.id;
    }
    if (result == PJ_WIPE_START_AUDIO_ACTIVE) {
        return -2;
    }
    if (result == PJ_WIPE_START_STORAGE_BUSY) {
        return -3;
    }
    if (result == PJ_WIPE_START_TASK_FAILED) {
        return -4;
    }
    return -1;
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
    if (audio_lifecycle_active()) {
        board_status_set_error(
            "storage recovery blocked while audio is active");
        return 0;
    }
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    int maintenance_acquired = pj_storage_recovery_try_begin(&g_storage_coordinator);
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
    if (!maintenance_acquired) {
        board_status_set_error(
            "storage recovery blocked by active storage work");
        return 0;
    }
    pj_board_sync_inventory_mutation_t sync_mutation;
    if (!pj_board_companion_sync_inventory_mutation_begin(
            &sync_mutation, 0)) {
        board_status_set_error(
            "storage recovery blocked by unavailable Sync state");
        portENTER_CRITICAL(&g_storage_coordinator_lock);
        pj_storage_recovery_finish(&g_storage_coordinator);
        portEXIT_CRITICAL(&g_storage_coordinator_lock);
        return 0;
    }
    int sync_resume_needed = sync_mutation.start_pending;
    pj_board_status_t status = board_status_snapshot_base();
    if (g_sd_card != NULL) {
        esp_err_t unmount_err = esp_vfs_fat_sdcard_unmount(status.storage_path,
                                                          g_sd_card);
        if (unmount_err != ESP_OK) {
            (void)pj_board_companion_sync_inventory_mutation_finish(
                &sync_mutation);
            board_status_set_error("microSD unmount failed: %s",
                                   esp_err_to_name(unmount_err));
            portENTER_CRITICAL(&g_storage_coordinator_lock);
            pj_storage_recovery_finish(&g_storage_coordinator);
            portEXIT_CRITICAL(&g_storage_coordinator_lock);
            if (sync_resume_needed &&
                !pj_board_companion_sync_resume()) {
                ESP_LOGW(TAG,
                         "Unable to resume queued Sync after storage recovery failure");
            }
            return 0;
        }
        g_sd_card = NULL;
    }
    board_status_take();
    g_status.storage_mounted = 0;
    g_status.storage = PJ_BOARD_SERVICE_UNAVAILABLE;
    g_status.storage_health = PJ_STORAGE_HEALTH_UNMOUNTED;
    board_status_give();
    esp_err_t err = storage_init();
    if (!pj_board_companion_sync_inventory_mutation_finish(
            &sync_mutation)) {
        ESP_LOGE(TAG, "Storage recovery could not release the Sync barrier");
    }
    if (err != ESP_OK) {
        board_status_set_service(BOARD_SERVICE_STORAGE,
                                 PJ_BOARD_SERVICE_ERROR);
        board_status_set_error(
            "microSD recovery failed: %s; check card and filesystem",
            esp_err_to_name(err));
        portENTER_CRITICAL(&g_storage_coordinator_lock);
        pj_storage_recovery_finish(&g_storage_coordinator);
        portEXIT_CRITICAL(&g_storage_coordinator_lock);
        if (sync_resume_needed && !pj_board_companion_sync_resume()) {
            ESP_LOGW(TAG,
                     "Unable to resume queued Sync after storage recovery failure");
        }
        return 0;
    }
    board_status_set_service(BOARD_SERVICE_STORAGE, PJ_BOARD_SERVICE_READY);
    board_status_clear_error_if_owned("microSD ", "storage recovery ");
    if (!g_audio_ready) {
        esp_err_t audio_err = audio_init();
        if (audio_err == ESP_OK) {
            board_status_set_service(BOARD_SERVICE_AUDIO,
                                     PJ_BOARD_SERVICE_READY);
        } else {
            board_status_set_service(BOARD_SERVICE_AUDIO,
                                     PJ_BOARD_SERVICE_ERROR);
            board_status_set_error(
                "storage recovered but audio init failed: %s",
                esp_err_to_name(audio_err));
        }
    }
    portENTER_CRITICAL(&g_storage_coordinator_lock);
    pj_storage_recovery_finish(&g_storage_coordinator);
    portEXIT_CRITICAL(&g_storage_coordinator_lock);
    if (sync_resume_needed && !pj_board_companion_sync_resume()) {
        ESP_LOGW(TAG, "Unable to resume queued Sync after storage recovery");
    }
    board_update_publish(BOARD_UPDATE_NOTES);
    return 1;
#else
    return 0;
#endif
}

#ifdef ESP_PLATFORM
static int auth_ok(httpd_req_t *req)
{
    char header[96];
    char token[sizeof(g_status.token)] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", header,
                                    sizeof(header)) != ESP_OK ||
        !provisioned_token_read(token, sizeof(token))) {
        return 0;
    }
    const char *method = req->method == HTTP_GET ? "GET" :
                         req->method == HTTP_PUT ? "PUT" :
                         req->method == HTTP_DELETE ? "DELETE" : "OTHER";
    int valid = pj_auth_header_valid(header, token) ||
           pj_board_companion_sync_scoped_auth_valid(header, method,
                                                      req->uri, token);
    memset(token, 0, sizeof(token));
    return valid;
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t send_json_object(httpd_req_t *req, const cJSON *json)
{
    char *encoded = cJSON_PrintUnformatted(json);
    if (encoded == NULL) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"response encoding failed\"}");
    }
    esp_err_t result = send_json(req, encoded);
    cJSON_free(encoded);
    return result;
}

static esp_err_t require_auth(httpd_req_t *req)
{
    if (auth_ok(req)) {
        return ESP_OK;
    }
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer realm=\"pocket-journal\"");
    httpd_resp_set_hdr(req, "Connection", "close");
    (void)send_json(req, "{\"error\":\"unauthorized\"}");
    return ESP_ERR_INVALID_STATE;
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

static board_time_update_result_t board_set_time_date(
    int hour, int minute, int second, int *year, int year_provided,
    int month, int day, int update_utc_offset, int utc_offset_minutes,
    const char *source)
{
    board_time_update_result_t result = {
        .status = BOARD_TIME_UPDATE_INVALID,
    };
    if (year == NULL || source == NULL) {
        return result;
    }
    if (!time_transaction_take()) {
        result.status = BOARD_TIME_UPDATE_UNAVAILABLE;
        return result;
    }
    if (!year_provided) {
        board_time_snapshot_t current = board_time_snapshot();
        if (!current.time_set || current.year < 2024 || current.year > 2099) {
            time_transaction_give();
            result.status = BOARD_TIME_UPDATE_YEAR_REQUIRED;
            return result;
        }
        *year = current.year;
    }
    if (!valid_time_date(hour, minute, *year, month, day) ||
        second < 0 || second > 59 ||
        (update_utc_offset &&
         !pj_time_utc_offset_valid(utc_offset_minutes))) {
        ESP_LOGE(TAG, "Rejected invalid time/date from %s", source);
        time_transaction_give();
        return result;
    }

    pj_time_clock_anchor_t target_anchor;
    if (!pj_time_clock_anchor_set(&target_anchor, *year, month, day, hour,
                                  minute, second, 0)) {
        ESP_LOGE(TAG, "Rejected invalid time/date from %s", source);
        time_transaction_give();
        return result;
    }

    board_time_commit_result_t commit = board_time_commit_locked(
        hour, minute, *year, month, day, second, &target_anchor, 1,
        update_utc_offset, utc_offset_minutes, 1);
    result.partial_components = commit.transaction.partial_components;
    switch (commit.transaction.status) {
    case PJ_TIME_TRANSACTION_OK:
        result.status = BOARD_TIME_UPDATE_OK;
        ESP_LOGI(TAG,
                 "Time/date updated from %s: %02d:%02d:%02d %04d-%02d-%02d",
                 source, hour, minute, second, *year, month, day);
        break;
    case PJ_TIME_TRANSACTION_RTC_SNAPSHOT_FAILED:
        result.status = BOARD_TIME_UPDATE_RTC_SNAPSHOT_FAILED;
        board_status_set_error("RTC snapshot failed for %s time update: %s",
                               source,
                               esp_err_to_name(commit.rtc_snapshot_error));
        ESP_LOGW(TAG, "RTC snapshot failed for %s time update: %s", source,
                 esp_err_to_name(commit.rtc_snapshot_error));
        break;
    case PJ_TIME_TRANSACTION_OFFSET_FAILED_ROLLED_BACK:
        result.status = BOARD_TIME_UPDATE_OFFSET_PERSIST_FAILED;
        board_status_set_error(
            "UTC offset write failed for %s and was rolled back: %s", source,
            esp_err_to_name(commit.offset_store_error));
        ESP_LOGW(TAG, "UTC offset write failed for %s and was rolled back: %s",
                 source, esp_err_to_name(commit.offset_store_error));
        break;
    case PJ_TIME_TRANSACTION_RTC_FAILED_ROLLED_BACK:
        result.status = BOARD_TIME_UPDATE_RTC_WRITE_FAILED;
        board_status_set_error(
            "RTC write failed for %s and was rolled back: %s", source,
            esp_err_to_name(commit.rtc_write_error));
        ESP_LOGW(TAG, "RTC write failed for %s and was rolled back: %s", source,
                 esp_err_to_name(commit.rtc_write_error));
        break;
    case PJ_TIME_TRANSACTION_PARTIAL_COMMIT:
        result.status = BOARD_TIME_UPDATE_PARTIAL_COMMIT;
        board_status_set_error(
            "%s time rollback failed; partial components=0x%" PRIx32,
            source, result.partial_components);
        ESP_LOGE(TAG,
                 "%s time rollback failed; partial components=0x%" PRIx32
                 " rtc_restore=%s offset_restore=%s",
                 source, result.partial_components,
                 esp_err_to_name(commit.rtc_restore_error),
                 esp_err_to_name(commit.offset_restore_error));
        break;
    case PJ_TIME_TRANSACTION_INVALID:
    default:
        result.status = BOARD_TIME_UPDATE_INVALID;
        break;
    }
    time_transaction_give();
    return result;
}

typedef struct {
    const char *code;
    const char *error;
    int retryable;
    int partial_commit;
} board_time_update_error_fields_t;

static board_time_update_error_fields_t board_time_update_error_fields(
    const board_time_update_result_t *result)
{
    switch (result->status) {
    case BOARD_TIME_UPDATE_INVALID:
        return (board_time_update_error_fields_t) {
            "invalid_time", "invalid civil time or UTC offset", 0, 0};
    case BOARD_TIME_UPDATE_YEAR_REQUIRED:
        return (board_time_update_error_fields_t) {
            "time_year_required",
            "year is required until device time is initialized", 0, 0};
    case BOARD_TIME_UPDATE_RTC_SNAPSHOT_FAILED:
        return (board_time_update_error_fields_t) {
            "time_rtc_snapshot_failed", "RTC snapshot failed", 1, 0};
    case BOARD_TIME_UPDATE_OFFSET_PERSIST_FAILED:
        return (board_time_update_error_fields_t) {
            "time_offset_persist_failed",
            "UTC offset persistence failed and was rolled back", 1, 0};
    case BOARD_TIME_UPDATE_RTC_WRITE_FAILED:
        return (board_time_update_error_fields_t) {
            "time_rtc_write_failed", "RTC write failed and was rolled back",
            1, 0};
    case BOARD_TIME_UPDATE_PARTIAL_COMMIT:
        return (board_time_update_error_fields_t) {
            "time_partial_commit", "time rollback failed", 0, 1};
    case BOARD_TIME_UPDATE_UNAVAILABLE:
    default:
        return (board_time_update_error_fields_t) {
            "time_unavailable", "time transaction unavailable", 1, 0};
    }
}

static void serial_print_time_update_error(
    const board_time_update_result_t *result)
{
    board_time_update_error_fields_t fields =
        board_time_update_error_fields(result);
    int rtc_failed = (result->partial_components &
                      PJ_TIME_TRANSACTION_COMPONENT_RTC) != 0;
    int offset_failed = (result->partial_components &
                         PJ_TIME_TRANSACTION_COMPONENT_UTC_OFFSET) != 0;
    printf("PJ_ERR {\"error\":\"%s\",\"code\":\"%s\","
           "\"retryable\":%s,\"partial_commit\":%s,"
           "\"partial_components\":%" PRIu32 ","
           "\"rtc_rollback_failed\":%s,"
           "\"utc_offset_rollback_failed\":%s}\n",
           fields.error, fields.code, fields.retryable ? "true" : "false",
           fields.partial_commit ? "true" : "false",
           result->partial_components, rtc_failed ? "true" : "false",
           offset_failed ? "true" : "false");
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
    if (wifi_state->rssi_known) {
        cJSON_AddNumberToObject(wifi, "rssi_dbm", wifi_state->rssi_dbm);
    } else {
        cJSON_AddNullToObject(wifi, "rssi_dbm");
    }
    if (wifi_state->channel_known) {
        cJSON_AddNumberToObject(wifi, "channel", wifi_state->channel);
    } else {
        cJSON_AddNullToObject(wifi, "channel");
    }
    static const char *const auth_modes[] = {
        "open", "wep", "wpa_psk", "wpa2_psk", "wpa_wpa2_psk",
        "wpa2_enterprise", "wpa3_psk", "wpa2_wpa3_psk", "wapi_psk",
        "owe", "wpa3_enterprise_192", "reserved_11", "reserved_12",
        "dpp", "wpa3_enterprise", "wpa2_wpa3_enterprise",
        "wpa_enterprise",
    };
    if (wifi_state->auth_mode_known &&
        wifi_state->auth_mode < sizeof(auth_modes) / sizeof(auth_modes[0])) {
        cJSON_AddStringToObject(wifi, "auth_mode",
                                auth_modes[wifi_state->auth_mode]);
    } else {
        cJSON_AddNullToObject(wifi, "auth_mode");
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
    cJSON_AddStringToObject(
        time_sync, "civil_time_semantics",
        sync_state->utc_offset_known ? "fixed_utc_offset" : "unconfigured");
    if (sync_state->utc_offset_known) {
        cJSON_AddNumberToObject(time_sync, "utc_offset_minutes",
                                sync_state->utc_offset_minutes);
    } else {
        cJSON_AddNullToObject(time_sync, "utc_offset_minutes");
    }
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

static void wipe_status_fill_json(cJSON *wipe, const pj_wipe_status_t *status)
{
    cJSON_AddNumberToObject(wipe, "id", status->id);
    cJSON_AddStringToObject(wipe, "state", pj_wipe_state_name(status->state));
    cJSON_AddNumberToObject(wipe, "audio_deleted", (double)status->audio_deleted);
    cJSON_AddNumberToObject(wipe, "transcripts_deleted", (double)status->transcripts_deleted);
    cJSON_AddNumberToObject(wipe, "notes_deleted", (double)status->notes_deleted);
    if (status->code == PJ_WIPE_CODE_NONE) {
        cJSON_AddNullToObject(wipe, "code");
    } else {
        cJSON_AddStringToObject(wipe, "code", pj_wipe_code_name(status->code));
    }
    cJSON_AddBoolToObject(wipe, "retryable", status->retryable != 0);
}

static int runtime_identity_add_json(cJSON *parent)
{
    pj_reset_reason_t reason = runtime_reset_reason();
    return parent != NULL &&
           cJSON_AddNumberToObject(parent, "boot_id", (double)g_runtime_boot_id) != NULL &&
           cJSON_AddNumberToObject(parent, "uptime_ms", (double)board_monotonic_ms()) != NULL &&
           cJSON_AddStringToObject(parent, "reset_reason",
                                   pj_reset_reason_name(reason)) != NULL &&
           cJSON_AddNumberToObject(parent, "reset_reason_code", (double)reason) != NULL;
}

static int wipe_status_add_json(cJSON *parent, const pj_wipe_status_t *status)
{
    cJSON *wipe = cJSON_AddObjectToObject(parent, "recording_wipe");
    if (wipe == NULL) {
        return 0;
    }
    wipe_status_fill_json(wipe, status);
    return 1;
}

static int wipe_history_add_json(cJSON *parent,
                                 const pj_wipe_status_t *statuses,
                                 size_t count)
{
    cJSON *history = cJSON_AddArrayToObject(parent, "recording_wipe_recent");
    if (history == NULL) {
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        cJSON *wipe = cJSON_CreateObject();
        if (wipe == NULL) {
            return 0;
        }
        wipe_status_fill_json(wipe, &statuses[i]);
        cJSON_AddItemToArray(history, wipe);
    }
    return 1;
}

typedef enum {
    PJ_SETTINGS_COMMIT_OK = 0,
    PJ_SETTINGS_COMMIT_INVALID,
    PJ_SETTINGS_COMMIT_BUSY,
    PJ_SETTINGS_COMMIT_CONFLICT,
    PJ_SETTINGS_COMMIT_STORE_FAILED,
} pj_settings_commit_result_t;

static int settings_parse_update(const cJSON *json, pj_settings_t *settings,
                                 uint32_t *expected_generation,
                                 int *has_expected_generation);
static pj_settings_commit_result_t settings_commit_update(
    const cJSON *json, int require_expected_generation,
    uint32_t forced_expected_generation, int use_forced_expected_generation,
    pj_settings_t *committed, uint32_t *generation, int *changed);

static int settings_snapshot(pj_settings_t *settings, uint32_t *generation)
{
    if (settings == NULL || generation == NULL ||
        !settings_take(portMAX_DELAY)) {
        return 0;
    }
    *settings = g_settings;
    *generation = g_settings_store.generation;
    settings_give();
    return 1;
}

static int settings_add_json(cJSON *json, const pj_settings_t *settings,
                             uint32_t generation)
{
    if (json == NULL || settings == NULL) {
        return 0;
    }
    int pending_sync = 0;
    int transferred_sync = 0;
    collect_sync_counts(&pending_sync, &transferred_sync);
    return cJSON_AddStringToObject(json, "theme",
                                   settings->dark_mode ? "dark" : "light") != NULL &&
           cJSON_AddNumberToObject(json, "volume", settings->volume) != NULL &&
           cJSON_AddBoolToObject(json, "alarm_enabled",
                                 settings->alarm_enabled != 0) != NULL &&
           cJSON_AddNumberToObject(json, "alarm_hour", settings->alarm_hour) != NULL &&
           cJSON_AddNumberToObject(json, "alarm_minute", settings->alarm_minute) != NULL &&
           cJSON_AddNumberToObject(json, "timer_seconds", settings->timer_seconds) != NULL &&
           cJSON_AddNumberToObject(json, "interval_seconds",
                                   settings->interval_seconds) != NULL &&
           cJSON_AddBoolToObject(json, "clock_24h", settings->clock_24h != 0) != NULL &&
           cJSON_AddStringToObject(json, "temperature_unit",
                                   settings->temperature_fahrenheit ? "f" : "c") != NULL &&
           cJSON_AddNumberToObject(json, "transcript_font_size",
                                   settings->transcript_font_size) != NULL &&
           cJSON_AddNumberToObject(json, "sync_pending", pending_sync) != NULL &&
           cJSON_AddNumberToObject(json, "sync_transferred", transferred_sync) != NULL &&
           cJSON_AddNumberToObject(json, "generation", generation) != NULL;
}

static int capabilities_add_json(cJSON *json, int include_lan_ota)
{
    cJSON *capabilities = json == NULL ? NULL :
        cJSON_AddObjectToObject(json, "capabilities");
    int valid = capabilities != NULL &&
                cJSON_AddBoolToObject(capabilities, "status", 1) != NULL &&
                cJSON_AddBoolToObject(capabilities, "time.write", 1) != NULL &&
                cJSON_AddBoolToObject(capabilities, "settings.read", 1) != NULL &&
                cJSON_AddBoolToObject(capabilities, "settings.write", 1) != NULL &&
                cJSON_AddBoolToObject(capabilities, "recordings.list", 1) != NULL &&
                cJSON_AddBoolToObject(capabilities, "recordings.download", 1) != NULL &&
                cJSON_AddBoolToObject(capabilities, "recordings.delete", 1) != NULL &&
                cJSON_AddBoolToObject(capabilities, "transcripts.write", 1) != NULL &&
                cJSON_AddBoolToObject(capabilities, "audio.sync", 1) != NULL &&
                cJSON_AddBoolToObject(capabilities, "wifi.provision", 1) != NULL;
    if (valid && include_lan_ota) {
        valid = cJSON_AddBoolToObject(capabilities, "ota.read", 1) != NULL &&
                cJSON_AddBoolToObject(capabilities, "ota.write",
                                      pj_ota_write_enabled()) != NULL;
    }
    return valid;
}

static int serial_request_matches(const char *line, const char *command,
                                  const char **request_id)
{
    size_t command_length = strlen(command);
    *request_id = NULL;
    if (strcmp(line, command) == 0) {
        return 1;
    }
    if (strncmp(line, command, command_length) != 0 ||
        strncmp(line + command_length, " request_id=", 12) != 0) {
        return 0;
    }
    const char *value = line + command_length + 12;
    size_t length = strlen(value);
    if (length == 0U || length > 32U) {
        return 0;
    }
    for (size_t i = 0; i < length; i++) {
        unsigned char ch = (unsigned char)value[i];
        if (!isalnum(ch) && ch != '-' && ch != '_' && ch != '.') {
            return 0;
        }
    }
    *request_id = value;
    return 1;
}

static void serial_print_json(const char *prefix, cJSON *json)
{
    char *encoded = cJSON_PrintUnformatted(json);
    if (encoded == NULL) {
        printf("PJ_ERR {\"error\":\"status encoding failed\"}\n");
        return;
    }
    printf("%s %s\n", prefix, encoded);
    cJSON_free(encoded);
}

static int companion_sync_add_json(cJSON *json,
                                   const pj_companion_sync_state_t *sync)
{
    if (json == NULL || sync == NULL) {
        return 0;
    }
    return cJSON_AddBoolToObject(
               json, "request_pending",
               pj_companion_sync_state_pending(sync)) != NULL &&
           cJSON_AddNumberToObject(json, "requested_generation",
                                   sync->requested_generation) != NULL &&
           cJSON_AddNumberToObject(json, "acknowledged_generation",
                                   sync->acknowledged_generation) != NULL &&
           cJSON_AddNumberToObject(json, "active_generation",
                                   sync->active_generation) != NULL &&
           cJSON_AddNumberToObject(json, "requested_ms",
                                   (double)sync->requested_ms) != NULL &&
           cJSON_AddNumberToObject(json, "active_requested_ms",
                                   (double)sync->active_requested_ms) != NULL &&
           cJSON_AddNumberToObject(
               json, "claim_generation",
               pj_companion_sync_state_claim_generation(sync)) != NULL &&
           cJSON_AddNumberToObject(
               json, "claim_requested_ms",
               (double)(sync->active_generation != 0U ?
                            sync->active_requested_ms : sync->requested_ms)) != NULL &&
           cJSON_AddStringToObject(json, "state",
                                   pj_companion_sync_phase_name(sync->phase)) != NULL &&
           cJSON_AddStringToObject(
               json, "transport",
               pj_companion_sync_transport_name(sync->transport)) != NULL &&
           cJSON_AddStringToObject(json, "operation_id",
                                   sync->operation_id) != NULL &&
           cJSON_AddNumberToObject(json, "total", sync->total) != NULL &&
           cJSON_AddNumberToObject(json, "pending", sync->pending) != NULL &&
           cJSON_AddNumberToObject(json, "transferred",
                                   sync->transferred) != NULL &&
           cJSON_AddNumberToObject(json, "failed", sync->failed) != NULL &&
           cJSON_AddBoolToObject(json, "online", sync->online != 0) != NULL &&
           cJSON_AddStringToObject(json, "error", sync->error) != NULL;
}

static void serial_print_status(const char *request_id)
{
    int pending_sync = 0;
    int transferred_sync = 0;
    pj_board_status_t status = pj_board_status();
    collect_sync_counts_nonblocking(
        &pending_sync, &transferred_sync,
        status.storage == PJ_BOARD_SERVICE_READY);
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        printf("PJ_ERR {\"error\":\"status allocation failed\"}\n");
        return;
    }
    cJSON_AddStringToObject(json, "command", "PJ_STATUS");
    if (request_id != NULL) {
        cJSON_AddStringToObject(json, "request_id", request_id);
    }
    cJSON_AddStringToObject(json, "device_id", status.device_id);
    cJSON_AddStringToObject(json, "firmware_version",
                            esp_app_get_description()->version);
    cJSON_AddStringToObject(json, "storage", service_name(status.storage));
    cJSON_AddStringToObject(json, "audio", service_name(status.audio));
    cJSON_AddBoolToObject(json, "time_set", status.time_set != 0);
    cJSON_AddBoolToObject(json, "wifi_provisioned",
                          status.wifi_diagnostics.provisioned != 0);
    cJSON_AddStringToObject(json, "wifi", service_name(status.wifi));
    cJSON_AddStringToObject(json, "ip", status.ip_addr);
    cJSON_AddNumberToObject(json, "pending_sync", pending_sync);
    cJSON_AddNumberToObject(json, "transferred_sync", transferred_sync);
    pj_companion_sync_state_t companion_sync;
    cJSON *companion_json = cJSON_AddObjectToObject(json, "companion_sync");
    int companion_ok = pj_board_companion_sync_snapshot(&companion_sync) &&
                       companion_sync_add_json(companion_json,
                                               &companion_sync);
    storage_wipe_snapshot_t wipe = storage_wipe_snapshot();
    if (!companion_ok || !capabilities_add_json(json, 0) ||
        !runtime_identity_add_json(json) ||
        !wipe_status_add_json(json, &wipe.current) ||
        !wipe_history_add_json(json, wipe.history, wipe.history_count) ||
        !connectivity_add_json(json, &status)) {
        cJSON_Delete(json);
        printf("PJ_ERR {\"error\":\"status allocation failed\"}\n");
        return;
    }
    serial_print_json("PJ_OK", json);
    cJSON_Delete(json);
}

static void serial_stack_margin_check(const char *command)
{
    static UBaseType_t warned_margin = UINT_MAX;
    UBaseType_t minimum_free = uxTaskGetStackHighWaterMark(NULL);
    if (minimum_free < PJ_SERIAL_MIN_FREE_STACK_BYTES &&
        minimum_free < warned_margin) {
        warned_margin = minimum_free;
        ESP_LOGW(TAG,
                 "USB command stack margin low: command=%s minimum_free=%u "
                 "configured=%u",
                 command, (unsigned)minimum_free,
                 (unsigned)PJ_SERIAL_COMMAND_TASK_STACK);
    }
}

static void serial_print_wipe_start(pj_wipe_start_result_t result,
                                    const pj_wipe_status_t *status,
                                    const char *request_id)
{
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        printf("PJ_ERR {\"error\":\"wipe response allocation failed\"}\n");
        return;
    }
    cJSON_AddStringToObject(json, "command", "PJ_WIPE_RECORDINGS");
    if (request_id != NULL) {
        cJSON_AddStringToObject(json, "request_id", request_id);
    }
    if (!runtime_identity_add_json(json)) {
        cJSON_Delete(json);
        printf("PJ_ERR {\"error\":\"wipe response allocation failed\"}\n");
        return;
    }
    const char *prefix = "PJ_ERR";
    if (result == PJ_WIPE_START_STARTED || result == PJ_WIPE_START_ATTACHED) {
        prefix = "PJ_OK";
        cJSON_AddBoolToObject(json, "accepted", 1);
        cJSON_AddBoolToObject(json, "attached", result == PJ_WIPE_START_ATTACHED);
        (void)wipe_status_add_json(json, status);
    } else {
        const char *error = "storage unavailable";
        const char *code = "storage_unavailable";
        int retryable = 1;
        if (result == PJ_WIPE_START_AUDIO_ACTIVE) {
            error = "audio task active";
            code = "audio_active";
        } else if (result == PJ_WIPE_START_STORAGE_BUSY) {
            error = "storage maintenance busy";
            code = "storage_busy";
        } else if (result == PJ_WIPE_START_TASK_FAILED) {
            error = "recording wipe task start failed";
            code = "wipe_task_start_failed";
        }
        cJSON_AddStringToObject(json, "error", error);
        cJSON_AddStringToObject(json, "code", code);
        cJSON_AddBoolToObject(json, "retryable", retryable);
        if (status->id != 0U) {
            (void)wipe_status_add_json(json, status);
        }
    }
    serial_print_json(prefix, json);
    cJSON_Delete(json);
}

static int serial_drain_stdout(void)
{
    if (fflush(stdout) != 0) {
        return errno != 0 ? errno : EIO;
    }
    if (fsync(fileno(stdout)) != 0) {
        return errno != 0 ? errno : EIO;
    }
    return 0;
}

static void serial_print_interval_reset(const char *request_id)
{
    uint32_t generation = interval_reset_request();
    interval_reset_state_t result = {0};
    int completed = interval_reset_wait(generation, &result);
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        printf("PJ_ERR {\"error\":\"interval reset response allocation failed\"}\n");
        return;
    }
    cJSON_AddStringToObject(json, "command", "PJ_INTERVAL_RESET");
    if (request_id != NULL) {
        cJSON_AddStringToObject(json, "request_id", request_id);
    }
    if (completed) {
        cJSON_AddBoolToObject(json, "interval_active_before",
                              result.interval_active_before);
        cJSON_AddBoolToObject(json, "interval_active_after",
                              result.interval_active_after);
    }
    cJSON_AddBoolToObject(json, "silence_requested", 1);
    cJSON_AddBoolToObject(json, "silenced",
                          completed && result.audio_silenced &&
                          !result.interval_active_after);
    if (!completed) {
        cJSON_AddStringToObject(json, "error", "interval reset timed out");
        cJSON_AddStringToObject(json, "code", "interval_reset_timeout");
        cJSON_AddBoolToObject(json, "retryable", 1);
        serial_print_json("PJ_ERR", json);
    } else if (!result.persistence_ok) {
        cJSON_AddStringToObject(json, "error", "interval reset could not be persisted");
        cJSON_AddStringToObject(json, "code", "interval_reset_persist_failed");
        cJSON_AddBoolToObject(json, "retryable", 1);
        serial_print_json("PJ_ERR", json);
    } else {
        cJSON_AddBoolToObject(json, "reset", 1);
        cJSON_AddBoolToObject(json, "state_changed", result.state_changed);
        cJSON_AddBoolToObject(json, "persisted", 1);
        serial_print_json("PJ_OK", json);
    }
    cJSON_Delete(json);
}

static int file_sha256_hex_unlocked(const char *path, char out[65]);
static int audio_sha256_hex_unlocked(const char *filename, char out[65]);
static int audio_sha256_hex(const char *filename, char out[65]);

static int transcript_source_check_audio_unlocked(
    const char *body, size_t body_size, const pj_audio_entry_t *entry,
    pj_transcript_source_result_t *result)
{
    char current_sha256[65];
    if (body == NULL || entry == NULL || result == NULL ||
        entry->size_bytes < 0 ||
        !audio_sha256_hex_unlocked(entry->filename, current_sha256)) {
        return 0;
    }
    *result = pj_transcript_source_check(
        body, body_size, current_sha256, (uint64_t)entry->size_bytes);
    return 1;
}

static int serial_command_prefix(const char *line, const char *command)
{
    size_t size = strlen(command);
    return strncmp(line, command, size) == 0 &&
           (line[size] == '\0' || line[size] == ' ');
}

static void serial_sync_error(const char *command, const char *request_id,
                              const char *message, const char *code,
                              int retryable)
{
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        printf("PJ_ERR {\"error\":\"USB response allocation failed\"}\n");
        return;
    }
    cJSON_AddStringToObject(json, "command", command);
    if (request_id != NULL && pj_usb_sync_request_id_valid(request_id)) {
        cJSON_AddStringToObject(json, "request_id", request_id);
    }
    cJSON_AddStringToObject(json, "error", message);
    cJSON_AddStringToObject(json, "code", code);
    cJSON_AddBoolToObject(json, "retryable", retryable != 0);
    serial_print_json("PJ_ERR", json);
    cJSON_Delete(json);
}

static cJSON *serial_sync_response(const char *command, const char *request_id)
{
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(json, "command", command);
    if (request_id != NULL && pj_usb_sync_request_id_valid(request_id)) {
        cJSON_AddStringToObject(json, "request_id", request_id);
    }
    return json;
}

static int serial_settings_args_exact(const pj_usb_sync_args_t *args,
                                      const char *const *allowed,
                                      size_t allowed_count);

static void serial_companion_response(
    const char *command, const char *request_id,
    const pj_companion_sync_state_t *sync, const char *claim_result,
    int replayed)
{
    cJSON *json = serial_sync_response(command, request_id);
    pj_board_status_t status = pj_board_status();
    if (json == NULL ||
        cJSON_AddStringToObject(json, "device_id", status.device_id) == NULL ||
        !companion_sync_add_json(json, sync) ||
        (claim_result != NULL &&
         cJSON_AddStringToObject(json, "claim_result", claim_result) == NULL) ||
        cJSON_AddBoolToObject(json, "replayed", replayed != 0) == NULL) {
        cJSON_Delete(json);
        serial_sync_error(command, request_id,
                          "sync state response allocation failed",
                          "out_of_memory", 1);
        return;
    }
    serial_print_json("PJ_OK", json);
    cJSON_Delete(json);
}

static void serial_companion_status(char *line)
{
    const char *command = "PJ_SYNC_STATUS";
    const char *const allowed[] = {"request_id"};
    pj_usb_sync_args_t args;
    if (!pj_usb_sync_parse_args(line, command, &args) ||
        !serial_settings_args_exact(&args, allowed,
                                    sizeof(allowed) / sizeof(allowed[0]))) {
        serial_sync_error(command, NULL, "invalid sync status arguments",
                          "invalid_arguments", 0);
        return;
    }
    const char *request_id = pj_usb_sync_arg(&args, "request_id");
    pj_companion_sync_state_t sync;
    if (!pj_usb_sync_request_id_valid(request_id)) {
        serial_sync_error(command, request_id, "invalid sync request id",
                          "invalid_arguments", 0);
    } else if (!pj_board_companion_sync_snapshot(&sync)) {
        serial_sync_error(command, request_id, "sync state unavailable",
                          "sync_state_unavailable", 1);
    } else {
        serial_companion_response(command, request_id, &sync, NULL, 0);
    }
}

static void serial_companion_claim(char *line)
{
    const char *command = "PJ_SYNC_CLAIM";
    const char *const allowed[] = {
        "request_id", "generation", "operation_id",
    };
    pj_usb_sync_args_t args;
    if (!pj_usb_sync_parse_args(line, command, &args) ||
        !serial_settings_args_exact(&args, allowed,
                                    sizeof(allowed) / sizeof(allowed[0]))) {
        serial_sync_error(command, NULL, "invalid sync claim arguments",
                          "invalid_arguments", 0);
        return;
    }
    const char *request_id = pj_usb_sync_arg(&args, "request_id");
    const char *operation_id = pj_usb_sync_arg(&args, "operation_id");
    uint32_t generation = 0U;
    if (!pj_usb_sync_request_id_valid(request_id) || operation_id == NULL ||
        operation_id[0] == '\0' ||
        strlen(operation_id) >= PJ_COMPANION_SYNC_OPERATION_ID_BYTES ||
        !pj_usb_sync_parse_u32(pj_usb_sync_arg(&args, "generation"),
                               &generation) || generation == 0U) {
        serial_sync_error(command, request_id, "invalid sync claim arguments",
                          "invalid_arguments", 0);
        return;
    }
    pj_companion_sync_state_t sync;
    int result = pj_board_companion_sync_usb_claim(generation, operation_id,
                                                    &sync);
    if (result < 0) {
        serial_sync_error(command, request_id, "sync claim could not be saved",
                          "sync_store_failed", 1);
        return;
    }
    const char *result_name = "stale";
    if (result == PJ_COMPANION_SYNC_CLAIM_STARTED) {
        result_name = "started";
    } else if (result == PJ_COMPANION_SYNC_CLAIM_ATTACHED) {
        result_name = "attached";
    } else if (result == PJ_COMPANION_SYNC_CLAIM_BUSY) {
        result_name = "busy";
    }
    serial_companion_response(command, request_id, &sync, result_name, 0);
}

static int serial_companion_counts(const pj_usb_sync_args_t *args,
                                   uint32_t *generation, int *total, int *pending,
                                   int *transferred, int *failed)
{
    uint32_t total_value = 0U;
    uint32_t pending_value = 0U;
    uint32_t transferred_value = 0U;
    uint32_t failed_value = 0U;
    return pj_usb_sync_parse_u32(pj_usb_sync_arg(args, "generation"),
                                 generation) && *generation != 0U &&
           pj_usb_sync_parse_u32(pj_usb_sync_arg(args, "pending"),
                                 &pending_value) &&
           pj_usb_sync_parse_u32(pj_usb_sync_arg(args, "total"),
                                 &total_value) &&
           pj_usb_sync_parse_u32(pj_usb_sync_arg(args, "transferred"),
                                 &transferred_value) &&
           pj_usb_sync_parse_u32(pj_usb_sync_arg(args, "failed"),
                                 &failed_value) &&
           total_value <= (uint32_t)INT_MAX &&
           pending_value <= (uint32_t)INT_MAX &&
           transferred_value <= (uint32_t)INT_MAX &&
           failed_value <= (uint32_t)INT_MAX &&
           ((*total = (int)total_value),
            (*pending = (int)pending_value),
            (*transferred = (int)transferred_value),
            (*failed = (int)failed_value), 1);
}

static void serial_companion_progress(char *line, const char *command,
                                      const char *phase, int include_error)
{
    const char *const progress_allowed[] = {
        "request_id", "generation", "operation_id", "total", "pending",
        "transferred", "failed",
    };
    const char *const failure_allowed[] = {
        "request_id", "generation", "operation_id", "total", "pending",
        "transferred", "failed", "error_hex",
    };
    const char *const *allowed = include_error ? failure_allowed :
                                 progress_allowed;
    size_t allowed_count = include_error ?
        sizeof(failure_allowed) / sizeof(failure_allowed[0]) :
        sizeof(progress_allowed) / sizeof(progress_allowed[0]);
    pj_usb_sync_args_t args;
    if (!pj_usb_sync_parse_args(line, command, &args) ||
        !serial_settings_args_exact(&args, allowed, allowed_count)) {
        serial_sync_error(command, NULL, "invalid sync progress arguments",
                          "invalid_arguments", 0);
        return;
    }
    const char *request_id = pj_usb_sync_arg(&args, "request_id");
    const char *operation_id = pj_usb_sync_arg(&args, "operation_id");
    uint32_t generation = 0U;
    int total = 0;
    int pending = 0;
    int transferred = 0;
    int failed = 0;
    char error[PJ_COMPANION_SYNC_ERROR_BYTES] = {0};
    if (!pj_usb_sync_request_id_valid(request_id) || operation_id == NULL ||
        operation_id[0] == '\0' ||
        strlen(operation_id) >= PJ_COMPANION_SYNC_OPERATION_ID_BYTES ||
        !serial_companion_counts(&args, &generation, &total, &pending,
                                 &transferred, &failed)) {
        serial_sync_error(command, request_id,
                          "invalid sync progress arguments",
                          "invalid_arguments", 0);
        return;
    }
    if (include_error) {
        size_t decoded = 0U;
        const char *encoded = pj_usb_sync_arg(&args, "error_hex");
        if (encoded == NULL ||
            !pj_usb_sync_hex_decode(encoded, (uint8_t *)error,
                                    sizeof(error) - 1U, &decoded) ||
            memchr(error, '\0', decoded) != NULL) {
            serial_sync_error(command, request_id, "invalid sync error text",
                              "invalid_arguments", 0);
            return;
        }
        error[decoded] = '\0';
        if (!pj_companion_sync_error_valid(error)) {
            serial_sync_error(command, request_id, "invalid sync error text",
                              "invalid_arguments", 0);
            return;
        }
    }
    pj_companion_sync_state_t sync;
    int result = pj_board_companion_sync_usb_progress(
        generation, operation_id, phase, total, pending, transferred, failed,
        error, &sync);
    if (result < 0) {
        serial_sync_error(command, request_id,
                          "sync acknowledgement could not be saved",
                          "sync_store_failed", 1);
    } else if (result == PJ_COMPANION_SYNC_APPLY_REJECTED) {
        serial_sync_error(command, request_id,
                          "sync generation or claim does not match",
                          "generation_conflict", 0);
    } else {
        serial_companion_response(
            command, request_id, &sync, NULL,
            result == PJ_COMPANION_SYNC_APPLY_REPLAY);
    }
}

static int serial_sync_audio_id(const char *encoded, char *audio_id,
                                size_t audio_id_size)
{
    if (encoded == NULL || audio_id == NULL || audio_id_size < 2U) {
        return 0;
    }
    size_t decoded = 0;
    if (!pj_usb_sync_hex_decode(encoded, (uint8_t *)audio_id,
                                audio_id_size - 1U, &decoded) ||
        decoded == 0U || memchr(audio_id, '\0', decoded) != NULL) {
        return 0;
    }
    audio_id[decoded] = '\0';
    return decoded < PJ_NOTE_FILENAME_LEN && strstr(audio_id, "..") == NULL &&
           strchr(audio_id, '/') == NULL && strchr(audio_id, '\\') == NULL &&
           is_audio_filename(audio_id);
}

static uint32_t serial_audio_snapshot(const pj_audio_entry_t *entries,
                                      size_t count)
{
    uint32_t snapshot = pj_usb_sync_snapshot_update(0U, &count, sizeof(count));
    for (size_t i = 0; i < count; i++) {
        snapshot = pj_usb_sync_snapshot_update(
            snapshot, entries[i].filename, strlen(entries[i].filename) + 1U);
        snapshot = pj_usb_sync_snapshot_update(
            snapshot, &entries[i].size_bytes, sizeof(entries[i].size_bytes));
        snapshot = pj_usb_sync_snapshot_update(
            snapshot, &entries[i].data_bytes, sizeof(entries[i].data_bytes));
        snapshot = pj_usb_sync_snapshot_update(
            snapshot, &entries[i].note.synced, sizeof(entries[i].note.synced));
        snapshot = pj_usb_sync_snapshot_update(
            snapshot, entries[i].note.transcript_path,
            strlen(entries[i].note.transcript_path) + 1U);
    }
    return pj_usb_sync_snapshot_finish(snapshot);
}

static int serial_json_add_hex(cJSON *json, const char *key,
                               const char *value)
{
    size_t size = strlen(value);
    if (size > PJ_NOTE_TRANSCRIPT_PATH_LEN - 1U) {
        return 0;
    }
    char encoded[PJ_NOTE_TRANSCRIPT_PATH_LEN * 2U + 1U];
    return pj_usb_sync_hex_encode((const uint8_t *)value, size, encoded,
                                  sizeof(encoded)) &&
           cJSON_AddStringToObject(json, key, encoded) != NULL;
}

static void serial_settings_response(const char *command,
                                     const char *request_id,
                                     const pj_settings_t *settings,
                                     uint32_t generation, int changed,
                                     int replayed)
{
    cJSON *json = serial_sync_response(command, request_id);
    if (json == NULL || !settings_add_json(json, settings, generation) ||
        cJSON_AddBoolToObject(json, "changed", changed != 0) == NULL ||
        cJSON_AddBoolToObject(json, "replayed", replayed != 0) == NULL) {
        cJSON_Delete(json);
        serial_sync_error(command, request_id, "settings response allocation failed",
                          "out_of_memory", 1);
        return;
    }
    serial_print_json("PJ_OK", json);
    cJSON_Delete(json);
}

static int serial_settings_args_exact(const pj_usb_sync_args_t *args,
                                      const char *const *allowed,
                                      size_t allowed_count)
{
    if (args == NULL || args->count != allowed_count) {
        return 0;
    }
    for (size_t i = 0; i < args->count; i++) {
        int matched = 0;
        for (size_t j = 0; j < allowed_count; j++) {
            if (strcmp(args->args[i].key, allowed[j]) == 0) {
                matched = 1;
                break;
            }
        }
        if (!matched) {
            return 0;
        }
    }
    return 1;
}

static void serial_settings_get(char *line)
{
    const char *command = "PJ_SETTINGS_GET";
    const char *const allowed[] = {"request_id"};
    pj_usb_sync_args_t args;
    if (!pj_usb_sync_parse_args(line, command, &args) ||
        !serial_settings_args_exact(&args, allowed,
                                    sizeof(allowed) / sizeof(allowed[0]))) {
        serial_sync_error(command, NULL, "invalid settings read arguments",
                          "invalid_arguments", 0);
        return;
    }
    const char *request_id = pj_usb_sync_arg(&args, "request_id");
    if (!pj_usb_sync_request_id_valid(request_id)) {
        serial_sync_error(command, request_id, "invalid settings request id",
                          "invalid_arguments", 0);
        return;
    }
    pj_settings_t settings;
    uint32_t generation = 0;
    if (!settings_snapshot(&settings, &generation)) {
        serial_sync_error(command, request_id, "settings busy", "settings_busy", 1);
        return;
    }
    serial_settings_response(command, request_id, &settings, generation, 0, 0);
}

static void serial_settings_set(char *line)
{
    const char *command = "PJ_SETTINGS_SET";
    const char *const allowed[] = {
        "request_id", "expected_generation", "payload_hex",
    };
    pj_usb_sync_args_t args;
    if (!pj_usb_sync_parse_args(line, command, &args) ||
        !serial_settings_args_exact(&args, allowed,
                                    sizeof(allowed) / sizeof(allowed[0]))) {
        serial_sync_error(command, NULL, "invalid settings update arguments",
                          "invalid_arguments", 0);
        return;
    }
    const char *request_id = pj_usb_sync_arg(&args, "request_id");
    const char *payload_hex = pj_usb_sync_arg(&args, "payload_hex");
    uint32_t expected_generation = 0;
    if (!pj_usb_sync_request_id_valid(request_id) ||
        !pj_usb_sync_parse_u32(pj_usb_sync_arg(&args, "expected_generation"),
                               &expected_generation) || payload_hex == NULL) {
        serial_sync_error(command, request_id, "invalid settings update arguments",
                          "invalid_arguments", 0);
        return;
    }
    char body[PJ_USB_SETTINGS_BODY_BYTES + 1U];
    size_t body_size = 0;
    if (!pj_usb_sync_hex_decode(payload_hex, (uint8_t *)body,
                                sizeof(body) - 1U, &body_size) ||
        body_size == 0U || memchr(body, '\0', body_size) != NULL) {
        serial_sync_error(command, request_id, "invalid settings payload",
                          "invalid_payload", 0);
        return;
    }
    body[body_size] = '\0';
    if (g_usb_settings_replay.valid &&
        strcmp(g_usb_settings_replay.request_id, request_id) == 0) {
        if (g_usb_settings_replay.expected_generation != expected_generation ||
            g_usb_settings_replay.payload_size != body_size ||
            memcmp(g_usb_settings_replay.payload, body, body_size) != 0) {
            serial_sync_error(command, request_id,
                              "settings request id reused with different content",
                              "request_id_reused", 0);
            return;
        }
        serial_settings_response(command, request_id,
                                 &g_usb_settings_replay.committed,
                                 g_usb_settings_replay.resulting_generation,
                                 g_usb_settings_replay.changed, 1);
        return;
    }

    cJSON *json = cJSON_ParseWithOpts(body, NULL, 1);
    pj_settings_t committed;
    uint32_t generation = 0;
    int changed = 0;
    pj_settings_commit_result_t result = settings_commit_update(
        json, 1, expected_generation, 1, &committed, &generation, &changed);
    cJSON_Delete(json);
    if (result != PJ_SETTINGS_COMMIT_OK) {
        const char *message = "unsupported or invalid settings";
        const char *code = "invalid_settings";
        int retryable = 0;
        if (result == PJ_SETTINGS_COMMIT_BUSY) {
            message = "settings busy";
            code = "settings_busy";
            retryable = 1;
        } else if (result == PJ_SETTINGS_COMMIT_CONFLICT) {
            message = "settings generation conflict";
            code = "generation_conflict";
        } else if (result == PJ_SETTINGS_COMMIT_STORE_FAILED) {
            message = "settings could not be persisted";
            code = "settings_store_failed";
            retryable = 1;
        }
        serial_sync_error(command, request_id, message, code, retryable);
        return;
    }
    g_usb_settings_replay = (pj_usb_settings_replay_t) {
        .valid = 1,
        .expected_generation = expected_generation,
        .payload_size = body_size,
        .committed = committed,
        .resulting_generation = generation,
        .changed = changed,
    };
    (void)snprintf(g_usb_settings_replay.request_id,
                   sizeof(g_usb_settings_replay.request_id), "%s", request_id);
    memcpy(g_usb_settings_replay.payload, body, body_size + 1U);
    serial_settings_response(command, request_id, &committed, generation,
                             changed, 0);
}

static void serial_audio_list(char *line)
{
    const char *command = "PJ_AUDIO_LIST";
    pj_usb_sync_args_t args;
    if (!pj_usb_sync_parse_args(line, command, &args)) {
        serial_sync_error(command, NULL, "invalid audio list arguments",
                          "invalid_arguments", 0);
        return;
    }
    const char *request_id = pj_usb_sync_arg(&args, "request_id");
    uint32_t cursor = 0;
    uint32_t requested_snapshot = 0;
    if (!pj_usb_sync_parse_u32(pj_usb_sync_arg(&args, "cursor"), &cursor) ||
        !pj_usb_sync_parse_u32(pj_usb_sync_arg(&args, "snapshot"),
                               &requested_snapshot) ||
        (request_id != NULL && !pj_usb_sync_request_id_valid(request_id))) {
        serial_sync_error(command, request_id, "invalid audio list arguments",
                          "invalid_arguments", 0);
        return;
    }
    pj_board_status_t status = board_status_snapshot_base();
    if (status.storage != PJ_BOARD_SERVICE_READY) {
        serial_sync_error(command, request_id, "storage unavailable",
                          "storage_unavailable", 1);
        return;
    }
    pj_audio_entry_t *entries = NULL;
    int count = collect_audio_entries(&entries);
    if (count < 0) {
        free(entries);
        if (count == PJ_AUDIO_COLLECT_TOO_MANY) {
            serial_sync_error(command, request_id, "audio library exceeds device index limit",
                              "too_many_audio_files", 0);
        } else if (count == PJ_AUDIO_COLLECT_NO_MEMORY) {
            serial_sync_error(command, request_id, "audio list out of memory",
                              "out_of_memory", 1);
        } else {
            serial_sync_error(command, request_id, "storage maintenance active",
                              "storage_busy", 1);
        }
        return;
    }
    uint32_t snapshot = serial_audio_snapshot(entries, (size_t)count);
    if (requested_snapshot != 0U && requested_snapshot != snapshot) {
        free(entries);
        serial_sync_error(command, request_id, "audio list changed",
                          "list_changed", 1);
        return;
    }
    if (cursor > (uint32_t)count) {
        free(entries);
        serial_sync_error(command, request_id, "audio list cursor out of range",
                          "invalid_cursor", 0);
        return;
    }

    cJSON *json = serial_sync_response(command, request_id);
    cJSON *item = NULL;
    if (json != NULL &&
        cJSON_AddNumberToObject(json, "audio_read_max_bytes",
                                PJ_USB_SYNC_AUDIO_READ_CHUNK_BYTES) == NULL) {
        cJSON_Delete(json);
        json = NULL;
    }
    if (json != NULL) {
        cJSON_AddNumberToObject(json, "snapshot", snapshot);
        cJSON_AddNumberToObject(json, "cursor", cursor);
        uint32_t next = cursor < (uint32_t)count ? cursor + 1U : cursor;
        cJSON_AddNumberToObject(json, "next_cursor", next);
        cJSON_AddBoolToObject(json, "done", next >= (uint32_t)count);
        if (cursor < (uint32_t)count) {
            const pj_audio_entry_t *entry = &entries[cursor];
            item = cJSON_AddObjectToObject(json, "item");
            char source_sha256[65];
            if (item == NULL ||
                !serial_json_add_hex(item, "audio_id_hex", entry->filename) ||
                !serial_json_add_hex(item, "filename_hex", entry->filename)) {
                cJSON_Delete(json);
                json = NULL;
            } else {
                if (entry->label[0] != '\0') {
                    (void)serial_json_add_hex(item, "label_hex", entry->label);
                }
                cJSON_AddNumberToObject(item, "size", entry->size_bytes);
                cJSON_AddNumberToObject(item, "data_bytes", entry->data_bytes);
                if (audio_sha256_hex(entry->filename, source_sha256)) {
                    cJSON_AddStringToObject(item, "source_sha256", source_sha256);
                } else {
                    cJSON_AddNullToObject(item, "source_sha256");
                }
                if (entry->note.created_at[0] != '\0') {
                    (void)serial_json_add_hex(item, "created_at_hex",
                                              entry->note.created_at);
                }
                cJSON_AddNumberToObject(item, "duration_ms",
                                        entry->note.duration_ms);
                cJSON_AddBoolToObject(item, "synced",
                                      entry->note.synced != 0);
                cJSON_AddBoolToObject(item, "transcript_uploaded",
                                      entry->note.synced != 0);
                if (entry->note.transcript_path[0] != '\0') {
                    (void)serial_json_add_hex(item, "transcript_path_hex",
                                              entry->note.transcript_path);
                }
            }
        }
    }
    free(entries);
    if (json == NULL) {
        serial_sync_error(command, request_id, "audio list response allocation failed",
                          "out_of_memory", 1);
        return;
    }
    serial_print_json("PJ_OK", json);
    cJSON_Delete(json);
}

static int serial_audio_identity_unlocked(const char *audio_id,
                                          const struct stat *st,
                                          char sha256[65])
{
    if (g_usb_audio_identity.valid &&
        strcmp(g_usb_audio_identity.audio_id, audio_id) == 0 &&
        g_usb_audio_identity.size == st->st_size &&
        g_usb_audio_identity.modified == st->st_mtime) {
        (void)memcpy(sha256, g_usb_audio_identity.sha256,
                     sizeof(g_usb_audio_identity.sha256));
        return 1;
    }
    if (!audio_sha256_hex_unlocked(audio_id, sha256)) {
        return 0;
    }
    memset(&g_usb_audio_identity, 0, sizeof(g_usb_audio_identity));
    (void)snprintf(g_usb_audio_identity.audio_id,
                   sizeof(g_usb_audio_identity.audio_id), "%s", audio_id);
    (void)memcpy(g_usb_audio_identity.sha256, sha256, 65U);
    g_usb_audio_identity.size = st->st_size;
    g_usb_audio_identity.modified = st->st_mtime;
    g_usb_audio_identity.valid = 1;
    return 1;
}

static void serial_audio_read(char *line)
{
    const char *command = "PJ_AUDIO_READ";
    pj_usb_sync_args_t args;
    if (!pj_usb_sync_parse_args(line, command, &args)) {
        serial_sync_error(command, NULL, "invalid audio read arguments",
                          "invalid_arguments", 0);
        return;
    }
    const char *request_id = pj_usb_sync_arg(&args, "request_id");
    const char *expected_sha = pj_usb_sync_arg(&args, "source_sha256");
    char audio_id[PJ_NOTE_FILENAME_LEN];
    uint64_t offset = 0;
    uint32_t maximum = 0;
    if (!serial_sync_audio_id(pj_usb_sync_arg(&args, "id_hex"), audio_id,
                              sizeof(audio_id)) ||
        !pj_usb_sync_parse_u64(pj_usb_sync_arg(&args, "offset"), &offset) ||
        !pj_usb_sync_parse_u32(pj_usb_sync_arg(&args, "max_bytes"), &maximum) ||
        !pj_usb_sync_audio_read_size_valid(maximum) ||
        (expected_sha != NULL && !pj_usb_sync_sha256_hex_valid(expected_sha)) ||
        (request_id != NULL && !pj_usb_sync_request_id_valid(request_id))) {
        serial_sync_error(command, request_id, "invalid audio read arguments",
                          "invalid_arguments", 0);
        return;
    }
    if (!storage_shared_try_acquire()) {
        serial_sync_error(command, request_id, "storage maintenance active",
                          "storage_busy", 1);
        return;
    }
    char path[160];
    (void)snprintf(path, sizeof(path), PJ_AUDIO_DIR "/%s", audio_id);
    struct stat st;
    char actual_sha[65];
    if (stat(path, &st) != 0 || st.st_size < 0 ||
        !serial_audio_identity_unlocked(audio_id, &st, actual_sha)) {
        storage_shared_release();
        serial_sync_error(command, request_id, "audio not found",
                          "audio_not_found", 0);
        return;
    }
    if (expected_sha != NULL && strcasecmp(expected_sha, actual_sha) != 0) {
        storage_shared_release();
        serial_sync_error(command, request_id, "audio source changed",
                          "source_changed", 1);
        return;
    }
    if (offset > (uint64_t)st.st_size) {
        storage_shared_release();
        serial_sync_error(command, request_id, "audio offset out of range",
                          "invalid_offset", 0);
        return;
    }
    uint8_t data[PJ_USB_SYNC_AUDIO_READ_CHUNK_BYTES];
    size_t wanted = maximum;
    uint64_t remaining = (uint64_t)st.st_size - offset;
    if ((uint64_t)wanted > remaining) {
        wanted = (size_t)remaining;
    }
    FILE *file = fopen(path, "rb");
    size_t read = 0;
    int read_ok = file != NULL && fseeko(file, (off_t)offset, SEEK_SET) == 0;
    if (read_ok && wanted > 0U) {
        read = fread(data, 1, wanted, file);
        read_ok = read == wanted && ferror(file) == 0;
    }
    if (file != NULL) {
        read_ok = fclose(file) == 0 && read_ok;
    }
    storage_shared_release();
    if (!read_ok) {
        serial_sync_error(command, request_id, "audio read failed",
                          "storage_io", 1);
        return;
    }
    char encoded[PJ_USB_SYNC_AUDIO_READ_CHUNK_BYTES * 2U + 1U];
    cJSON *json = serial_sync_response(command, request_id);
    if (json == NULL ||
        !pj_usb_sync_hex_encode(data, read, encoded, sizeof(encoded)) ||
        cJSON_AddStringToObject(json, "id_hex",
                                pj_usb_sync_arg(&args, "id_hex")) == NULL ||
        cJSON_AddNumberToObject(json, "offset", (double)offset) == NULL ||
        cJSON_AddNumberToObject(json, "total_bytes", (double)st.st_size) == NULL ||
        cJSON_AddStringToObject(json, "data_hex", encoded) == NULL ||
        cJSON_AddBoolToObject(json, "eof",
                              offset + read == (uint64_t)st.st_size) == NULL ||
        cJSON_AddStringToObject(json, "source_sha256", actual_sha) == NULL) {
        cJSON_Delete(json);
        serial_sync_error(command, request_id, "audio response allocation failed",
                          "out_of_memory", 1);
        return;
    }
    serial_print_json("PJ_OK", json);
    cJSON_Delete(json);
}

static uint32_t serial_upload_id(void)
{
    uint32_t id = esp_random();
    return id == 0U ? 1U : id;
}

static int serial_create_upload_file(uint32_t expected_bytes)
{
    if (!storage_shared_try_acquire()) {
        return 0;
    }
    pj_board_status_t status = board_status_snapshot_base();
    int ready = status.storage == PJ_BOARD_SERVICE_READY &&
                storage_preflight(expected_bytes, "USB transcript upload");
    remove(PJ_USB_TRANSCRIPT_TEMP_PATH);
    FILE *file = ready ? fopen(PJ_USB_TRANSCRIPT_TEMP_PATH, "wb") : NULL;
    int ok = file != NULL;
    if (file != NULL) {
        ok = fflush(file) == 0 && fsync(fileno(file)) == 0;
        ok = fclose(file) == 0 && ok;
    }
    if (!ok) {
        remove(PJ_USB_TRANSCRIPT_TEMP_PATH);
    }
    storage_shared_release();
    return ok;
}

static void serial_transcript_begin(char *line)
{
    const char *command = "PJ_TRANSCRIPT_BEGIN";
    pj_usb_sync_args_t args;
    if (!pj_usb_sync_parse_args(line, command, &args)) {
        serial_sync_error(command, NULL, "invalid transcript begin arguments",
                          "invalid_arguments", 0);
        return;
    }
    const char *request_id = pj_usb_sync_arg(&args, "request_id");
    const char *sha256 = pj_usb_sync_arg(&args, "sha256");
    char audio_id[PJ_NOTE_FILENAME_LEN];
    uint32_t expected_bytes = 0;
    if (!pj_usb_sync_request_id_valid(request_id) ||
        !serial_sync_audio_id(pj_usb_sync_arg(&args, "id_hex"), audio_id,
                              sizeof(audio_id)) ||
        !pj_usb_sync_parse_u32(pj_usb_sync_arg(&args, "bytes"),
                               &expected_bytes) ||
        expected_bytes == 0U ||
        expected_bytes > PJ_USB_SYNC_TRANSCRIPT_MAX_BYTES ||
        !pj_usb_sync_sha256_hex_valid(sha256)) {
        serial_sync_error(command, request_id,
                          "invalid transcript begin arguments",
                          "invalid_arguments", 0);
        return;
    }
    pj_board_status_t status = board_status_snapshot_base();
    if (status.storage != PJ_BOARD_SERVICE_READY) {
        serial_sync_error(command, request_id, "storage unavailable",
                          "storage_unavailable", 1);
        return;
    }
    pj_audio_entry_t entry;
    if (!probe_audio_entry(audio_id, &entry)) {
        serial_sync_error(command, request_id, "audio not found",
                          "audio_not_found", 0);
        return;
    }
    pj_usb_upload_begin_result_t begin = pj_usb_upload_begin(
        &g_usb_upload, serial_upload_id(), request_id, audio_id,
        expected_bytes, sha256);
    if (begin == PJ_USB_UPLOAD_BEGIN_BUSY) {
        serial_sync_error(command, request_id,
                          "another transcript upload is active",
                          "upload_busy", 1);
        return;
    }
    if (begin == PJ_USB_UPLOAD_BEGIN_INVALID) {
        serial_sync_error(command, request_id,
                          "invalid transcript begin arguments",
                          "invalid_arguments", 0);
        return;
    }
    if (begin == PJ_USB_UPLOAD_BEGIN_STARTED &&
        !serial_create_upload_file(expected_bytes)) {
        pj_usb_upload_init(&g_usb_upload);
        pj_board_status_t status = board_status_snapshot_base();
        serial_sync_error(command, request_id,
                          "transcript upload file could not be created",
                          status.storage_health == PJ_STORAGE_HEALTH_FULL ?
                              "storage_full" : "storage_io",
                          1);
        return;
    }
    cJSON *json = serial_sync_response(command, request_id);
    if (json == NULL) {
        serial_sync_error(command, request_id, "response allocation failed",
                          "out_of_memory", 1);
        return;
    }
    cJSON_AddNumberToObject(json, "upload_id", g_usb_upload.upload_id);
    cJSON_AddNumberToObject(json, "offset", g_usb_upload.received_bytes);
    cJSON_AddBoolToObject(json, "accepted", 1);
    cJSON_AddBoolToObject(json, "attached",
                          begin == PJ_USB_UPLOAD_BEGIN_ATTACHED);
    serial_print_json("PJ_OK", json);
    cJSON_Delete(json);
}

static void serial_transcript_write(char *line)
{
    const char *command = "PJ_TRANSCRIPT_WRITE";
    pj_usb_sync_args_t args;
    if (!pj_usb_sync_parse_args(line, command, &args)) {
        serial_sync_error(command, NULL, "invalid transcript write arguments",
                          "invalid_arguments", 0);
        return;
    }
    const char *request_id = pj_usb_sync_arg(&args, "request_id");
    uint32_t upload_id = 0;
    uint32_t offset = 0;
    uint8_t data[PJ_USB_SYNC_CHUNK_BYTES];
    size_t data_size = 0;
    if (!pj_usb_sync_request_id_valid(request_id) ||
        !pj_usb_sync_parse_u32(pj_usb_sync_arg(&args, "upload_id"),
                               &upload_id) || upload_id == 0U ||
        !pj_usb_sync_parse_u32(pj_usb_sync_arg(&args, "offset"), &offset) ||
        !pj_usb_sync_hex_decode(pj_usb_sync_arg(&args, "data_hex"), data,
                                sizeof(data), &data_size) || data_size == 0U) {
        serial_sync_error(command, request_id,
                          "invalid transcript write arguments",
                          "invalid_arguments", 0);
        return;
    }
    pj_usb_upload_write_result_t check = pj_usb_upload_check_write(
        &g_usb_upload, upload_id, offset, data, data_size);
    if (check == PJ_USB_UPLOAD_WRITE_UNKNOWN) {
        serial_sync_error(command, request_id, "unknown transcript upload",
                          "upload_not_found", 0);
        return;
    }
    if (check == PJ_USB_UPLOAD_WRITE_OFFSET) {
        serial_sync_error(command, request_id, "transcript offset mismatch",
                          "offset_mismatch", 1);
        return;
    }
    if (check == PJ_USB_UPLOAD_WRITE_CONTENT) {
        serial_sync_error(command, request_id,
                          "transcript replay content mismatch",
                          "content_mismatch", 0);
        return;
    }
    if (check == PJ_USB_UPLOAD_WRITE_TOO_LARGE) {
        serial_sync_error(command, request_id, "transcript chunk out of bounds",
                          "chunk_out_of_bounds", 0);
        return;
    }
    if (check == PJ_USB_UPLOAD_WRITE_NEW) {
        if (!storage_shared_try_acquire()) {
            serial_sync_error(command, request_id, "storage maintenance active",
                              "storage_busy", 1);
            return;
        }
        struct stat st;
        FILE *file = NULL;
        int ok = stat(PJ_USB_TRANSCRIPT_TEMP_PATH, &st) == 0 &&
                 st.st_size == (off_t)g_usb_upload.received_bytes;
        if (ok) {
            file = fopen(PJ_USB_TRANSCRIPT_TEMP_PATH, "r+b");
            ok = file != NULL && fseeko(file, (off_t)offset, SEEK_SET) == 0 &&
                 fwrite(data, 1, data_size, file) == data_size &&
                 fflush(file) == 0 && fsync(fileno(file)) == 0;
        }
        if (file != NULL) {
            ok = fclose(file) == 0 && ok;
        }
        storage_shared_release();
        if (!ok) {
            serial_sync_error(command, request_id,
                              "transcript chunk could not be stored",
                              "storage_io", 1);
            return;
        }
        pj_usb_upload_apply_write(&g_usb_upload, offset, data, data_size);
    }
    cJSON *json = serial_sync_response(command, request_id);
    if (json == NULL) {
        serial_sync_error(command, request_id, "response allocation failed",
                          "out_of_memory", 1);
        return;
    }
    cJSON_AddNumberToObject(json, "upload_id", upload_id);
    cJSON_AddNumberToObject(json, "next_offset", g_usb_upload.received_bytes);
    cJSON_AddBoolToObject(json, "replayed",
                          check == PJ_USB_UPLOAD_WRITE_REPLAY);
    serial_print_json("PJ_OK", json);
    cJSON_Delete(json);
}

static int serial_read_upload_body(char **body, size_t *body_size)
{
    struct stat st;
    if (body == NULL || body_size == NULL || !storage_shared_try_acquire()) {
        return 0;
    }
    if (stat(PJ_USB_TRANSCRIPT_TEMP_PATH, &st) != 0 || st.st_size <= 0 ||
        (uint64_t)st.st_size > PJ_USB_SYNC_TRANSCRIPT_MAX_BYTES ||
        (uint32_t)st.st_size != g_usb_upload.expected_bytes) {
        storage_shared_release();
        return 0;
    }
    char *allocated = heap_caps_malloc((size_t)st.st_size + 1U,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (allocated == NULL) {
        allocated = malloc((size_t)st.st_size + 1U);
    }
    if (allocated == NULL) {
        storage_shared_release();
        return 0;
    }
    FILE *file = fopen(PJ_USB_TRANSCRIPT_TEMP_PATH, "rb");
    size_t read = file == NULL ? 0U : fread(allocated, 1, (size_t)st.st_size, file);
    int ok = file != NULL && read == (size_t)st.st_size && ferror(file) == 0;
    if (file != NULL) {
        ok = fclose(file) == 0 && ok;
    }
    storage_shared_release();
    if (!ok) {
        free(allocated);
        return 0;
    }
    allocated[read] = '\0';
    *body = allocated;
    *body_size = read;
    return 1;
}

static void serial_transcript_commit(char *line)
{
    const char *command = "PJ_TRANSCRIPT_COMMIT";
    pj_usb_sync_args_t args;
    if (!pj_usb_sync_parse_args(line, command, &args)) {
        serial_sync_error(command, NULL, "invalid transcript commit arguments",
                          "invalid_arguments", 0);
        return;
    }
    const char *request_id = pj_usb_sync_arg(&args, "request_id");
    const char *sha256 = pj_usb_sync_arg(&args, "sha256");
    uint32_t upload_id = 0;
    if (!pj_usb_sync_request_id_valid(request_id) ||
        !pj_usb_sync_parse_u32(pj_usb_sync_arg(&args, "upload_id"),
                               &upload_id) ||
        !pj_usb_sync_sha256_hex_valid(sha256) ||
        !pj_usb_upload_commit_ready(&g_usb_upload, upload_id, sha256)) {
        serial_sync_error(command, request_id,
                          "transcript upload is incomplete or unknown",
                          "upload_incomplete", 1);
        return;
    }
    if (g_usb_upload.status != PJ_USB_UPLOAD_COMMITTED) {
        if (!storage_shared_try_acquire()) {
            serial_sync_error(command, request_id, "storage maintenance active",
                              "storage_busy", 1);
            return;
        }
        char actual_sha[65];
        int digest_ok = file_sha256_hex_unlocked(PJ_USB_TRANSCRIPT_TEMP_PATH,
                                                 actual_sha);
        storage_shared_release();
        if (!digest_ok || strcasecmp(actual_sha, sha256) != 0) {
            serial_sync_error(command, request_id,
                              "transcript checksum mismatch",
                              "checksum_mismatch", 0);
            return;
        }
        char *body = NULL;
        size_t body_size = 0;
        if (!serial_read_upload_body(&body, &body_size)) {
            serial_sync_error(command, request_id,
                              "transcript body could not be read",
                              "out_of_memory", 1);
            return;
        }
        pj_transcript_body_result_t validation = pj_transcript_body_validate(
            body, body_size, body_size);
        if (validation != PJ_TRANSCRIPT_BODY_VALID) {
            free(body);
            serial_sync_error(command, request_id,
                              validation == PJ_TRANSCRIPT_BODY_MALFORMED ?
                                  "malformed transcript JSON" :
                                  "transcript requires non-empty text",
                              "invalid_transcript", 0);
            return;
        }
        if (!storage_shared_try_acquire()) {
            free(body);
            serial_sync_error(command, request_id, "storage maintenance active",
                              "storage_busy", 1);
            return;
        }
        pj_audio_entry_t entry;
        if (!probe_audio_entry_unlocked(g_usb_upload.audio_id, &entry)) {
            storage_shared_release();
            free(body);
            serial_sync_error(command, request_id, "audio not found",
                              "audio_not_found", 0);
            return;
        }
        pj_transcript_source_result_t source_result;
        if (!transcript_source_check_audio_unlocked(
                body, body_size, &entry, &source_result)) {
            storage_shared_release();
            free(body);
            serial_sync_error(command, request_id,
                              "audio checksum could not be read",
                              "storage_io", 1);
            return;
        }
        pj_transcript_commit_source_decision_t source_decision =
            pj_transcript_source_commit_decision(source_result);
        if (source_decision == PJ_TRANSCRIPT_COMMIT_SOURCE_RETRY_CHANGED) {
            storage_shared_release();
            free(body);
            serial_sync_error(command, request_id,
                              "audio source changed; download and retry",
                              "source_changed", 1);
            return;
        }
        if (source_decision == PJ_TRANSCRIPT_COMMIT_SOURCE_REJECT_INVALID) {
            storage_shared_release();
            free(body);
            serial_sync_error(command, request_id,
                              "invalid transcript source provenance",
                              "invalid_transcript", 0);
            return;
        }
        char path[PJ_NOTE_TRANSCRIPT_PATH_LEN];
        if (!transcript_path_for_audio(path, sizeof(path),
                                       g_usb_upload.audio_id)) {
            storage_shared_release();
            free(body);
            serial_sync_error(command, request_id, "invalid audio id",
                              "invalid_arguments", 0);
            return;
        }
        esp_err_t write_error = json_write_file_atomic(path, body, body_size);
        free(body);
        if (write_error != ESP_OK) {
            storage_shared_release();
            serial_sync_error(command, request_id,
                              "transcript could not be committed",
                              write_error == ESP_ERR_INVALID_STATE ?
                                  "storage_busy" : "storage_io",
                              1);
            return;
        }
        remove(PJ_USB_TRANSCRIPT_TEMP_PATH);
        if (!probe_audio_entry_unlocked(g_usb_upload.audio_id, &entry)) {
            ESP_LOGW(TAG, "USB transcript committed but metadata refresh failed");
        }
        storage_shared_release();
        pj_usb_upload_mark_committed(&g_usb_upload);
        board_update_publish(BOARD_UPDATE_NOTES);
    }
    cJSON *json = serial_sync_response(command, request_id);
    if (json == NULL) {
        serial_sync_error(command, request_id, "response allocation failed",
                          "out_of_memory", 1);
        return;
    }
    cJSON_AddNumberToObject(json, "upload_id", upload_id);
    cJSON_AddBoolToObject(json, "committed", 1);
    cJSON_AddNumberToObject(json, "bytes", g_usb_upload.expected_bytes);
    serial_print_json("PJ_OK", json);
    cJSON_Delete(json);
}

static void serial_transcript_abort(char *line)
{
    const char *command = "PJ_TRANSCRIPT_ABORT";
    pj_usb_sync_args_t args;
    if (!pj_usb_sync_parse_args(line, command, &args)) {
        serial_sync_error(command, NULL, "invalid transcript abort arguments",
                          "invalid_arguments", 0);
        return;
    }
    const char *request_id = pj_usb_sync_arg(&args, "request_id");
    uint32_t upload_id = 0;
    if (!pj_usb_sync_request_id_valid(request_id) ||
        !pj_usb_sync_parse_u32(pj_usb_sync_arg(&args, "upload_id"),
                               &upload_id) || upload_id == 0U) {
        serial_sync_error(command, request_id,
                          "invalid transcript abort arguments",
                          "invalid_arguments", 0);
        return;
    }
    int matched_active = g_usb_upload.status == PJ_USB_UPLOAD_ACTIVE &&
                         g_usb_upload.upload_id == upload_id;
    if (!pj_usb_upload_abort(&g_usb_upload, upload_id)) {
        serial_sync_error(command, request_id,
                          "committed transcript cannot be aborted",
                          "already_committed", 0);
        return;
    }
    if (matched_active && storage_shared_try_acquire()) {
        remove(PJ_USB_TRANSCRIPT_TEMP_PATH);
        storage_shared_release();
    }
    cJSON *json = serial_sync_response(command, request_id);
    if (json == NULL) {
        serial_sync_error(command, request_id, "response allocation failed",
                          "out_of_memory", 1);
        return;
    }
    cJSON_AddNumberToObject(json, "upload_id", upload_id);
    cJSON_AddBoolToObject(json, "aborted", 1);
    serial_print_json("PJ_OK", json);
    cJSON_Delete(json);
}

static int serial_try_sync_command(char *line)
{
    if (serial_command_prefix(line, "PJ_SYNC_STATUS")) {
        serial_companion_status(line);
    } else if (serial_command_prefix(line, "PJ_SYNC_CLAIM")) {
        serial_companion_claim(line);
    } else if (serial_command_prefix(line, "PJ_SYNC_PROGRESS")) {
        serial_companion_progress(line, "PJ_SYNC_PROGRESS", "running", 0);
    } else if (serial_command_prefix(line, "PJ_SYNC_COMPLETE")) {
        serial_companion_progress(line, "PJ_SYNC_COMPLETE", "succeeded", 0);
    } else if (serial_command_prefix(line, "PJ_SYNC_FAIL")) {
        serial_companion_progress(line, "PJ_SYNC_FAIL", "failed", 1);
    } else if (serial_command_prefix(line, "PJ_SETTINGS_GET")) {
        serial_settings_get(line);
    } else if (serial_command_prefix(line, "PJ_SETTINGS_SET")) {
        serial_settings_set(line);
    } else if (serial_command_prefix(line, "PJ_AUDIO_LIST")) {
        serial_audio_list(line);
    } else if (serial_command_prefix(line, "PJ_AUDIO_READ")) {
        serial_audio_read(line);
    } else if (serial_command_prefix(line, "PJ_TRANSCRIPT_BEGIN")) {
        serial_transcript_begin(line);
    } else if (serial_command_prefix(line, "PJ_TRANSCRIPT_WRITE")) {
        serial_transcript_write(line);
    } else if (serial_command_prefix(line, "PJ_TRANSCRIPT_COMMIT")) {
        serial_transcript_commit(line);
    } else if (serial_command_prefix(line, "PJ_TRANSCRIPT_ABORT")) {
        serial_transcript_abort(line);
    } else {
        return 0;
    }
    return 1;
}

static void serial_command_task(void *arg)
{
    (void)arg;
    char line[768] = {0};
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
        if (serial_try_sync_command(line)) {
            fflush(stdout);
            continue;
        }
        const char *request_id = NULL;
        if (serial_request_matches(line, "PJ_STATUS", &request_id)) {
            serial_print_status(request_id);
            fflush(stdout);
            serial_stack_margin_check("PJ_STATUS");
            continue;
        }
        if (serial_request_matches(line, "PJ_WIPE_RECORDINGS", &request_id)) {
            pj_wipe_status_t wipe_status = {0};
            pj_wipe_start_result_t result = recording_wipe_start(
                request_id, &wipe_status, PJ_WIPE_WORKER_RELEASE_AFTER_RESPONSE);
            serial_print_wipe_start(result, &wipe_status, request_id);
            int drain_error = serial_drain_stdout();
            if (result == PJ_WIPE_START_STARTED &&
                !recording_wipe_release(wipe_status.id)) {
                ESP_LOGE(TAG, "Recording wipe ACK release failed: operation=%" PRIu32,
                         wipe_status.id);
            }
            if (drain_error != 0) {
                ESP_LOGW(TAG, "USB wipe response drain failed: errno=%d", drain_error);
            }
            continue;
        }
        if (serial_request_matches(line, "PJ_INTERVAL_RESET", &request_id)) {
            serial_print_interval_reset(request_id);
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
                pj_board_status_t status = board_status_snapshot_base();
                printf("PJ_OK {\"device_id\":\"%s\",\"wifi\":\"stored\"}\n",
                       status.device_id);
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
            int utc_offset_minutes = 0;
            int second = 0;
            char extra = '\0';
            int fields = sscanf(time_payload, "%d %d %d %d %d %d %d %c",
                                &year, &month, &day, &hour, &minute,
                                &utc_offset_minutes, &second, &extra);
            int update_utc_offset = fields >= 6;
            if ((fields == 5 || fields == 6 || fields == 7) &&
                valid_time_date(hour, minute, year, month, day) &&
                second >= 0 && second <= 59 &&
                (!update_utc_offset ||
                 pj_time_utc_offset_valid(utc_offset_minutes))) {
                board_time_update_result_t update = board_set_time_date(
                    hour, minute, second, &year, 1, month, day,
                    update_utc_offset, utc_offset_minutes, "USB-C partner");
                if (update.status == BOARD_TIME_UPDATE_OK) {
                    if (update_utc_offset) {
                        printf("PJ_OK {\"hour\":%d,\"minute\":%d,\"second\":%d,\"year\":%d,\"month\":%d,\"day\":%d,\"utc_offset_minutes\":%d}\n",
                               hour, minute, second, year, month, day,
                               utc_offset_minutes);
                    } else {
                        printf("PJ_OK {\"hour\":%d,\"minute\":%d,\"second\":%d,\"year\":%d,\"month\":%d,\"day\":%d}\n",
                               hour, minute, second, year, month, day);
                    }
                } else {
                    serial_print_time_update_error(&update);
                }
            } else {
                printf("PJ_ERR {\"error\":\"expected PJ_TIME yyyy mm dd hh mm [utc_offset_minutes [second]]\"}\n");
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
    int driver_installed_here = 0;
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = {
            .tx_buffer_size = PJ_SERIAL_TX_BUFFER_BYTES,
            .rx_buffer_size = PJ_SERIAL_RX_BUFFER_BYTES,
        };
        esp_err_t err = usb_serial_jtag_driver_install(&config);
        if (err != ESP_OK) {
            return err;
        }
        driver_installed_here = 1;
    }
    int input_flags = fcntl(fileno(stdin), F_GETFL);
    if (input_flags < 0 ||
        fcntl(fileno(stdin), F_SETFL, input_flags & ~O_NONBLOCK) < 0) {
        if (driver_installed_here) {
            (void)usb_serial_jtag_driver_uninstall();
        }
        return ESP_FAIL;
    }
    usb_serial_jtag_vfs_use_driver();
    BaseType_t created = xTaskCreate(serial_command_task, "pj-serial", PJ_SERIAL_COMMAND_TASK_STACK, NULL, 3, NULL);
    if (created != pdPASS) {
        if (driver_installed_here) {
            usb_serial_jtag_vfs_use_nonblocking();
        }
        (void)fcntl(fileno(stdin), F_SETFL, input_flags);
        if (driver_installed_here) {
            (void)usb_serial_jtag_driver_uninstall();
        }
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
    int wifi_provisioned = wifi_credentials_stored_snapshot();
    if (status.storage_mounted && storage_shared_try_acquire()) {
        (void)storage_refresh_capacity();
        storage_shared_release();
        status = pj_board_status();
    }
    if (status.storage == PJ_BOARD_SERVICE_READY) {
        collect_sync_counts(&pending_sync, &transferred_sync);
    }
    cJSON *json = cJSON_CreateObject();
    cJSON *time = json == NULL ? NULL : cJSON_AddObjectToObject(json, "time");
    if (json == NULL || time == NULL) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"status allocation failed\"}");
    }
    cJSON_AddStringToObject(json, "device_id", status.device_id);
    cJSON_AddStringToObject(json, "firmware_version",
                            esp_app_get_description()->version);
    cJSON_AddStringToObject(json, "board_profile", "waveshare-esp32-s3-touch-epaper-1.54-v2");
    cJSON_AddStringToObject(json, "display", service_name(status.display));
    cJSON_AddStringToObject(json, "storage", service_name(status.storage));
    cJSON_AddStringToObject(json, "audio", service_name(status.audio));
    cJSON_AddStringToObject(json, "ble_provisioning",
                            service_name(status.ble_provisioning));
    cJSON_AddStringToObject(json, "wifi", service_name(status.wifi));
    cJSON_AddStringToObject(json, "http", service_name(status.http));
    cJSON_AddStringToObject(json, "ip", status.ip_addr);
    cJSON_AddBoolToObject(json, "wifi_provisioned", wifi_provisioned);
    cJSON_AddNumberToObject(json, "battery_percent", status.battery_percent);
    cJSON_AddNumberToObject(json, "temperature_c", status.temperature_c);
    if (status.humidity_percent < 0) {
        cJSON_AddNullToObject(json, "humidity_percent");
    } else {
        cJSON_AddNumberToObject(json, "humidity_percent",
                                status.humidity_percent);
    }
    cJSON_AddNumberToObject(time, "hour", status.hour);
    cJSON_AddNumberToObject(time, "minute", status.minute);
    cJSON_AddNumberToObject(time, "year", status.year);
    cJSON_AddNumberToObject(time, "month", status.month);
    cJSON_AddNumberToObject(time, "day", status.day);
    cJSON_AddBoolToObject(json, "storage_mounted", status.storage_mounted != 0);
    cJSON_AddStringToObject(json, "storage_health",
                            pj_storage_health_name(status.storage_health));
    cJSON_AddNumberToObject(json, "storage_total_bytes",
                            (double)status.storage_total_bytes);
    cJSON_AddNumberToObject(json, "storage_free_bytes",
                            (double)status.storage_free_bytes);
    cJSON_AddNumberToObject(json, "storage_recovery_count",
                            status.storage_recovery_count);
    cJSON_AddNumberToObject(json, "pending_sync", pending_sync);
    cJSON_AddNumberToObject(json, "transferred_sync", transferred_sync);
    cJSON_AddStringToObject(json, "last_error", status.last_error);
    pj_companion_sync_state_t companion_sync;
    cJSON *companion_json = cJSON_AddObjectToObject(json, "companion_sync");
    int companion_ok = pj_board_companion_sync_snapshot(&companion_sync) &&
                       companion_sync_add_json(companion_json,
                                               &companion_sync);
    storage_wipe_snapshot_t wipe = storage_wipe_snapshot();
    if (!companion_ok || !capabilities_add_json(json, 1) ||
        !runtime_identity_add_json(json) ||
        !wipe_status_add_json(json, &wipe.current) ||
        !wipe_history_add_json(json, wipe.history, wipe.history_count) ||
        !connectivity_add_json(json, &status)) {
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

static esp_err_t time_send_response(httpd_req_t *req,
                                    const board_time_snapshot_t *time,
                                    int offset_known, int offset_minutes)
{
    cJSON *json = cJSON_CreateObject();
    if (json == NULL ||
        cJSON_AddNumberToObject(json, "hour", time->hour) == NULL ||
        cJSON_AddNumberToObject(json, "minute", time->minute) == NULL ||
        cJSON_AddNumberToObject(json, "second", time->second) == NULL ||
        cJSON_AddNumberToObject(json, "year", time->year) == NULL ||
        cJSON_AddNumberToObject(json, "month", time->month) == NULL ||
        cJSON_AddNumberToObject(json, "day", time->day) == NULL ||
        (offset_known &&
         cJSON_AddNumberToObject(json, "utc_offset_minutes", offset_minutes) == NULL)) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_json(req, "{\"error\":\"time response allocation failed\"}");
    }
    esp_err_t result = send_json_object(req, json);
    cJSON_Delete(json);
    return result;
}

static esp_err_t time_send_update_error(
    httpd_req_t *req, const board_time_update_result_t *result)
{
    board_time_update_error_fields_t fields =
        board_time_update_error_fields(result);
    if (result->status == BOARD_TIME_UPDATE_INVALID ||
        result->status == BOARD_TIME_UPDATE_YEAR_REQUIRED) {
        httpd_resp_set_status(req, "400 Bad Request");
    } else if (result->status == BOARD_TIME_UPDATE_PARTIAL_COMMIT) {
        httpd_resp_set_status(req, "500 Internal Server Error");
    } else {
        httpd_resp_set_status(req, "503 Service Unavailable");
    }

    int rtc_failed = (result->partial_components &
                      PJ_TIME_TRANSACTION_COMPONENT_RTC) != 0;
    int offset_failed = (result->partial_components &
                         PJ_TIME_TRANSACTION_COMPONENT_UTC_OFFSET) != 0;
    cJSON *json = cJSON_CreateObject();
    if (json == NULL ||
        cJSON_AddStringToObject(json, "error", fields.error) == NULL ||
        cJSON_AddStringToObject(json, "code", fields.code) == NULL ||
        cJSON_AddBoolToObject(json, "retryable", fields.retryable) == NULL ||
        cJSON_AddBoolToObject(json, "partial_commit",
                             fields.partial_commit) == NULL ||
        cJSON_AddNumberToObject(json, "partial_components",
                               result->partial_components) == NULL ||
        cJSON_AddBoolToObject(json, "rtc_rollback_failed", rtc_failed) == NULL ||
        cJSON_AddBoolToObject(json, "utc_offset_rollback_failed",
                             offset_failed) == NULL) {
        cJSON_Delete(json);
        return send_json(req, "{\"error\":\"time error response allocation failed\"}");
    }
    esp_err_t response = send_json_object(req, json);
    cJSON_Delete(json);
    return response;
}

static esp_err_t time_get_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    if (!time_transaction_take()) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"time unavailable\"}");
    }
    board_time_snapshot_t time = board_time_snapshot();
    int offset_known;
    int offset_minutes;
    portENTER_CRITICAL(&g_connectivity_lock);
    offset_known = g_utc_offset_known;
    offset_minutes = g_utc_offset_minutes;
    portEXIT_CRITICAL(&g_connectivity_lock);
    time_transaction_give();
    return time_send_response(req, &time, offset_known, offset_minutes);
}

static int json_exact_int(const cJSON *item, int *value)
{
    if (!cJSON_IsNumber(item) || item->valuedouble != (double)item->valueint) {
        return 0;
    }
    *value = item->valueint;
    return 1;
}

static int time_parse_update(const char *body, int *hour, int *minute,
                             int *second,
                             int *year, int *month, int *day,
                             int *has_year,
                             int *has_offset, int *offset_minutes)
{
    cJSON *json = cJSON_Parse(body);
    if (!cJSON_IsObject(json) || json->child == NULL) {
        cJSON_Delete(json);
        return 0;
    }
    unsigned seen = 0;
    for (const cJSON *item = json->child; item != NULL; item = item->next) {
        const char *key = item->string;
        unsigned field = 0;
        int *target = NULL;
        if (key != NULL && strcmp(key, "hour") == 0) {
            field = 1u << 0;
            target = hour;
        } else if (key != NULL && strcmp(key, "minute") == 0) {
            field = 1u << 1;
            target = minute;
        } else if (key != NULL && strcmp(key, "year") == 0) {
            field = 1u << 2;
            target = year;
        } else if (key != NULL && strcmp(key, "month") == 0) {
            field = 1u << 3;
            target = month;
        } else if (key != NULL && strcmp(key, "day") == 0) {
            field = 1u << 4;
            target = day;
        } else if (key != NULL && strcmp(key, "utc_offset_minutes") == 0) {
            field = 1u << 5;
            target = offset_minutes;
        } else if (key != NULL && strcmp(key, "second") == 0) {
            field = 1u << 6;
            target = second;
        } else {
            cJSON_Delete(json);
            return 0;
        }
        if ((seen & field) != 0 || !json_exact_int(item, target)) {
            cJSON_Delete(json);
            return 0;
        }
        seen |= field;
    }
    cJSON_Delete(json);
    const unsigned required = (1u << 0) | (1u << 1) | (1u << 3) | (1u << 4);
    *has_year = (seen & (1u << 2)) != 0;
    *has_offset = (seen & (1u << 5)) != 0;
    return (seen & required) == required &&
           *hour >= 0 && *hour <= 23 && *minute >= 0 && *minute <= 59 &&
           *month >= 1 && *month <= 12 && *day >= 1 && *day <= 31 &&
           (!*has_year ||
            valid_time_date(*hour, *minute, *year, *month, *day)) &&
           *second >= 0 && *second <= 59 &&
           (!*has_offset || pj_time_utc_offset_valid(*offset_minutes));
}

static esp_err_t time_put_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    char body[192];
    int hour = 0;
    int minute = 0;
    int second = 0;
    int year = 0;
    int month = 0;
    int day = 0;
    int has_year = 0;
    int has_offset = 0;
    int offset_minutes = 0;
    if (!read_body(req, body, sizeof(body))) {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"expected hour/minute/month/day integers; optional second, year, and utc_offset_minutes\"}");
    }
    if (!time_parse_update(body, &hour, &minute, &second,
                           &year, &month, &day,
                           &has_year,
                           &has_offset, &offset_minutes)) {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"expected hour/minute/month/day integers; optional second, year, and utc_offset_minutes\"}");
    }
    board_time_update_result_t result = board_set_time_date(
        hour, minute, second, &year, has_year, month, day, has_offset,
        offset_minutes, "HTTP");
    if (result.status != BOARD_TIME_UPDATE_OK) {
        return time_send_update_error(req, &result);
    }
    board_time_snapshot_t time = {
        .hour = hour,
        .minute = minute,
        .second = second,
        .year = year,
        .month = month,
        .day = day,
        .time_set = 1,
    };
    return time_send_response(req, &time, has_offset, offset_minutes);
}

static esp_err_t settings_get_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    pj_settings_t settings;
    uint32_t generation = 0;
    cJSON *json = cJSON_CreateObject();
    if (json == NULL || !settings_snapshot(&settings, &generation) ||
        !settings_add_json(json, &settings, generation)) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"settings unavailable\"}");
    }
    char *encoded = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (encoded == NULL) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"settings encoding failed\"}");
    }
    esp_err_t result = send_json(req, encoded);
    cJSON_free(encoded);
    return result;
}

static int json_exact_u32(const cJSON *item, uint32_t *value)
{
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
        item->valuedouble > (double)UINT32_MAX ||
        item->valuedouble != (double)(uint32_t)item->valuedouble) {
        return 0;
    }
    *value = (uint32_t)item->valuedouble;
    return 1;
}

static int settings_parse_update(const cJSON *json, pj_settings_t *settings,
                                 uint32_t *expected_generation,
                                 int *has_expected_generation)
{
    if (!cJSON_IsObject(json) || json->child == NULL || settings == NULL ||
        expected_generation == NULL || has_expected_generation == NULL) {
        return 0;
    }
    int setting_count = 0;
    *has_expected_generation = 0;
    for (const cJSON *item = json->child; item != NULL; item = item->next) {
        const char *key = item->string;
        if (key == NULL) {
            return 0;
        }
        for (const cJSON *previous = json->child; previous != item;
             previous = previous->next) {
            if (previous->string != NULL && strcmp(previous->string, key) == 0) {
                return 0;
            }
        }
        if (strcmp(key, "expected_generation") == 0) {
            if (*has_expected_generation ||
                !json_exact_u32(item, expected_generation)) {
                return 0;
            }
            *has_expected_generation = 1;
        } else if (strcmp(key, "theme") == 0) {
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
            setting_count++;
        } else if (strcmp(key, "alarm_enabled") == 0) {
            if (!cJSON_IsBool(item)) {
                return 0;
            }
            settings->alarm_enabled = cJSON_IsTrue(item) ? 1 : 0;
            setting_count++;
        } else if (strcmp(key, "volume") == 0) {
            if (!json_exact_int(item, &settings->volume)) {
                return 0;
            }
            setting_count++;
        } else if (strcmp(key, "alarm_hour") == 0) {
            if (!json_exact_int(item, &settings->alarm_hour)) {
                return 0;
            }
            setting_count++;
        } else if (strcmp(key, "alarm_minute") == 0) {
            if (!json_exact_int(item, &settings->alarm_minute)) {
                return 0;
            }
            setting_count++;
        } else if (strcmp(key, "timer_seconds") == 0) {
            if (!json_exact_int(item, &settings->timer_seconds)) {
                return 0;
            }
            setting_count++;
        } else if (strcmp(key, "interval_seconds") == 0) {
            if (!json_exact_int(item, &settings->interval_seconds)) {
                return 0;
            }
            setting_count++;
        } else if (strcmp(key, "clock_24h") == 0) {
            if (!cJSON_IsBool(item)) {
                return 0;
            }
            settings->clock_24h = cJSON_IsTrue(item) ? 1 : 0;
            setting_count++;
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
            setting_count++;
        } else if (strcmp(key, "transcript_font_size") == 0) {
            if (!json_exact_int(item, &settings->transcript_font_size)) {
                return 0;
            }
            setting_count++;
        } else {
            return 0;
        }
    }
    return setting_count > 0 && pj_settings_valid(settings);
}

static pj_settings_commit_result_t settings_commit_update(
    const cJSON *json, int require_expected_generation,
    uint32_t forced_expected_generation, int use_forced_expected_generation,
    pj_settings_t *committed, uint32_t *generation, int *changed)
{
    if (committed == NULL || generation == NULL || changed == NULL ||
        !settings_take(portMAX_DELAY)) {
        return PJ_SETTINGS_COMMIT_BUSY;
    }
    pj_settings_t updated = g_settings;
    uint32_t expected_generation = 0;
    int has_expected_generation = 0;
    if (!settings_parse_update(json, &updated, &expected_generation,
                               &has_expected_generation) ||
        (use_forced_expected_generation && has_expected_generation &&
         expected_generation != forced_expected_generation)) {
        settings_give();
        return PJ_SETTINGS_COMMIT_INVALID;
    }
    if (use_forced_expected_generation) {
        expected_generation = forced_expected_generation;
        has_expected_generation = 1;
    }
    if ((require_expected_generation && !has_expected_generation) ||
        (has_expected_generation &&
         expected_generation != g_settings_store.generation)) {
        settings_give();
        return PJ_SETTINGS_COMMIT_CONFLICT;
    }
    *changed = memcmp(&updated, &g_settings, sizeof(updated)) != 0;
    if (*changed) {
        esp_err_t err = settings_save(&updated);
        if (err != ESP_OK) {
            settings_give();
            return PJ_SETTINGS_COMMIT_STORE_FAILED;
        }
        g_settings = updated;
        board_update_publish(BOARD_UPDATE_SETTINGS);
    }
    *committed = g_settings;
    *generation = g_settings_store.generation;
    int codec_volume = *changed ? pj_settings_codec_volume(updated.volume) : -1;
    settings_give();
    if (*changed) {
        settings_apply_codec_volume(codec_volume);
        alert_audio_set_volume(codec_volume);
    }
    return PJ_SETTINGS_COMMIT_OK;
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
    pj_settings_t committed;
    uint32_t generation = 0;
    int changed = 0;
    pj_settings_commit_result_t result = settings_commit_update(
        json, 0, 0, 0, &committed, &generation, &changed);
    cJSON_Delete(json);
    if (result == PJ_SETTINGS_COMMIT_INVALID) {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"unsupported or invalid settings\",\"code\":\"invalid_settings\",\"retryable\":false}");
    }
    if (result == PJ_SETTINGS_COMMIT_CONFLICT) {
        httpd_resp_set_status(req, "409 Conflict");
        return send_json(req, "{\"error\":\"settings generation conflict\",\"code\":\"generation_conflict\",\"retryable\":false}");
    }
    if (result == PJ_SETTINGS_COMMIT_BUSY) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"settings busy\",\"code\":\"settings_busy\",\"retryable\":true}");
    }
    if (result == PJ_SETTINGS_COMMIT_STORE_FAILED) {
        ESP_LOGW(TAG, "HTTP settings save failed");
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_json(req, "{\"error\":\"settings store failed\",\"code\":\"settings_store_failed\",\"retryable\":true}");
    }
    cJSON *response = cJSON_CreateObject();
    if (response == NULL || !settings_add_json(response, &committed, generation) ||
        cJSON_AddBoolToObject(response, "changed", changed != 0) == NULL) {
        cJSON_Delete(response);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"settings response allocation failed\"}");
    }
    char *encoded = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (encoded == NULL) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"settings response encoding failed\"}");
    }
    esp_err_t send_result = send_json(req, encoded);
    cJSON_free(encoded);
    return send_result;
}

static int file_sha256_hex_unlocked(const char *path, char out[65])
{
    uint8_t buffer[1024];
    uint8_t digest[PSA_HASH_LENGTH(PSA_ALG_SHA_256)];
    size_t digest_length = 0;
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    int valid = 0;
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

static int audio_sha256_hex_unlocked(const char *filename, char out[65])
{
    char path[160];
    (void)snprintf(path, sizeof(path), PJ_AUDIO_DIR "/%s", filename);
    return file_sha256_hex_unlocked(path, out);
}

static int audio_sha256_hex(const char *filename, char out[65])
{
    if (!storage_shared_try_acquire()) {
        return 0;
    }
    int valid = audio_sha256_hex_unlocked(filename, out);
    storage_shared_release();
    return valid;
}

static esp_err_t audio_list_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    pj_board_status_t status = board_status_snapshot_base();
    if (status.storage != PJ_BOARD_SERVICE_READY) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"storage unavailable; run storage recovery\"}");
    }
    pj_audio_entry_t *entries = NULL;
    int count = collect_audio_entries(&entries);
    if (count < 0) {
        free(entries);
        if (count == PJ_AUDIO_COLLECT_TOO_MANY) {
            httpd_resp_set_status(req, "507 Insufficient Storage");
            return send_json(req,
                             "{\"error\":\"audio library exceeds device index limit\","
                             "\"code\":\"too_many_audio_files\",\"retryable\":false}");
        }
        if (count == PJ_AUDIO_COLLECT_NO_MEMORY) {
            httpd_resp_set_status(req, "503 Service Unavailable");
            return send_json(req,
                             "{\"error\":\"audio list out of memory\","
                             "\"code\":\"out_of_memory\",\"retryable\":true}");
        }
        httpd_resp_set_status(req, "409 Conflict");
        return send_json(req,
                         "{\"error\":\"storage maintenance active\","
                         "\"code\":\"storage_busy\",\"retryable\":true}");
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t start_err = httpd_resp_sendstr_chunk(req, "{\"audio\":[");
    if (start_err != ESP_OK) {
        free(entries);
        return start_err;
    }
    int emitted = 0;
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
    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, "]}"), TAG, "audio list end send failed");
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t audio_delete_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    pj_wipe_status_t status = {0};
    pj_wipe_start_result_t start =
        recording_wipe_start(NULL, &status, PJ_WIPE_WORKER_RELEASE_NOW);
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"wipe response allocation failed\"}");
    }
    if (!runtime_identity_add_json(json)) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"wipe response allocation failed\"}");
    }
    if (start == PJ_WIPE_START_STARTED || start == PJ_WIPE_START_ATTACHED) {
        httpd_resp_set_status(req, "202 Accepted");
        cJSON_AddBoolToObject(json, "accepted", 1);
        cJSON_AddBoolToObject(json, "attached", start == PJ_WIPE_START_ATTACHED);
        (void)wipe_status_add_json(json, &status);
    } else {
        const char *message = "storage unavailable";
        const char *code = "storage_unavailable";
        if (start == PJ_WIPE_START_AUDIO_ACTIVE) {
            message = "audio task active";
            code = "audio_active";
            httpd_resp_set_status(req, "409 Conflict");
        } else if (start == PJ_WIPE_START_STORAGE_BUSY) {
            message = "storage busy";
            code = "storage_busy";
            httpd_resp_set_status(req, "409 Conflict");
        } else {
            httpd_resp_set_status(req, "503 Service Unavailable");
            if (start == PJ_WIPE_START_TASK_FAILED) {
                message = "recording wipe task start failed";
                code = "wipe_task_start_failed";
            }
        }
        cJSON_AddStringToObject(json, "error", message);
        cJSON_AddStringToObject(json, "code", code);
        cJSON_AddBoolToObject(json, "retryable", 1);
        if (status.id != 0U) {
            (void)wipe_status_add_json(json, &status);
        }
    }
    esp_err_t result = send_json_object(req, json);
    cJSON_Delete(json);
    return result;
}

static esp_err_t audio_download_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    pj_board_status_t status = board_status_snapshot_base();
    if (status.storage != PJ_BOARD_SERVICE_READY) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"storage unavailable; run storage recovery\"}");
    }
    const char *id = strrchr(req->uri, '/');
    if (id == NULL || id[1] == '\0' ||
        memchr(id + 1, '\0', PJ_NOTE_FILENAME_LEN) == NULL ||
        strstr(id + 1, "..") != NULL || strchr(id + 1, '/') != NULL) {
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"invalid audio id\"}");
    }
    char path[160];
    (void)snprintf(path, sizeof(path), PJ_AUDIO_DIR "/%s", id + 1);
    if (!storage_shared_try_acquire()) {
        httpd_resp_set_status(req, "409 Conflict");
        return send_json(req,
                         "{\"error\":\"storage maintenance active\","
                         "\"code\":\"storage_busy\",\"retryable\":true}");
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        storage_shared_release();
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
                storage_shared_release();
                return err;
            }
        }
        if (read < sizeof(chunk)) {
            break;
        }
    }
    fclose(file);
    storage_shared_release();
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t transcript_put_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    pj_board_status_t status = board_status_snapshot_base();
    if (status.storage != PJ_BOARD_SERVICE_READY) {
        drain_body(req);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"storage unavailable; run storage recovery\"}");
    }
    const char *id = strrchr(req->uri, '/');
    if (id == NULL || id[1] == '\0' ||
        memchr(id + 1, '\0', PJ_NOTE_FILENAME_LEN) == NULL ||
        strstr(id + 1, "..") != NULL || strchr(id + 1, '/') != NULL ||
        strchr(id + 1, '\\') != NULL) {
        httpd_resp_set_status(req, "400 Bad Request");
        drain_body(req);
        return send_json(req, "{\"error\":\"invalid transcript id\"}");
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
    if (!storage_shared_try_acquire()) {
        free(body);
        httpd_resp_set_status(req, "409 Conflict");
        return send_json(req,
                         "{\"error\":\"storage maintenance active\","
                         "\"code\":\"storage_busy\",\"retryable\":true}");
    }
    pj_audio_entry_t entry;
    if (!probe_audio_entry_unlocked(id + 1, &entry)) {
        storage_shared_release();
        free(body);
        httpd_resp_set_status(req, "404 Not Found");
        return send_json(req, "{\"error\":\"audio not found\"}");
    }
    pj_transcript_source_result_t source_result;
    if (!transcript_source_check_audio_unlocked(
            body, (size_t)req->content_len, &entry, &source_result)) {
        storage_shared_release();
        free(body);
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_json(req,
                         "{\"error\":\"audio checksum could not be read\","
                         "\"code\":\"storage_io\",\"retryable\":true}");
    }
    pj_transcript_commit_source_decision_t source_decision =
        pj_transcript_source_commit_decision(source_result);
    if (source_decision == PJ_TRANSCRIPT_COMMIT_SOURCE_RETRY_CHANGED) {
        storage_shared_release();
        free(body);
        httpd_resp_set_status(req, "409 Conflict");
        return send_json(req,
                         "{\"error\":\"audio source changed; download and retry\","
                         "\"code\":\"source_changed\",\"retryable\":true}");
    }
    if (source_decision == PJ_TRANSCRIPT_COMMIT_SOURCE_REJECT_INVALID) {
        storage_shared_release();
        free(body);
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req,
                         "{\"error\":\"invalid transcript source provenance\","
                         "\"code\":\"invalid_transcript\",\"retryable\":false}");
    }
    char path[PJ_NOTE_TRANSCRIPT_PATH_LEN];
    if (!transcript_path_for_audio(path, sizeof(path), id + 1)) {
        storage_shared_release();
        free(body);
        httpd_resp_set_status(req, "400 Bad Request");
        return send_json(req, "{\"error\":\"invalid transcript id\"}");
    }
    esp_err_t write_err = json_write_file_atomic(path, body, (size_t)req->content_len);
    free(body);
    status = board_status_snapshot_base();
    if (write_err == ESP_ERR_NO_MEM &&
        status.storage_health == PJ_STORAGE_HEALTH_FULL) {
        storage_shared_release();
        httpd_resp_set_status(req, "507 Insufficient Storage");
        return send_json(req, "{\"error\":\"insufficient storage for transcript\"}");
    }
    if (write_err == ESP_ERR_INVALID_STATE) {
        storage_shared_release();
        httpd_resp_set_status(req, "503 Service Unavailable");
        return send_json(req, "{\"error\":\"storage unavailable; run storage recovery\"}");
    }
    if (write_err != ESP_OK) {
        storage_shared_release();
        ESP_LOGW(TAG, "Transcript store failed: %s", path);
        httpd_resp_set_status(req, "500 Internal Server Error");
        return send_json(req, "{\"error\":\"transcript store failed\"}");
    }
    if (!probe_audio_entry_unlocked(id + 1, &entry)) {
        ESP_LOGW(TAG, "Transcript stored but note metadata refresh failed: %s", id + 1);
    }
    storage_shared_release();
    board_update_publish(BOARD_UPDATE_NOTES);
    ESP_LOGI(TAG, "Transcript stored and note marked synced: %s", path);
    return send_json(req, "{\"uploaded\":true}");
}

static esp_err_t storage_recover_handler(httpd_req_t *req)
{
    if (require_auth(req) != ESP_OK) {
        return ESP_OK;
    }
    if (audio_lifecycle_active()) {
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
        board_status_set_service(BOARD_SERVICE_HTTP, PJ_BOARD_SERVICE_ERROR);
        board_status_set_error("network stack init failed: %s",
                               esp_err_to_name(err));
        ESP_LOGE(TAG, "Network stack init failed: %s", esp_err_to_name(err));
        return 0;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = PJ_HTTP_MAX_URI_HANDLERS;
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;
    err = httpd_start(&g_http_server, &config);
    if (err != ESP_OK) {
        board_status_set_service(BOARD_SERVICE_HTTP, PJ_BOARD_SERVICE_ERROR);
        board_status_set_error("HTTP start failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "HTTP start failed: %s", esp_err_to_name(err));
        return 0;
    }

#define REGISTER_URI_OR_FAIL(uri, method, handler) do { \
        err = register_uri(g_http_server, uri, method, handler); \
        if (err != ESP_OK) { \
            board_status_set_service(BOARD_SERVICE_HTTP, PJ_BOARD_SERVICE_ERROR); \
            board_status_set_error("HTTP route registration failed for %s: %s", \
                                   uri, esp_err_to_name(err)); \
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
    REGISTER_URI_OR_FAIL("/v1/audio", HTTP_GET, audio_list_handler);
    REGISTER_URI_OR_FAIL("/v1/audio", HTTP_DELETE, audio_delete_handler);
    REGISTER_URI_OR_FAIL("/v1/audio/*", HTTP_GET, audio_download_handler);
    REGISTER_URI_OR_FAIL("/v1/transcripts/*", HTTP_PUT, transcript_put_handler);
    err = pj_ota_register_http(g_http_server);
    if (err != ESP_OK) {
        board_status_set_service(BOARD_SERVICE_HTTP, PJ_BOARD_SERVICE_ERROR);
        board_status_set_error("OTA HTTP route registration failed: %s",
                               esp_err_to_name(err));
        httpd_stop(g_http_server);
        g_http_server = NULL;
        return 0;
    }
#undef REGISTER_URI_OR_FAIL

    board_status_set_service(BOARD_SERVICE_HTTP, PJ_BOARD_SERVICE_READY);
    ESP_LOGI(TAG, "HTTP API started with bearer authentication enabled");
    return 1;
#else
    g_status.http = PJ_BOARD_SERVICE_UNAVAILABLE;
    return 0;
#endif
}
