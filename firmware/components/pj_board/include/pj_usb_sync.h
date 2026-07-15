#ifndef PJ_USB_SYNC_H
#define PJ_USB_SYNC_H

#include <stddef.h>
#include <stdint.h>

#define PJ_USB_SYNC_MAX_ARGS 10U
#define PJ_USB_SYNC_REQUEST_ID_BYTES 33U
#define PJ_USB_SYNC_AUDIO_ID_BYTES 161U
#define PJ_USB_SYNC_SHA256_HEX_BYTES 65U
#define PJ_USB_SYNC_CHUNK_BYTES 256U
#define PJ_USB_SYNC_AUDIO_READ_CHUNK_BYTES 1024U
#define PJ_USB_SYNC_TRANSCRIPT_MAX_BYTES (64U * 1024U)

typedef struct {
    char *key;
    char *value;
} pj_usb_sync_arg_t;

typedef struct {
    pj_usb_sync_arg_t args[PJ_USB_SYNC_MAX_ARGS];
    size_t count;
} pj_usb_sync_args_t;

int pj_usb_sync_parse_args(char *line, const char *command,
                           pj_usb_sync_args_t *parsed);
const char *pj_usb_sync_arg(const pj_usb_sync_args_t *parsed,
                            const char *key);
int pj_usb_sync_parse_u32(const char *value, uint32_t *out);
int pj_usb_sync_parse_u64(const char *value, uint64_t *out);
int pj_usb_sync_request_id_valid(const char *request_id);
int pj_usb_sync_sha256_hex_valid(const char *value);
int pj_usb_sync_audio_read_size_valid(uint32_t size);
int pj_usb_sync_hex_decode(const char *hex, uint8_t *out, size_t out_size,
                           size_t *decoded_size);
int pj_usb_sync_hex_encode(const uint8_t *data, size_t data_size, char *out,
                           size_t out_size);
uint32_t pj_usb_sync_snapshot_update(uint32_t snapshot, const void *data,
                                     size_t data_size);
uint32_t pj_usb_sync_snapshot_finish(uint32_t snapshot);

typedef enum {
    PJ_USB_UPLOAD_IDLE = 0,
    PJ_USB_UPLOAD_ACTIVE,
    PJ_USB_UPLOAD_COMMITTED,
    PJ_USB_UPLOAD_ABORTED,
} pj_usb_upload_status_t;

typedef enum {
    PJ_USB_UPLOAD_BEGIN_STARTED = 0,
    PJ_USB_UPLOAD_BEGIN_ATTACHED,
    PJ_USB_UPLOAD_BEGIN_BUSY,
    PJ_USB_UPLOAD_BEGIN_INVALID,
} pj_usb_upload_begin_result_t;

typedef enum {
    PJ_USB_UPLOAD_WRITE_NEW = 0,
    PJ_USB_UPLOAD_WRITE_REPLAY,
    PJ_USB_UPLOAD_WRITE_UNKNOWN,
    PJ_USB_UPLOAD_WRITE_OFFSET,
    PJ_USB_UPLOAD_WRITE_CONTENT,
    PJ_USB_UPLOAD_WRITE_TOO_LARGE,
} pj_usb_upload_write_result_t;

typedef struct {
    pj_usb_upload_status_t status;
    uint32_t upload_id;
    uint32_t expected_bytes;
    uint32_t received_bytes;
    uint32_t last_offset;
    uint16_t last_size;
    char request_id[PJ_USB_SYNC_REQUEST_ID_BYTES];
    char audio_id[PJ_USB_SYNC_AUDIO_ID_BYTES];
    char expected_sha256[PJ_USB_SYNC_SHA256_HEX_BYTES];
    uint8_t last_chunk[PJ_USB_SYNC_CHUNK_BYTES];
} pj_usb_upload_t;

void pj_usb_upload_init(pj_usb_upload_t *upload);
pj_usb_upload_begin_result_t pj_usb_upload_begin(
    pj_usb_upload_t *upload, uint32_t upload_id, const char *request_id,
    const char *audio_id, uint32_t expected_bytes, const char *sha256);
pj_usb_upload_write_result_t pj_usb_upload_check_write(
    const pj_usb_upload_t *upload, uint32_t upload_id, uint32_t offset,
    const uint8_t *data, size_t data_size);
void pj_usb_upload_apply_write(pj_usb_upload_t *upload, uint32_t offset,
                               const uint8_t *data, size_t data_size);
int pj_usb_upload_commit_ready(const pj_usb_upload_t *upload,
                               uint32_t upload_id, const char *sha256);
void pj_usb_upload_mark_committed(pj_usb_upload_t *upload);
int pj_usb_upload_abort(pj_usb_upload_t *upload, uint32_t upload_id);

#endif
