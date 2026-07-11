#include "pj_auth.h"

#include <assert.h>
#include <stdio.h>

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
    puts("auth tests passed");
    return 0;
}
