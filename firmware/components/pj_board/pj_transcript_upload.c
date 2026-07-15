#include "pj_transcript_upload.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

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

int pj_transcript_label_extract(const char *body, size_t body_length,
                                char *label, size_t label_size)
{
    if (label == NULL || label_size == 0U) {
        return 0;
    }
    label[0] = '\0';
    if (body == NULL || body_length == 0U ||
        body_length > PJ_TRANSCRIPT_MAX_BODY_BYTES ||
        memchr(body, '\0', body_length) != NULL) {
        return 0;
    }
    cJSON *json = cJSON_ParseWithLengthOpts(body, body_length + 1U, NULL, true);
    if (json == NULL || !cJSON_IsObject(json)) {
        cJSON_Delete(json);
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
    size_t used = 0U;
    int pending_space = 0;
    for (const char *cursor = display;
         *cursor != '\0' && used + 1U < label_size; cursor++) {
        unsigned char ch = (unsigned char)*cursor;
        if (isspace(ch)) {
            pending_space = used > 0U;
            continue;
        }
        if (pending_space && used + 1U < label_size) {
            label[used++] = ' ';
        }
        label[used++] = (char)ch;
        pending_space = 0;
    }
    label[used] = '\0';
    cJSON_Delete(json);
    return used > 0U;
}
