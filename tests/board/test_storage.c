#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include "pj_storage.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_PATH_BYTES 512U

static void put_le16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void valid_header(uint8_t header[44], uint32_t data_bytes)
{
    assert(pj_storage_wav_encode_header(header, 44, data_bytes, 16000, 1, 16));
}

static int matches_wav(const char *name)
{
    const size_t len = strlen(name);
    return len >= 4U && strcmp(name + len - 4U, ".wav") == 0;
}

static void make_temp_dir(char path[TEST_PATH_BYTES])
{
    const int length = snprintf(path, TEST_PATH_BYTES, "/tmp/pj-storage-XXXXXX");
    assert(length > 0 && (size_t)length < TEST_PATH_BYTES);
    assert(mkdtemp(path) != NULL);
}

static void join_path(char out[TEST_PATH_BYTES], const char *dir, const char *name)
{
    const int length = snprintf(out, TEST_PATH_BYTES, "%s/%s", dir, name);
    assert(length > 0 && (size_t)length < TEST_PATH_BYTES);
}

static void create_file(const char *path)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fputs("test", file) >= 0);
    assert(fclose(file) == 0);
}

static int path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static size_t count_matching(const char *dir_path, int (*matches)(const char *name))
{
    DIR *dir = opendir(dir_path);
    assert(dir != NULL);
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.' && matches(entry->d_name)) {
            count++;
        }
    }
    assert(closedir(dir) == 0);
    return count;
}

static void assert_no_delete_errors(pj_storage_delete_result_t result)
{
    assert(result.open_errno == 0);
    assert(result.scan_errno == 0);
    assert(result.close_errno == 0);
    assert(result.allocation_errno == 0);
    assert(result.remove_failures == 0);
    assert(result.first_remove_errno == 0);
}

static void test_delete_matching_empty(void)
{
    char dir[TEST_PATH_BYTES];
    make_temp_dir(dir);

    pj_storage_delete_result_t result = pj_storage_delete_matching(dir, matches_wav, 8U);
    assert_no_delete_errors(result);
    assert(result.matched == 0U);
    assert(result.snapshotted == 0U);
    assert(result.deleted == 0U);
    assert(result.truncated == 0U);
    assert(rmdir(dir) == 0);
}

static void test_delete_matching_filters_and_deletes_multiple(void)
{
    char dir[TEST_PATH_BYTES];
    char first[TEST_PATH_BYTES];
    char second[TEST_PATH_BYTES];
    char nonmatching[TEST_PATH_BYTES];
    char hidden[TEST_PATH_BYTES];
    make_temp_dir(dir);
    join_path(first, dir, "first.wav");
    join_path(second, dir, "second.wav");
    join_path(nonmatching, dir, "keep.json");
    join_path(hidden, dir, ".hidden.wav");
    create_file(first);
    create_file(second);
    create_file(nonmatching);
    create_file(hidden);

    pj_storage_delete_result_t result = pj_storage_delete_matching(dir, matches_wav, 8U);
    assert_no_delete_errors(result);
    assert(result.matched == 2U);
    assert(result.snapshotted == 2U);
    assert(result.deleted == 2U);
    assert(result.truncated == 0U);
    assert(!path_exists(first));
    assert(!path_exists(second));
    assert(path_exists(nonmatching));
    assert(path_exists(hidden));

    assert(unlink(nonmatching) == 0);
    assert(unlink(hidden) == 0);
    assert(rmdir(dir) == 0);
}

static void test_delete_matching_reports_truncation(void)
{
    char dir[TEST_PATH_BYTES];
    char path[TEST_PATH_BYTES];
    make_temp_dir(dir);
    join_path(path, dir, "first.wav");
    create_file(path);
    join_path(path, dir, "second.wav");
    create_file(path);
    join_path(path, dir, "third.wav");
    create_file(path);

    pj_storage_delete_result_t result = pj_storage_delete_matching(dir, matches_wav, 2U);
    assert_no_delete_errors(result);
    assert(result.matched == 3U);
    assert(result.snapshotted == 2U);
    assert(result.deleted == 2U);
    assert(result.truncated == 1U);
    assert(count_matching(dir, matches_wav) == 1U);

    result = pj_storage_delete_matching(dir, matches_wav, 2U);
    assert_no_delete_errors(result);
    assert(result.deleted == 1U);
    assert(rmdir(dir) == 0);
}

