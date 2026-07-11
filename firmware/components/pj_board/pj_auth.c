#include "pj_auth.h"

#include <stddef.h>
#include <string.h>

int pj_auth_header_valid(const char *authorization, const char *token)
{
    static const char prefix[] = "Bearer ";
    if (authorization == NULL || token == NULL || token[0] == '\0' ||
        strncmp(authorization, prefix, sizeof(prefix) - 1U) != 0) {
        return 0;
    }
    const char *provided = authorization + sizeof(prefix) - 1U;
    size_t token_length = strlen(token);
    if (strlen(provided) != token_length) {
        return 0;
    }
    unsigned char difference = 0;
    for (size_t i = 0; i < token_length; i++) {
        difference |= (unsigned char)provided[i] ^ (unsigned char)token[i];
    }
    return difference == 0;
}
