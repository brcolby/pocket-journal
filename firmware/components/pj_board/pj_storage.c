#include "pj_storage.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(__GNUC__)
#define PJ_STORAGE_WEAK __attribute__((weak))
#else
#define PJ_STORAGE_WEAK
#endif

PJ_STORAGE_WEAK int pj_storage_fs_remove(const char *path)
{
    return remove(path);
}

PJ_STORAGE_WEAK int pj_storage_fs_rename(const char *old_path,
                                         const char *new_path)
{
    return rename(old_path, new_path);
}

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

static void write_le16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
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
    if (has_suffix(filename, ".wav.bak")) {
        return PJ_STORAGE_RECOVERY_VALIDATE_BACKUP;
    }
    if (has_suffix(filename, ".json.bak")) {
        return target_exists ? PJ_STORAGE_RECOVERY_DELETE_BACKUP : PJ_STORAGE_RECOVERY_RESTORE_BACKUP;
    }
    return PJ_STORAGE_RECOVERY_IGNORE;
}

pj_storage_backup_recovery_result_t pj_storage_recover_backup(
    const char *backup_path, const char *target_path,
    pj_storage_path_validator_t validate, void *validate_context)
{
    if (backup_path == NULL || target_path == NULL || validate == NULL ||
        backup_path[0] == '\0' || target_path[0] == '\0' ||
        strcmp(backup_path, target_path) == 0) {
        return PJ_STORAGE_BACKUP_RECOVERY_FAILED;
    }

    struct stat backup_stat;
    if (stat(backup_path, &backup_stat) != 0) {
        return errno == ENOENT ? PJ_STORAGE_BACKUP_RECOVERY_NONE :
                                PJ_STORAGE_BACKUP_RECOVERY_FAILED;
    }

    int backup_validity = validate(backup_path, validate_context);
    int target_validity = validate(target_path, validate_context);
    if (backup_validity < 0 || target_validity < 0) {
        return PJ_STORAGE_BACKUP_RECOVERY_FAILED;
    }
    if (backup_validity > 0) {
        if (pj_storage_fs_remove(target_path) != 0 && errno != ENOENT) {
            return PJ_STORAGE_BACKUP_RECOVERY_FAILED;
        }
        if (pj_storage_fs_rename(backup_path, target_path) != 0) {
            return PJ_STORAGE_BACKUP_RECOVERY_FAILED;
        }
        int restored_validity = validate(target_path, validate_context);
        if (restored_validity <= 0) {
            (void)pj_storage_fs_rename(target_path, backup_path);
            return PJ_STORAGE_BACKUP_RECOVERY_FAILED;
        }
        return PJ_STORAGE_BACKUP_RECOVERY_RESTORED;
    }

    if (target_validity > 0) {
        return pj_storage_fs_remove(backup_path) == 0 || errno == ENOENT ?
                   PJ_STORAGE_BACKUP_RECOVERY_REMOVED :
                   PJ_STORAGE_BACKUP_RECOVERY_FAILED;
    }
    return PJ_STORAGE_BACKUP_RECOVERY_NONE;
}