static void test_delete_matching_reports_remove_failure_once(void)
{
    char dir[TEST_PATH_BYTES];
    char removable[TEST_PATH_BYTES];
    char blocked[TEST_PATH_BYTES];
    char child[TEST_PATH_BYTES];
    make_temp_dir(dir);
    join_path(removable, dir, "removable.wav");
    join_path(blocked, dir, "blocked.wav");
    join_path(child, blocked, "child");
    create_file(removable);
    assert(mkdir(blocked, 0700) == 0);
    create_file(child);

    pj_storage_delete_result_t result = pj_storage_delete_matching(dir, matches_wav, 8U);
    assert(result.open_errno == 0);
    assert(result.scan_errno == 0);
    assert(result.close_errno == 0);
    assert(result.allocation_errno == 0);
    assert(result.matched == 2U);
    assert(result.snapshotted == 2U);
    assert(result.deleted == 1U);
    assert(result.remove_failures == 1U);
    assert(result.first_remove_errno != 0);
    assert(result.truncated == 0U);
    assert(!path_exists(removable));
    assert(path_exists(blocked));

    assert(unlink(child) == 0);
    assert(rmdir(blocked) == 0);
    assert(rmdir(dir) == 0);
}

static void test_delete_matching_reports_allocation_error(void)
{
    char dir[TEST_PATH_BYTES];
    char recording[TEST_PATH_BYTES];
    make_temp_dir(dir);
    join_path(recording, dir, "keep.wav");
    create_file(recording);

    pj_storage_delete_result_t result =
        pj_storage_delete_matching(dir, matches_wav, SIZE_MAX);
    assert(result.allocation_errno == ENOMEM);
    assert(result.deleted == 0U);
    assert(path_exists(recording));

    assert(unlink(recording) == 0);
    assert(rmdir(dir) == 0);
}

static void test_capacity_policy(void)
{
    const uint64_t reserve = 256U * 1024U;
    assert(!pj_storage_can_write(reserve, 1, reserve));
    assert(pj_storage_can_write(reserve + 1, 1, reserve));
    assert(!pj_storage_can_write(10, UINT64_MAX, 5));
    assert(pj_storage_can_write(UINT64_MAX, 0, UINT64_MAX));
    assert(pj_storage_capacity_health(0, 0, 0, 0, reserve) == PJ_STORAGE_HEALTH_UNMOUNTED);
    assert(pj_storage_capacity_health(1, 0, 0, 0, reserve) == PJ_STORAGE_HEALTH_IO_ERROR);
    assert(pj_storage_capacity_health(1, 1, 1000, 1001, reserve) == PJ_STORAGE_HEALTH_IO_ERROR);
    assert(pj_storage_capacity_health(1, 1, 1024 * 1024, reserve, reserve) == PJ_STORAGE_HEALTH_FULL);
    assert(pj_storage_capacity_health(1, 1, 1024 * 1024, reserve + 1, reserve) == PJ_STORAGE_HEALTH_LOW_SPACE);
    assert(pj_storage_capacity_health(1, 1, 1024 * 1024, reserve * 2 + 1, reserve) == PJ_STORAGE_HEALTH_HEALTHY);
    assert(strcmp(pj_storage_health_name(PJ_STORAGE_HEALTH_LOW_SPACE), "low_space") == 0);
}

static void test_recovery_policy(void)
{
    assert(pj_storage_recovery_action(NULL, 0) == PJ_STORAGE_RECOVERY_IGNORE);
    assert(pj_storage_recovery_action("", 0) == PJ_STORAGE_RECOVERY_IGNORE);
    assert(pj_storage_recovery_action("rec.wav.tmp", 0) == PJ_STORAGE_RECOVERY_DELETE_TEMP);
    assert(pj_storage_recovery_action("note.json.tmp", 0) == PJ_STORAGE_RECOVERY_DELETE_TEMP);
    assert(pj_storage_recovery_action("note.json.bak", 0) == PJ_STORAGE_RECOVERY_RESTORE_BACKUP);
    assert(pj_storage_recovery_action("note.json.bak", 1) == PJ_STORAGE_RECOVERY_DELETE_BACKUP);
    assert(pj_storage_recovery_action("valid.wav", 0) == PJ_STORAGE_RECOVERY_IGNORE);
    assert(pj_storage_recovery_action("similar.wav.TMP", 0) == PJ_STORAGE_RECOVERY_IGNORE);
}

