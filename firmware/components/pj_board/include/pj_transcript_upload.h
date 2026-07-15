#ifndef PJ_TRANSCRIPT_UPLOAD_H
#define PJ_TRANSCRIPT_UPLOAD_H

#include <stddef.h>

#define PJ_TRANSCRIPT_MAX_BODY_BYTES (64U * 1024U)

typedef enum {
    PJ_TRANSCRIPT_BODY_VALID = 0,
    PJ_TRANSCRIPT_BODY_REQUIRED,
    PJ_TRANSCRIPT_BODY_TOO_LARGE,
    PJ_TRANSCRIPT_BODY_INCOMPLETE,
    PJ_TRANSCRIPT_BODY_MALFORMED,
    PJ_TRANSCRIPT_BODY_INVALID_CONTENT,
} pj_transcript_body_result_t;

pj_transcript_body_result_t pj_transcript_body_validate(const char *body,
                                                        size_t received_length,
                                                        size_t declared_length);
int pj_transcript_label_extract(const char *body, size_t body_length,
                                char *label, size_t label_size);

#endif
