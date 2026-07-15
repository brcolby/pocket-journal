#include "pj_usb_sync.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int key_valid(const char *key)
{
    if (key == NULL || key[0] == '\0') {
        return 0;
    }
    for (const unsigned char *cursor = (const unsigned char *)key;
         *cursor != '\0'; cursor++) {
        if (!isalnum(*cursor) && *cursor != '_') {
            return 0;
        }
    }
    return 1;
}

int pj_usb_sync_parse_args(char *line, const char *command,
                           pj_usb_sync_args_t *parsed)
{
    if (line == NULL || command == NULL || parsed == NULL) {
        return 0;
    }
    memset(parsed, 0, sizeof(*parsed));
    size_t command_size = strlen(command);
    if (strncmp(line, command, command_size) != 0 ||
        (line[command_size] != '\0' && line[command_size] != ' ')) {
        return 0;
    }
    char *cursor = line + command_size;
    while (*cursor != '\0') {
        while (*cursor == ' ') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        if (parsed->count >= PJ_USB_SYNC_MAX_ARGS) {
            return 0;
        }
        char *token = cursor;
        while (*cursor != '\0' && *cursor != ' ') {
            cursor++;
        }
        if (*cursor != '\0') {
            *cursor++ = '\0';
        }
        char *equals = strchr(token, '=');
        if (equals == NULL || equals == token || equals[1] == '\0') {
            return 0;
        }
        *equals++ = '\0';
        if (!key_valid(token)) {
            return 0;
        }
        for (size_t i = 0; i < parsed->count; i++) {
            if (strcmp(parsed->args[i].key, token) == 0) {
                return 0;
            }
        }
        parsed->args[parsed->count++] = (pj_usb_sync_arg_t) {
            .key = token,
            .value = equals,
        };
    }
    return 1;
}

const char *pj_usb_sync_arg(const pj_usb_sync_args_t *parsed,
                            const char *key)
{
    if (parsed == NULL || key == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < parsed->count; i++) {
        if (strcmp(parsed->args[i].key, key) == 0) {
            return parsed->args[i].value;
        }
    }
    return NULL;
}

static int parse_unsigned(const char *value, uint64_t maximum, uint64_t *out)
{
    if (value == NULL || value[0] == '\0' || value[0] == '+' ||
        value[0] == '-' || out == NULL) {
        return 0;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed > maximum) {
        return 0;
    }
    *out = (uint64_t)parsed;
    return 1;
}

int pj_usb_sync_parse_u32(const char *value, uint32_t *out)
{
    uint64_t parsed = 0;
    if (!parse_unsigned(value, UINT32_MAX, &parsed) || out == NULL) {
        return 0;
    }
    *out = (uint32_t)parsed;
    return 1;
}

int pj_usb_sync_parse_u64(const char *value, uint64_t *out)
{
    return parse_unsigned(value, UINT64_MAX, out);
}

int pj_usb_sync_request_id_valid(const char *request_id)
{
    if (request_id == NULL) {
        return 0;
    }
    size_t size = strlen(request_id);
    if (size == 0 || size >= PJ_USB_SYNC_REQUEST_ID_BYTES) {
        return 0;
    }
    for (size_t i = 0; i < size; i++) {
        unsigned char ch = (unsigned char)request_id[i];
        if (!isalnum(ch) && ch != '-' && ch != '_' && ch != '.') {
            return 0;
        }
    }
    return 1;
}

int pj_usb_sync_sha256_hex_valid(const char *value)
{
    if (value == NULL || strlen(value) != 64U) {
        return 0;
    }
    for (size_t i = 0; i < 64U; i++) {
        if (!isxdigit((unsigned char)value[i])) {
            return 0;
        }
    }
    return 1;
}

