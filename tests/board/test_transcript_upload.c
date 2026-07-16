#include "pj_transcript_upload.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    static const char raw_sha256[] =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    static const char enhanced_sha256[] =
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    const char rich[] =
        "{\"text\":\"hello\",\"language\":\"en\",\"segments\":[{\"start\":0.0,\"end\":1.0}],"
        "\"future_metadata\":{\"speaker\":\"unknown\"}}";
    assert(pj_transcript_body_validate(rich, strlen(rich), strlen(rich)) ==
           PJ_TRANSCRIPT_BODY_VALID);
    assert(pj_transcript_body_validate(NULL, 0, 0) == PJ_TRANSCRIPT_BODY_REQUIRED);
    assert(pj_transcript_body_validate(rich, strlen(rich) - 1, strlen(rich)) ==
           PJ_TRANSCRIPT_BODY_INCOMPLETE);
    assert(pj_transcript_body_validate("{bad", 4, 4) == PJ_TRANSCRIPT_BODY_MALFORMED);
    assert(pj_transcript_body_validate("{\"text\":\"\"}", 11, 11) ==
           PJ_TRANSCRIPT_BODY_INVALID_CONTENT);

    const char matching_source[] =
        "{\"text\":\"hello\",\"source\":{"
        "\"sha256\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\","
        "\"bytes\":1234}}";
    assert(pj_transcript_source_check(matching_source,
                                      strlen(matching_source), raw_sha256,
                                      1234U) == PJ_TRANSCRIPT_SOURCE_MATCH);
    assert(pj_transcript_source_commit_decision(
               PJ_TRANSCRIPT_SOURCE_MATCH) ==
           PJ_TRANSCRIPT_COMMIT_SOURCE_ACCEPT);
    assert(pj_transcript_source_check(matching_source,
                                      strlen(matching_source), enhanced_sha256,
                                      1234U) == PJ_TRANSCRIPT_SOURCE_MISMATCH);
    assert(pj_transcript_source_commit_decision(
               pj_transcript_source_check(matching_source,
                                          strlen(matching_source),
                                          enhanced_sha256, 1234U)) ==
           PJ_TRANSCRIPT_COMMIT_SOURCE_RETRY_CHANGED);
    assert(pj_transcript_source_check(matching_source,
                                      strlen(matching_source), raw_sha256,
                                      1235U) == PJ_TRANSCRIPT_SOURCE_MISMATCH);

    const char no_source[] = "{\"text\":\"legacy client\"}";
    assert(pj_transcript_source_check(no_source, strlen(no_source), raw_sha256,
                                      1234U) ==
           PJ_TRANSCRIPT_SOURCE_UNSPECIFIED);
    assert(pj_transcript_source_commit_decision(
               PJ_TRANSCRIPT_SOURCE_UNSPECIFIED) ==
           PJ_TRANSCRIPT_COMMIT_SOURCE_REJECT_INVALID);
    const char malformed_source[] =
        "{\"text\":\"hello\",\"source\":{\"sha256\":\"not-a-hash\","
        "\"bytes\":1234}}";
    assert(pj_transcript_source_check(malformed_source,
                                      strlen(malformed_source), raw_sha256,
                                      1234U) == PJ_TRANSCRIPT_SOURCE_INVALID);
    assert(pj_transcript_source_commit_decision(
               PJ_TRANSCRIPT_SOURCE_INVALID) ==
           PJ_TRANSCRIPT_COMMIT_SOURCE_REJECT_INVALID);
    const char fractional_source_bytes[] =
        "{\"text\":\"hello\",\"source\":{"
        "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"bytes\":1234.5}}";
    assert(pj_transcript_source_check(fractional_source_bytes,
                                      strlen(fractional_source_bytes),
                                      raw_sha256, 1234U) ==
           PJ_TRANSCRIPT_SOURCE_INVALID);
    assert(pj_transcript_source_commit_decision(
               PJ_TRANSCRIPT_SOURCE_INVALID) ==
           PJ_TRANSCRIPT_COMMIT_SOURCE_REJECT_INVALID);
    const char changed_source_bytes[] =
        "{\"text\":\"hello\",\"source\":{"
        "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"bytes\":1235}}";
    assert(pj_transcript_source_check(changed_source_bytes,
                                      strlen(changed_source_bytes),
                                      raw_sha256, 1234U) ==
           PJ_TRANSCRIPT_SOURCE_MISMATCH);
    assert(pj_transcript_source_commit_decision(
               PJ_TRANSCRIPT_SOURCE_MISMATCH) ==
           PJ_TRANSCRIPT_COMMIT_SOURCE_RETRY_CHANGED);

    char embedded_nul[] = {'{', '\0', '}'};
    assert(pj_transcript_body_validate(embedded_nul, sizeof(embedded_nul),
                                       sizeof(embedded_nul)) == PJ_TRANSCRIPT_BODY_MALFORMED);

    char *boundary = malloc(PJ_TRANSCRIPT_MAX_BODY_BYTES + 2u);
    assert(boundary != NULL);
    const char prefix[] = "{\"text\":\"";
    memcpy(boundary, prefix, sizeof(prefix) - 1u);
    memset(boundary + sizeof(prefix) - 1u, 'a',
           PJ_TRANSCRIPT_MAX_BODY_BYTES - (sizeof(prefix) - 1u) - 2u);
    boundary[PJ_TRANSCRIPT_MAX_BODY_BYTES - 2u] = '"';
    boundary[PJ_TRANSCRIPT_MAX_BODY_BYTES - 1u] = '}';
    boundary[PJ_TRANSCRIPT_MAX_BODY_BYTES] = '\0';
    assert(pj_transcript_body_validate(boundary, PJ_TRANSCRIPT_MAX_BODY_BYTES,
                                       PJ_TRANSCRIPT_MAX_BODY_BYTES) ==
           PJ_TRANSCRIPT_BODY_VALID);
    assert(pj_transcript_body_validate(boundary, PJ_TRANSCRIPT_MAX_BODY_BYTES,
                                       PJ_TRANSCRIPT_MAX_BODY_BYTES + 1u) ==
           PJ_TRANSCRIPT_BODY_TOO_LARGE);
    free(boundary);

    const size_t large_size = 9000u;
    char *large = malloc(large_size + 1u);
    assert(large != NULL);
    const char large_prefix[] = "{\"text\":\"fallback\",\"padding\":\"";
    const char large_suffix[] = "\",\"title\":\"  LARGE   TITLE  \"}";
    size_t padding = large_size - (sizeof(large_prefix) - 1u) -
                     (sizeof(large_suffix) - 1u);
    memcpy(large, large_prefix, sizeof(large_prefix) - 1u);
    memset(large + sizeof(large_prefix) - 1u, 'x', padding);
    memcpy(large + sizeof(large_prefix) - 1u + padding, large_suffix,
           sizeof(large_suffix) - 1u);
    large[large_size] = '\0';
    char label[32];
    assert(pj_transcript_label_extract(large, large_size, label,
                                       sizeof(label)));
    assert(strcmp(label, "LARGE TITLE") == 0);
    free(large);

    puts("transcript upload tests passed");
    return 0;
}
