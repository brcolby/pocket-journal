#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int pj_auth_header_valid(const char *authorization, const char *token);

#ifdef __cplusplus
}
#endif
