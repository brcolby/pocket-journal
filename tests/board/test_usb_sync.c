#include "pj_usb_sync.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char SHA[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

static void test_arguments_and_numbers(void)
{
    char line[] = "PJ_AUDIO_READ id_hex=616263 offset=42 max_bytes=256 request_id=r-1";
    pj_usb_sync_args_t args;
    assert(pj_usb_sync_parse_args(line, "PJ_AUDIO_READ", &args));
    assert(strcmp(pj_usb_sync_arg(&args, "id_hex"), "616263") == 0);
    uint64_t offset = 0;
    uint32_t maximum = 0;
    assert(pj_usb_sync_parse_u64(pj_usb_sync_arg(&args, "offset"), &offset));
    assert(offset == 42U);
    assert(pj_usb_sync_parse_u32(pj_usb_sync_arg(&args, "max_bytes"), &maximum));
    assert(maximum == 256U);
    assert(pj_usb_sync_request_id_valid(pj_usb_sync_arg(&args, "request_id")));

    char duplicate[] = "CMD a=1 a=2";
    char malformed[] = "CMD missing";
    assert(!pj_usb_sync_parse_args(duplicate, "CMD", &args));
    assert(!pj_usb_sync_parse_args(malformed, "CMD", &args));
    assert(!pj_usb_sync_parse_u32("-1", &maximum));
    assert(!pj_usb_sync_parse_u32("4294967296", &maximum));
}

static void test_audio_read_chunk_bounds(void)
{
    assert(PJ_USB_SYNC_CHUNK_BYTES == 256U);
    assert(PJ_USB_SYNC_AUDIO_READ_CHUNK_BYTES == 1024U);
    assert(!pj_usb_sync_audio_read_size_valid(0U));
    assert(pj_usb_sync_audio_read_size_valid(1U));
    assert(pj_usb_sync_audio_read_size_valid(256U));
    assert(pj_usb_sync_audio_read_size_valid(1024U));
    assert(!pj_usb_sync_audio_read_size_valid(1025U));
    assert(!pj_usb_sync_audio_read_size_valid(UINT32_MAX));
}

static void test_hex_and_snapshot(void)
{
    uint8_t bytes[4];
    size_t size = 0;
    char encoded[9];
    assert(pj_usb_sync_hex_decode("00a1FE7f", bytes, sizeof(bytes), &size));
    assert(size == 4U && bytes[0] == 0 && bytes[1] == 0xa1 &&
           bytes[2] == 0xfe && bytes[3] == 0x7f);
    assert(pj_usb_sync_hex_encode(bytes, size, encoded, sizeof(encoded)));
    assert(strcmp(encoded, "00a1fe7f") == 0);
    assert(!pj_usb_sync_hex_decode("abc", bytes, sizeof(bytes), &size));
    assert(pj_usb_sync_sha256_hex_valid(SHA));
    assert(!pj_usb_sync_sha256_hex_valid("abcd"));
    uint32_t first = pj_usb_sync_snapshot_update(0, "one", 3);
    uint32_t second = pj_usb_sync_snapshot_update(first, "two", 3);
    assert(pj_usb_sync_snapshot_finish(second) != 0U);
    assert(second == pj_usb_sync_snapshot_update(
        pj_usb_sync_snapshot_update(0, "one", 3), "two", 3));
}

static void test_upload_idempotency_and_bounds(void)
{
    pj_usb_upload_t upload;
    pj_usb_upload_init(&upload);
    assert(pj_usb_upload_begin(&upload, 7, "req-1", "note.wav", 5, SHA) ==
           PJ_USB_UPLOAD_BEGIN_STARTED);
    assert(pj_usb_upload_begin(&upload, 99, "req-1", "note.wav", 5, SHA) ==
           PJ_USB_UPLOAD_BEGIN_ATTACHED);
    assert(upload.upload_id == 7U);
    assert(pj_usb_upload_begin(&upload, 8, "req-2", "other.wav", 5, SHA) ==
           PJ_USB_UPLOAD_BEGIN_BUSY);

    const uint8_t first[] = {'h', 'e', 'l'};
    assert(pj_usb_upload_check_write(&upload, 7, 0, first, sizeof(first)) ==
           PJ_USB_UPLOAD_WRITE_NEW);
    pj_usb_upload_apply_write(&upload, 0, first, sizeof(first));
    assert(upload.received_bytes == 3U);
    assert(pj_usb_upload_check_write(&upload, 7, 0, first, sizeof(first)) ==
           PJ_USB_UPLOAD_WRITE_REPLAY);
    const uint8_t bad[] = {'h', 'u', 'h'};
    assert(pj_usb_upload_check_write(&upload, 7, 0, bad, sizeof(bad)) ==
           PJ_USB_UPLOAD_WRITE_CONTENT);
    assert(pj_usb_upload_check_write(&upload, 7, 1, first, sizeof(first)) ==
           PJ_USB_UPLOAD_WRITE_OFFSET);

    const uint8_t last[] = {'l', 'o'};
    assert(pj_usb_upload_check_write(&upload, 7, 3, last, sizeof(last)) ==
           PJ_USB_UPLOAD_WRITE_NEW);
    pj_usb_upload_apply_write(&upload, 3, last, sizeof(last));
    assert(pj_usb_upload_commit_ready(&upload, 7, SHA));
    pj_usb_upload_mark_committed(&upload);
    assert(pj_usb_upload_commit_ready(&upload, 7, SHA));
    assert(!pj_usb_upload_abort(&upload, 7));
}

static void test_abort_is_safe(void)
{
    pj_usb_upload_t upload;
    pj_usb_upload_init(&upload);
    assert(pj_usb_upload_abort(&upload, 123));
    assert(pj_usb_upload_begin(&upload, 12, "req", "note.wav", 1, SHA) ==
           PJ_USB_UPLOAD_BEGIN_STARTED);
    assert(pj_usb_upload_abort(&upload, 12));
    assert(pj_usb_upload_abort(&upload, 12));
}

int main(void)
{
    test_arguments_and_numbers();
    test_audio_read_chunk_bounds();
    test_hex_and_snapshot();
    test_upload_idempotency_and_bounds();
    test_abort_is_safe();
    puts("USB sync protocol tests passed");
    return 0;
}
