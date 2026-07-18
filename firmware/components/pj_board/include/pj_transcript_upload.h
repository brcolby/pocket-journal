#ifndef PJ_TRANSCRIPT_UPLOAD_H
#define PJ_TRANSCRIPT_UPLOAD_H

#include <stddef.h>
#include <stdint.h>

#define PJ_TRANSCRIPT_MAX_BODY_BYTES (64U * 1024U)

typedef enum {
    PJ_TRANSCRIPT_BODY_VALID = 0,
    PJ_TRANSCRIPT_BODY_REQUIRED,
    PJ_TRANSCRIPT_BODY_TOO_LARGE,
    PJ_TRANSCRIPT_BODY_INCOMPLETE,
    PJ_TRANSCRIPT_BODY_MALFORMED,
    PJ_TRANSCRIPT_BODY_INVALID_CONTENT,
} pj_transcript_body_result_t;

typedef enum {
    PJ_TRANSCRIPT_SOURCE_UNSPECIFIED = 0,
    PJ_TRANSCRIPT_SOURCE_MATCH,
    PJ_TRANSCRIPT_SOURCE_MISMATCH,
    PJ_TRANSCRIPT_SOURCE_INVALID,
} pj_transcript_source_result_t;

typedef enum {
    PJ_TRANSCRIPT_COMMIT_SOURCE_ACCEPT = 0,
    PJ_TRANSCRIPT_COMMIT_SOURCE_RETRY_CHANGED,
    PJ_TRANSCRIPT_COMMIT_SOURCE_REJECT_INVALID,
} pj_transcript_commit_source_decision_t;

pj_transcript_body_result_t pj_transcript_body_validate(const char *body,
                                                        size_t received_length,
                                                        size_t declared_length);
int pj_transcript_label_extract(const char *body, size_t body_length,
                                char *label, size_t label_size);
int pj_transcript_text_extract(const char *body, size_t body_length,
                               char *text, size_t text_size);
int pj_transcript_marker_load(const char *path, char *label,
                              size_t label_size);
int pj_transcript_text_load(const char *path, char *text,
                            size_t text_size);
pj_transcript_source_result_t pj_transcript_source_check(
    const char *body, size_t body_length, const char expected_sha256[65],
    uint64_t expected_bytes);
pj_transcript_commit_source_decision_t pj_transcript_source_commit_decision(
    pj_transcript_source_result_t source_result);

#endif