static int hex_nibble(unsigned char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    value = (unsigned char)tolower(value);
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

int pj_usb_sync_hex_decode(const char *hex, uint8_t *out, size_t out_size,
                           size_t *decoded_size)
{
    if (hex == NULL || out == NULL) {
        return 0;
    }
    size_t hex_size = strlen(hex);
    if ((hex_size & 1U) != 0 || hex_size / 2U > out_size) {
        return 0;
    }
    for (size_t i = 0; i < hex_size; i += 2U) {
        int high = hex_nibble((unsigned char)hex[i]);
        int low = hex_nibble((unsigned char)hex[i + 1U]);
        if (high < 0 || low < 0) {
            return 0;
        }
        out[i / 2U] = (uint8_t)((high << 4) | low);
    }
    if (decoded_size != NULL) {
        *decoded_size = hex_size / 2U;
    }
    return 1;
}

int pj_usb_sync_hex_encode(const uint8_t *data, size_t data_size, char *out,
                           size_t out_size)
{
    static const char digits[] = "0123456789abcdef";
    if ((data == NULL && data_size != 0U) || out == NULL ||
        data_size > (SIZE_MAX - 1U) / 2U || out_size < data_size * 2U + 1U) {
        return 0;
    }
    for (size_t i = 0; i < data_size; i++) {
        out[i * 2U] = digits[data[i] >> 4];
        out[i * 2U + 1U] = digits[data[i] & 0x0fU];
    }
    out[data_size * 2U] = '\0';
    return 1;
}

uint32_t pj_usb_sync_snapshot_update(uint32_t snapshot, const void *data,
                                     size_t data_size)
{
    const uint8_t *bytes = data;
    uint32_t hash = snapshot == 0U ? 2166136261U : snapshot;
    for (size_t i = 0; i < data_size; i++) {
        hash ^= bytes[i];
        hash *= 16777619U;
    }
    return hash;
}

uint32_t pj_usb_sync_snapshot_finish(uint32_t snapshot)
{
    return snapshot == 0U ? 1U : snapshot;
}

void pj_usb_upload_init(pj_usb_upload_t *upload)
{
    if (upload != NULL) {
        memset(upload, 0, sizeof(*upload));
    }
}

static int begin_matches(const pj_usb_upload_t *upload,
                         const char *request_id, const char *audio_id,
                         uint32_t expected_bytes, const char *sha256)
{
    return strcmp(upload->request_id, request_id) == 0 &&
           strcmp(upload->audio_id, audio_id) == 0 &&
           upload->expected_bytes == expected_bytes &&
           strcmp(upload->expected_sha256, sha256) == 0;
}

pj_usb_upload_begin_result_t pj_usb_upload_begin(
    pj_usb_upload_t *upload, uint32_t upload_id, const char *request_id,
    const char *audio_id, uint32_t expected_bytes, const char *sha256)
{
    if (upload == NULL || upload_id == 0U ||
        !pj_usb_sync_request_id_valid(request_id) || audio_id == NULL ||
        audio_id[0] == '\0' || strlen(audio_id) >= sizeof(upload->audio_id) ||
        expected_bytes == 0U ||
        expected_bytes > PJ_USB_SYNC_TRANSCRIPT_MAX_BYTES ||
        !pj_usb_sync_sha256_hex_valid(sha256)) {
        return PJ_USB_UPLOAD_BEGIN_INVALID;
    }
    if (upload->status == PJ_USB_UPLOAD_ACTIVE) {
        return begin_matches(upload, request_id, audio_id, expected_bytes, sha256)
            ? PJ_USB_UPLOAD_BEGIN_ATTACHED : PJ_USB_UPLOAD_BEGIN_BUSY;
    }
    if (upload->status == PJ_USB_UPLOAD_COMMITTED &&
        begin_matches(upload, request_id, audio_id, expected_bytes, sha256)) {
        return PJ_USB_UPLOAD_BEGIN_ATTACHED;
    }
    memset(upload, 0, sizeof(*upload));
    upload->status = PJ_USB_UPLOAD_ACTIVE;
    upload->upload_id = upload_id;
    upload->expected_bytes = expected_bytes;
    (void)strcpy(upload->request_id, request_id);
    (void)strcpy(upload->audio_id, audio_id);
    (void)strcpy(upload->expected_sha256, sha256);
    return PJ_USB_UPLOAD_BEGIN_STARTED;
}

pj_usb_upload_write_result_t pj_usb_upload_check_write(
    const pj_usb_upload_t *upload, uint32_t upload_id, uint32_t offset,
    const uint8_t *data, size_t data_size)
{
    if (upload == NULL || upload->status != PJ_USB_UPLOAD_ACTIVE ||
        upload_id == 0U || upload_id != upload->upload_id) {
        return PJ_USB_UPLOAD_WRITE_UNKNOWN;
    }
    if (data == NULL || data_size == 0U ||
        data_size > PJ_USB_SYNC_CHUNK_BYTES ||
        offset > upload->expected_bytes ||
        data_size > upload->expected_bytes - offset) {
        return PJ_USB_UPLOAD_WRITE_TOO_LARGE;
    }
    if (offset == upload->received_bytes) {
        return PJ_USB_UPLOAD_WRITE_NEW;
    }
    if (offset == upload->last_offset && data_size == upload->last_size) {
        return memcmp(data, upload->last_chunk, data_size) == 0
            ? PJ_USB_UPLOAD_WRITE_REPLAY : PJ_USB_UPLOAD_WRITE_CONTENT;
    }
    return PJ_USB_UPLOAD_WRITE_OFFSET;
}

void pj_usb_upload_apply_write(pj_usb_upload_t *upload, uint32_t offset,
                               const uint8_t *data, size_t data_size)
{
    if (upload == NULL || data == NULL || data_size == 0U ||
        data_size > sizeof(upload->last_chunk)) {
        return;
    }
    upload->last_offset = offset;
    upload->last_size = (uint16_t)data_size;
    memcpy(upload->last_chunk, data, data_size);
    upload->received_bytes = offset + (uint32_t)data_size;
}

int pj_usb_upload_commit_ready(const pj_usb_upload_t *upload,
                               uint32_t upload_id, const char *sha256)
{
    if (upload == NULL || upload_id == 0U ||
        upload_id != upload->upload_id || !pj_usb_sync_sha256_hex_valid(sha256) ||
        strcmp(upload->expected_sha256, sha256) != 0) {
        return 0;
    }
    if (upload->status == PJ_USB_UPLOAD_COMMITTED) {
        return 1;
    }
    return upload->status == PJ_USB_UPLOAD_ACTIVE &&
           upload->received_bytes == upload->expected_bytes;
}

void pj_usb_upload_mark_committed(pj_usb_upload_t *upload)
{
    if (upload != NULL && upload->status == PJ_USB_UPLOAD_ACTIVE) {
        upload->status = PJ_USB_UPLOAD_COMMITTED;
    }
}

int pj_usb_upload_abort(pj_usb_upload_t *upload, uint32_t upload_id)
{
    if (upload == NULL || upload_id == 0U) {
        return 0;
    }
    if (upload->upload_id != upload_id || upload->status == PJ_USB_UPLOAD_IDLE ||
        upload->status == PJ_USB_UPLOAD_ABORTED) {
        return 1;
    }
    if (upload->status == PJ_USB_UPLOAD_COMMITTED) {
        return 0;
    }
    upload->status = PJ_USB_UPLOAD_ABORTED;
    upload->received_bytes = 0U;
    upload->last_size = 0U;
    memset(upload->last_chunk, 0, sizeof(upload->last_chunk));
    return 1;
}
