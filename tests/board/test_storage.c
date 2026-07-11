#include "pj_storage.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

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
    memset(header, 0, 44);
    memcpy(header, "RIFF", 4);
    put_le32(&header[4], 36U + data_bytes);
    memcpy(&header[8], "WAVEfmt ", 8);
    put_le32(&header[16], 16);
    put_le16(&header[20], 1);
    put_le16(&header[22], 1);
    put_le32(&header[24], 16000);
    put_le32(&header[28], 32000);
    put_le16(&header[32], 2);
    put_le16(&header[34], 16);
    memcpy(&header[36], "data", 4);
    put_le32(&header[40], data_bytes);
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

int main(void)
{
    test_capacity_policy();
    test_recovery_policy();
    test_wav_validation();
    puts("storage tests passed");
    return 0;
}