pj_storage_delete_result_t pj_storage_delete_matching(const char *dir_path,
                                                       pj_storage_name_match_fn matches,
                                                       size_t max_entries)
{
    pj_storage_delete_result_t result = {0};
    char **paths = NULL;

    if (dir_path == NULL || dir_path[0] == '\0' || matches == NULL) {
        result.open_errno = EINVAL;
        return result;
    }
    if (max_entries > SIZE_MAX / sizeof(paths[0])) {
        result.allocation_errno = ENOMEM;
        return result;
    }
    if (max_entries > 0U) {
        paths = calloc(max_entries, sizeof(paths[0]));
        if (paths == NULL) {
            result.allocation_errno = ENOMEM;
            return result;
        }
    }

    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        result.open_errno = errno != 0 ? errno : EIO;
        free(paths);
        return result;
    }

    const size_t dir_len = strlen(dir_path);
    struct dirent *entry;
    for (;;) {
        errno = 0;
        entry = readdir(dir);
        if (entry == NULL) {
            if (errno != 0) {
                result.scan_errno = errno;
            }
            break;
        }
        if (entry->d_name[0] == '.' || !matches(entry->d_name)) {
            continue;
        }
        result.matched++;
        if (result.snapshotted >= max_entries) {
            result.truncated++;
            break;
        }

        const size_t name_len = strlen(entry->d_name);
        if (name_len > SIZE_MAX - 2U || dir_len > SIZE_MAX - name_len - 2U) {
            result.allocation_errno = EOVERFLOW;
            result.truncated++;
            break;
        }
        const size_t path_size = dir_len + 1U + name_len + 1U;
        char *path = malloc(path_size);
        if (path == NULL) {
            result.allocation_errno = ENOMEM;
            result.truncated++;
            break;
        }
        memcpy(path, dir_path, dir_len);
        path[dir_len] = '/';
        memcpy(path + dir_len + 1U, entry->d_name, name_len + 1U);
        paths[result.snapshotted++] = path;
    }
    errno = 0;
    if (closedir(dir) != 0) {
        result.close_errno = errno != 0 ? errno : EIO;
    }

    if (result.close_errno == 0) {
        for (size_t i = 0; i < result.snapshotted; i++) {
            errno = 0;
            if (pj_storage_fs_remove(paths[i]) == 0) {
                result.deleted++;
                continue;
            }
            result.remove_failures++;
            if (result.first_remove_errno == 0) {
                result.first_remove_errno = errno != 0 ? errno : EIO;
            }
        }
    }
    for (size_t i = 0; i < result.snapshotted; i++) {
        free(paths[i]);
    }
    free(paths);
    return result;
}

int pj_storage_wav_encode_header(uint8_t *header, size_t header_size,
                                 uint32_t data_bytes, uint32_t sample_rate,
                                 uint16_t channels, uint16_t bits_per_sample)
{
    if (header == NULL || header_size < PJ_STORAGE_WAV_HEADER_BYTES ||
        sample_rate == 0 || (channels != 1U && channels != 2U) ||
        bits_per_sample == 0 || bits_per_sample % 8U != 0 ||
        data_bytes > UINT32_MAX - 36U) {
        return 0;
    }
    uint32_t bytes_per_sample = bits_per_sample / 8U;
    uint32_t block_align = channels * bytes_per_sample;
    uint64_t byte_rate = (uint64_t)sample_rate * block_align;
    if (block_align == 0 || block_align > UINT16_MAX ||
        byte_rate > UINT32_MAX || data_bytes % block_align != 0) {
        return 0;
    }

    memset(header, 0, PJ_STORAGE_WAV_HEADER_BYTES);
    memcpy(header, "RIFF", 4);
    write_le32(&header[4], 36U + data_bytes);
    memcpy(&header[8], "WAVEfmt ", 8);
    write_le32(&header[16], 16U);
    write_le16(&header[20], 1U);
    write_le16(&header[22], channels);
    write_le32(&header[24], sample_rate);
    write_le32(&header[28], (uint32_t)byte_rate);
    write_le16(&header[32], (uint16_t)block_align);
    write_le16(&header[34], bits_per_sample);
    memcpy(&header[36], "data", 4);
    write_le32(&header[40], data_bytes);
    return 1;
}

int pj_storage_wav_validate(const uint8_t *header, size_t header_size, uint64_t file_size,
                            uint32_t expected_sample_rate, uint16_t expected_bits,
                            pj_storage_wav_info_t *info)
{
    if (header == NULL || info == NULL ||
        header_size < PJ_STORAGE_WAV_HEADER_BYTES ||
        file_size < PJ_STORAGE_WAV_HEADER_BYTES ||
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
    uint64_t required_size = PJ_STORAGE_WAV_HEADER_BYTES + (uint64_t)data_bytes;
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
