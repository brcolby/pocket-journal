#include "pj_transcript_upload.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
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