static void test_wav_validation(void)
{
    uint8_t header[44];
    pj_storage_wav_info_t info;
    valid_header(header, 32000);
    assert(!pj_storage_wav_validate(NULL, sizeof(header), 32044, 16000, 16, &info));
    assert(!pj_storage_wav_validate(header, sizeof(header), 32044, 16000, 16, NULL));
    assert(pj_storage_wav_validate(header, sizeof(header), 32044, 16000, 16, &info));
    assert(info.data_bytes == 32000 && info.sample_rate == 16000 && info.channels == 1);

    assert(!pj_storage_wav_validate(header, 43, 32044, 16000, 16, &info));
    assert(!pj_storage_wav_validate(header, sizeof(header), 32043, 16000, 16, &info));
    assert(!pj_storage_wav_validate(header, sizeof(header), 32045, 16000, 16, &info));
    valid_header(header, 32000);
    put_le32(&header[4], 32037);
    assert(!pj_storage_wav_validate(header, sizeof(header), 32045, 16000, 16, &info));
    put_le32(&header[40], 32001);
    assert(!pj_storage_wav_validate(header, sizeof(header), 32045, 16000, 16, &info));
    valid_header(header, 32000);
    put_le32(&header[4], 35);
    assert(!pj_storage_wav_validate(header, sizeof(header), 32044, 16000, 16, &info));
    valid_header(header, 32000);
    put_le32(&header[4], 40000);
    assert(!pj_storage_wav_validate(header, sizeof(header), 32044, 16000, 16, &info));
    valid_header(header, 32000);
    put_le16(&header[22], 2);
    put_le16(&header[32], 4);
    assert(pj_storage_wav_validate(header, sizeof(header), 32044, 16000, 16, &info));
    assert(info.channels == 2);
    valid_header(header, 32000);
    put_le16(&header[20], 3);
    assert(!pj_storage_wav_validate(header, sizeof(header), 32044, 16000, 16, &info));
    valid_header(header, 32000);
    put_le32(&header[24], 48000);
    assert(!pj_storage_wav_validate(header, sizeof(header), 32044, 16000, 16, &info));
    valid_header(header, 32000);
    memcpy(header, "NOPE", 4);
    assert(!pj_storage_wav_validate(header, sizeof(header), 32044, 16000, 16, &info));
}

static void test_wav_header_encoding(void)
{
    uint8_t header[PJ_STORAGE_WAV_HEADER_BYTES];
    pj_storage_wav_info_t info;
    assert(pj_storage_wav_encode_header(header, sizeof(header), 64000, 16000, 2, 16));
    assert(pj_storage_wav_validate(header, sizeof(header), 64044, 16000, 16, &info));
    assert(info.data_bytes == 64000 && info.channels == 2);
    assert(!pj_storage_wav_encode_header(NULL, sizeof(header), 32000, 16000, 1, 16));
    assert(!pj_storage_wav_encode_header(header, sizeof(header) - 1, 32000, 16000, 1, 16));
    assert(!pj_storage_wav_encode_header(header, sizeof(header), 32001, 16000, 1, 16));
    assert(!pj_storage_wav_encode_header(header, sizeof(header), 32000, 0, 1, 16));
    assert(!pj_storage_wav_encode_header(header, sizeof(header), 32000, 16000, 3, 16));
    assert(!pj_storage_wav_encode_header(header, sizeof(header), UINT32_MAX, 16000, 1, 16));
}

int main(void)
{
    test_delete_matching_empty();
    test_delete_matching_filters_and_deletes_multiple();
    test_delete_matching_reports_truncation();
    test_delete_matching_reports_remove_failure_once();
    test_delete_matching_reports_allocation_error();
    test_capacity_policy();
    test_recovery_policy();
    test_wav_validation();
    test_wav_header_encoding();
    puts("storage tests passed");
    return 0;
}
