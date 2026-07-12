#include "pj_transcript_upload.h"

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
