#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PJ_STORAGE_HEALTH_UNMOUNTED = 0,
    PJ_STORAGE_HEALTH_HEALTHY,
    PJ_STORAGE_HEALTH_LOW_SPACE,
    PJ_STORAGE_HEALTH_FULL,
    PJ_STORAGE_HEALTH_IO_ERROR,
} pj_storage_health_t;

typedef enum {
    PJ_STORAGE_RECOVERY_IGNORE = 0,
    PJ_STORAGE_RECOVERY_DELETE_TEMP,
    PJ_STORAGE_RECOVERY_DELETE_BACKUP,
    PJ_STORAGE_RECOVERY_RESTORE_BACKUP,
} pj_storage_recovery_action_t;

typedef struct {
    uint32_t data_bytes;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
} pj_storage_wav_info_t;

#define PJ_STORAGE_WAV_HEADER_BYTES 44U

const char *pj_storage_health_name(pj_storage_health_t health);
pj_storage_health_t pj_storage_capacity_health(int mounted, int capacity_known,
                                               uint64_t total_bytes, uint64_t free_bytes,
                                               uint64_t reserve_bytes);
int pj_storage_can_write(uint64_t free_bytes, uint64_t write_bytes, uint64_t reserve_bytes);
pj_storage_recovery_action_t pj_storage_recovery_action(const char *filename, int target_exists);
int pj_storage_wav_encode_header(uint8_t *header, size_t header_size,
                                 uint32_t data_bytes, uint32_t sample_rate,
                                 uint16_t channels, uint16_t bits_per_sample);
int pj_storage_wav_validate(const uint8_t *header, size_t header_size, uint64_t file_size,
                            uint32_t expected_sample_rate, uint16_t expected_bits,
                            pj_storage_wav_info_t *info);

#ifdef __cplusplus
}
#endif
