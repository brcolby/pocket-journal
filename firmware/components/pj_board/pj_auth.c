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

int pj_auth_copy_provisioned_token(int provisioned, const char *token,
                                   char *output, size_t capacity)
{
    if (output == NULL || capacity == 0U) {
        return 0;
    }
    output[0] = '\0';
    if (!provisioned || token == NULL || token[0] == '\0') {
        return 0;
    }
    size_t length = strlen(token);
    if (length >= capacity) {
        return 0;
    }
    memcpy(output, token, length + 1U);
    return 1;
}
