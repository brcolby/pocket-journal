#include "pj_auth.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *token = "pairing-token_123";
    assert(pj_auth_header_valid("Bearer pairing-token_123", token));
    assert(!pj_auth_header_valid(NULL, token));
    assert(!pj_auth_header_valid("", token));
    assert(!pj_auth_header_valid("pairing-token_123", token));
    assert(!pj_auth_header_valid("bearer pairing-token_123", token));
    assert(!pj_auth_header_valid("Bearer", token));
    assert(!pj_auth_header_valid("Bearer  pairing-token_123", token));
    assert(!pj_auth_header_valid("Bearer pairing-token_123 ", token));
    assert(!pj_auth_header_valid("Bearer pairing-token_124", token));
    assert(!pj_auth_header_valid("Bearer pairing-token_12", token));
    assert(!pj_auth_header_valid("Bearer pairing-token_123", ""));

    char live_token[64];
    assert(!pj_auth_copy_provisioned_token(
        0, "development-token", live_token, sizeof(live_token)));
    assert(live_token[0] == '\0');
    assert(!pj_auth_copy_provisioned_token(
        1, "", live_token, sizeof(live_token)));
    assert(pj_auth_copy_provisioned_token(
        1, "new-provisioned-token", live_token, sizeof(live_token)));
    assert(strcmp(live_token, "new-provisioned-token") == 0);
    puts("auth tests passed");
    return 0;
}
