#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int pj_auth_header_valid(const char *authorization, const char *token);
int pj_auth_copy_provisioned_token(int provisioned, const char *token,
                                   char *output, size_t capacity);

#ifdef __cplusplus
}
#endif
