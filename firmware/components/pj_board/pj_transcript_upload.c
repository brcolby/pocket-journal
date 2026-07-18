#include "pj_transcript_upload.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"

pj_transcript_body_result_t pj_transcript_body_validate(const char *body,
                                                        size_t received_length,
                                                        size_t declared_length)
{
    if (declared_length == 0) {
        return PJ_TRANSCRIPT_BODY_REQUIRED;
    }
    if (declared_length > PJ_TRANSCRIPT_MAX_BODY_BYTES) {
        return PJ_TRANSCRIPT_BODY_TOO_LARGE;
    }
    if (body == NULL || received_length != declared_length) {
        return PJ_TRANSCRIPT_BODY_INCOMPLETE;
    }
    if (memchr(body, '\0', received_length) != NULL) {
        return PJ_TRANSCRIPT_BODY_MALFORMED;
    }
    cJSON *json = cJSON_ParseWithLengthOpts(body, received_length + 1u, NULL, true);
    if (json == NULL) {
        return PJ_TRANSCRIPT_BODY_MALFORMED;
    }
    cJSON *text = cJSON_GetObjectItemCaseSensitive(json, "text");
    pj_transcript_body_result_t result = PJ_TRANSCRIPT_BODY_VALID;
    if (!cJSON_IsObject(json) || !cJSON_IsString(text) || text->valuestring == NULL ||
        text->valuestring[0] == '\0') {
        result = PJ_TRANSCRIPT_BODY_INVALID_CONTENT;
    }
    cJSON_Delete(json);
    return result;
}

static int utf8_continuation(unsigned char value)
{
    return value >= 0x80U && value <= 0xbfU;
}

static size_t utf8_sequence_length(const unsigned char *cursor)
{
    unsigned char first = cursor[0];
    if (first < 0x80U) {
        return 1U;
    }
    if (first >= 0xc2U && first <= 0xdfU &&
        utf8_continuation(cursor[1])) {
        return 2U;
    }
    if (first >= 0xe0U && first <= 0xefU &&
        cursor[1] != '\0' && cursor[2] != '\0' &&
        utf8_continuation(cursor[1]) &&
        utf8_continuation(cursor[2]) &&
        !(first == 0xe0U && cursor[1] < 0xa0U) &&
        !(first == 0xedU && cursor[1] > 0x9fU)) {
        return 3U;
    }
    if (first >= 0xf0U && first <= 0xf4U &&
        cursor[1] != '\0' && cursor[2] != '\0' && cursor[3] != '\0' &&
        utf8_continuation(cursor[1]) &&
        utf8_continuation(cursor[2]) &&
        utf8_continuation(cursor[3]) &&
        !(first == 0xf0U && cursor[1] < 0x90U) &&
        !(first == 0xf4U && cursor[1] > 0x8fU)) {
        return 4U;
    }
    return 0U;
}

static int copy_display_text(const char *source, char *output,
                             size_t output_size)
{
    if (source == NULL || output == NULL || output_size == 0U) {
        return 0;
    }
    output[0] = '\0';
    size_t used = 0U;
    int pending_space = 0;
    const unsigned char *cursor = (const unsigned char *)source;
    while (*cursor != '\0') {
        unsigned char ch = *cursor;
        if (isspace(ch)) {
            pending_space = used > 0U;
            cursor++;
            continue;
        }
        size_t sequence_length = utf8_sequence_length(cursor);
        int replace_invalid = sequence_length == 0U;
        if (replace_invalid) {
            sequence_length = 1U;
        }
        size_t required = sequence_length + (pending_space ? 1U : 0U);
        if (used + required >= output_size) {
            break;
        }
        if (pending_space) {
            output[used++] = ' ';
        }
        if (replace_invalid) {
            output[used++] = '?';
        } else {
            memcpy(output + used, cursor, sequence_length);
            used += sequence_length;
        }
        pending_space = 0;
        cursor += sequence_length;
    }
    output[used] = '\0';
    return used > 0U;
}

static cJSON *parse_transcript_body(const char *body, size_t body_length)
{
    if (body == NULL || body_length == 0U ||
        body_length > PJ_TRANSCRIPT_MAX_BODY_BYTES ||
        memchr(body, '\0', body_length) != NULL) {
        return NULL;
    }
    cJSON *json = cJSON_ParseWithLengthOpts(body, body_length + 1U, NULL, true);
    if (json == NULL || !cJSON_IsObject(json)) {
        cJSON_Delete(json);
        return NULL;
    }
    return json;
}

int pj_transcript_label_extract(const char *body, size_t body_length,
                                char *label, size_t label_size)
{
    if (label == NULL || label_size == 0U) {
        return 0;
    }
    label[0] = '\0';
    cJSON *json = parse_transcript_body(body, body_length);
    if (json == NULL) {
        return 0;
    }
    const cJSON *title = cJSON_GetObjectItemCaseSensitive(json, "title");
    const cJSON *text = cJSON_GetObjectItemCaseSensitive(json, "text");
    const char *display = cJSON_IsString(title) && title->valuestring != NULL &&
                          title->valuestring[0] != '\0' ?
                          title->valuestring :
                          cJSON_IsString(text) ? text->valuestring : NULL;
    if (display == NULL || display[0] == '\0') {
        cJSON_Delete(json);
        return 0;
    }
    int extracted = copy_display_text(display, label, label_size);
    cJSON_Delete(json);
    return extracted;
}

