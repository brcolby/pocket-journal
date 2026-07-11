#include "pj_storage.h"

#include <string.h>

static int has_suffix(const char *value, const char *suffix)
{
    if (value == NULL || suffix == NULL) {
        return 0;
    }
    size_t value_len = strlen(value);
    size_t suffix_len = strlen(suffix);
    return value_len >= suffix_len && strcmp(value + value_len - suffix_len, suffix) == 0;
}

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

const char *pj_storage_health_name(pj_storage_health_t health)
{
    switch (health) {
    case PJ_STORAGE_HEALTH_HEALTHY:
        return "healthy";
    case PJ_STORAGE_HEALTH_LOW_SPACE:
        return "low_space";
    case PJ_STORAGE_HEALTH_FULL:
        return "full";
    case PJ_STORAGE_HEALTH_IO_ERROR:
        return "io_error";
    case PJ_STORAGE_HEALTH_UNMOUNTED:
    default:
        return "unmounted";
    }
}

int pj_storage_can_write(uint64_t free_bytes, uint64_t write_bytes, uint64_t reserve_bytes)
{
    return free_bytes >= reserve_bytes && write_bytes <= free_bytes - reserve_bytes;
}

pj_storage_health_t pj_storage_capacity_health(int mounted, int capacity_known,
                                               uint64_t total_bytes, uint64_t free_bytes,
                                               uint64_t reserve_bytes)
{
    if (!mounted) {
        return PJ_STORAGE_HEALTH_UNMOUNTED;
    }
    if (!capacity_known || total_bytes == 0 || free_bytes > total_bytes) {
        return PJ_STORAGE_HEALTH_IO_ERROR;
    }
    if (free_bytes <= reserve_bytes) {
        return PJ_STORAGE_HEALTH_FULL;
    }
    if (free_bytes - reserve_bytes <= reserve_bytes) {
        return PJ_STORAGE_HEALTH_LOW_SPACE;
    }
    return PJ_STORAGE_HEALTH_HEALTHY;
}

pj_storage_recovery_action_t pj_storage_recovery_action(const char *filename, int target_exists)
{
    if (filename == NULL || filename[0] == '\0') {
        return PJ_STORAGE_RECOVERY_IGNORE;
    }
    if (has_suffix(filename, ".wav.tmp") || has_suffix(filename, ".json.tmp")) {
        return PJ_STORAGE_RECOVERY_DELETE_TEMP;
    }
    if (has_suffix(filename, ".json.bak")) {
        return target_exists ? PJ_STORAGE_RECOVERY_DELETE_BACKUP : PJ_STORAGE_RECOVERY_RESTORE_BACKUP;
    }
    return PJ_STORAGE_RECOVERY_IGNORE;
}

int pj_storage_wav_validate(const uint8_t *header, size_t header_size, uint64_t file_size,
                            uint32_t expected_sample_rate, uint16_t expected_bits,
                            pj_storage_wav_info_t *info)
{
    if (header == NULL || info == NULL || header_size < 44U || file_size < 44U ||
        memcmp(header, "RIFF", 4) != 0 || memcmp(&header[8], "WAVE", 4) != 0 ||
        memcmp(&header[12], "fmt ", 4) != 0 || read_le32(&header[16]) != 16U ||
        read_le16(&header[20]) != 1U || memcmp(&header[36], "data", 4) != 0) {
        return 0;
    }

    uint32_t data_bytes = read_le32(&header[40]);
    uint32_t sample_rate = read_le32(&header[24]);
    uint16_t channels = read_le16(&header[22]);
    uint16_t bits = read_le16(&header[34]);
    uint16_t block_align = read_le16(&header[32]);
    uint64_t required_size = 44U + (uint64_t)data_bytes;
    uint16_t expected_align = (uint16_t)(channels * (bits / 8U));

    if (data_bytes == 0 || sample_rate != expected_sample_rate || bits != expected_bits ||
        (channels != 1U && channels != 2U) || expected_align == 0U ||
        block_align != expected_align || data_bytes % block_align != 0U ||
        required_size != file_size || read_le32(&header[4]) + 8ULL != file_size) {
        return 0;
    }

    info->data_bytes = data_bytes;
    info->sample_rate = sample_rate;
    info->channels = channels;
    info->bits_per_sample = bits;
    return 1;
}
