#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int volume;
    int dark_mode;
    int alarm_enabled;
    int alarm_hour;
    int alarm_minute;
    int timer_seconds;
    int interval_seconds;
    int clock_24h;
    int temperature_fahrenheit;
    int transcript_font_size;
} pj_settings_t;

#define PJ_SETTINGS_RECORD_BYTES 56u
#define PJ_SETTINGS_SLOT_COUNT 2u

typedef enum {
    PJ_SETTINGS_IO_ERROR = -1,
    PJ_SETTINGS_IO_NOT_FOUND = 0,
    PJ_SETTINGS_IO_OK = 1,
} pj_settings_io_result_t;

typedef pj_settings_io_result_t (*pj_settings_read_slot_fn)(
    void *context, unsigned slot, uint8_t *record, size_t *record_size);
typedef pj_settings_io_result_t (*pj_settings_write_slot_fn)(
    void *context, unsigned slot, const uint8_t *record, size_t record_size);

typedef struct {
    void *context;
    pj_settings_read_slot_fn read_slot;
    pj_settings_write_slot_fn write_slot;
    uint32_t generation;
    int active_slot;
    int has_record;
    int degraded;
} pj_settings_store_t;

typedef enum {
    PJ_SETTINGS_LOAD_ERROR = -1,
    PJ_SETTINGS_LOAD_NOT_FOUND = 0,
    PJ_SETTINGS_LOAD_OK = 1,
} pj_settings_load_result_t;

void pj_settings_defaults(pj_settings_t *settings);
int pj_settings_valid(const pj_settings_t *settings);
int pj_settings_codec_volume(int volume);
size_t pj_settings_encode_record(const pj_settings_t *settings,
                                 uint32_t generation, uint8_t *record,
                                 size_t record_size);
int pj_settings_decode_record(const uint8_t *record, size_t record_size,
                              pj_settings_t *settings, uint32_t *generation);
void pj_settings_store_init(pj_settings_store_t *store, void *context,
                            pj_settings_read_slot_fn read_slot,
                            pj_settings_write_slot_fn write_slot);
pj_settings_load_result_t pj_settings_store_load(pj_settings_store_t *store,
                                                 pj_settings_t *settings);
int pj_settings_store_save(pj_settings_store_t *store,
                           const pj_settings_t *settings);

#ifdef __cplusplus
}
#endif