int pj_transcript_text_extract(const char *body, size_t body_length,
                               char *text, size_t text_size)
{
    if (text == NULL || text_size == 0U) {
        return 0;
    }
    text[0] = '\0';
    cJSON *json = parse_transcript_body(body, body_length);
    if (json == NULL) {
        return 0;
    }
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(json, "text");
    int extracted = cJSON_IsString(value) && value->valuestring != NULL &&
                    copy_display_text(value->valuestring, text, text_size);
    cJSON_Delete(json);
    return extracted;
}

static int transcript_value_load(const char *path, char *output,
                                 size_t output_size, int text_only)
{
    struct stat st;
    if (path == NULL || output == NULL || output_size == 0U ||
        stat(path, &st) != 0 || st.st_size <= 0 ||
        (uint64_t)st.st_size > PJ_TRANSCRIPT_MAX_BODY_BYTES) {
        return 0;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    char *body = malloc((size_t)st.st_size + 1U);
    if (body == NULL) {
        fclose(file);
        return 0;
    }
    size_t body_size = fread(body, 1, (size_t)st.st_size, file);
    int read_ok = body_size == (size_t)st.st_size && ferror(file) == 0;
    int close_ok = fclose(file) == 0;
    body[body_size] = '\0';
    int loaded = read_ok && close_ok && (text_only ?
        pj_transcript_text_extract(body, body_size, output, output_size) :
        pj_transcript_label_extract(body, body_size, output, output_size));
    free(body);
    return loaded;
}

int pj_transcript_marker_load(const char *path, char *label,
                              size_t label_size)
{
    return transcript_value_load(path, label, label_size, 0);
}

int pj_transcript_text_load(const char *path, char *text,
                            size_t text_size)
{
    return transcript_value_load(path, text, text_size, 1);
}

static int sha256_hex_digit(unsigned char value)
{
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

static unsigned char lowercase_hex(unsigned char value)
{
    return value >= 'A' && value <= 'F' ?
           (unsigned char)(value + ('a' - 'A')) : value;
}

static int sha256_valid(const char *value)
{
    if (value == NULL || strlen(value) != 64U) {
        return 0;
    }
    for (size_t index = 0; index < 64U; index++) {
        if (!sha256_hex_digit((unsigned char)value[index])) {
            return 0;
        }
    }
    return 1;
}

static int sha256_equal(const char *left, const char *right)
{
    if (!sha256_valid(left) || !sha256_valid(right)) {
        return 0;
    }
    for (size_t index = 0; index < 64U; index++) {
        if (lowercase_hex((unsigned char)left[index]) !=
            lowercase_hex((unsigned char)right[index])) {
            return 0;
        }
    }
    return 1;
}

pj_transcript_source_result_t pj_transcript_source_check(
    const char *body, size_t body_length, const char expected_sha256[65],
    uint64_t expected_bytes)
{
    if (body == NULL || body_length == 0U || expected_sha256 == NULL ||
        memchr(body, '\0', body_length) != NULL) {
        return PJ_TRANSCRIPT_SOURCE_INVALID;
    }
    cJSON *json = cJSON_ParseWithLengthOpts(body, body_length + 1U, NULL, true);
    if (json == NULL || !cJSON_IsObject(json)) {
        cJSON_Delete(json);
        return PJ_TRANSCRIPT_SOURCE_INVALID;
    }
    const cJSON *source = cJSON_GetObjectItemCaseSensitive(json, "source");
    if (source == NULL) {
        cJSON_Delete(json);
        return PJ_TRANSCRIPT_SOURCE_UNSPECIFIED;
    }
    const cJSON *sha256 = cJSON_IsObject(source) ?
        cJSON_GetObjectItemCaseSensitive(source, "sha256") : NULL;
    const cJSON *bytes = cJSON_IsObject(source) ?
        cJSON_GetObjectItemCaseSensitive(source, "bytes") : NULL;
    if (!cJSON_IsString(sha256) || sha256->valuestring == NULL ||
        !cJSON_IsNumber(bytes) || bytes->valuedouble < 0.0 ||
        bytes->valuedouble > 9007199254740991.0 ||
        !sha256_valid(sha256->valuestring)) {
        cJSON_Delete(json);
        return PJ_TRANSCRIPT_SOURCE_INVALID;
    }
    uint64_t source_bytes = (uint64_t)bytes->valuedouble;
    if ((double)source_bytes != bytes->valuedouble) {
        cJSON_Delete(json);
        return PJ_TRANSCRIPT_SOURCE_INVALID;
    }
    if (!sha256_equal(sha256->valuestring, expected_sha256)) {
        cJSON_Delete(json);
        return PJ_TRANSCRIPT_SOURCE_MISMATCH;
    }
    cJSON_Delete(json);
    return source_bytes == expected_bytes ? PJ_TRANSCRIPT_SOURCE_MATCH :
                                            PJ_TRANSCRIPT_SOURCE_MISMATCH;
}

pj_transcript_commit_source_decision_t pj_transcript_source_commit_decision(
    pj_transcript_source_result_t source_result)
{
    if (source_result == PJ_TRANSCRIPT_SOURCE_MISMATCH) {
        return PJ_TRANSCRIPT_COMMIT_SOURCE_RETRY_CHANGED;
    }
    if (source_result == PJ_TRANSCRIPT_SOURCE_MATCH) {
        return PJ_TRANSCRIPT_COMMIT_SOURCE_ACCEPT;
    }
    return PJ_TRANSCRIPT_COMMIT_SOURCE_REJECT_INVALID;
}
