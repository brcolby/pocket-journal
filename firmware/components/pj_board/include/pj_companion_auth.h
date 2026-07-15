#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pj_companion_sync.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_COMPANION_AUTH_PROTOCOL_VERSION 1U
#define PJ_COMPANION_AUTH_MAC_HEX_BYTES 65U
#define PJ_COMPANION_AUTH_NONCE_BYTES 33U
#define PJ_COMPANION_AUTH_MAX_REQUESTED_MS 9007199254740991ULL

typedef enum {
    PJ_COMPANION_AUTH_OK = 0,
    PJ_COMPANION_AUTH_PROTOCOL_ERROR,
    PJ_COMPANION_AUTH_FAILED,
} pj_companion_auth_result_t;

typedef struct {
    uint32_t generation;
    uint64_t requested_ms;
    int total;
    int pending;
    int transferred;
    int failed;
    char device_id[32];
    char operation_id[PJ_COMPANION_SYNC_OPERATION_ID_BYTES];
    char action[8];
    char nonce[PJ_COMPANION_AUTH_NONCE_BYTES];
    char state[24];
    char error[PJ_COMPANION_SYNC_ERROR_BYTES];
} pj_companion_auth_response_t;

int pj_companion_auth_token_provisioned(const char *token);
int pj_companion_auth_build_request_json(
    const char *token, const char *action, const char *device_id,
    const char *device_ip, const char *operation_id, uint32_t generation,
    uint64_t requested_ms, const char *nonce,
    char **body_out);
pj_companion_auth_result_t pj_companion_auth_verify_response_json(
    const char *token, const char *body, size_t body_size,
    const char *expected_action, const char *expected_nonce,
    const char *expected_device_id,
    const char *expected_operation_id, uint32_t expected_generation,
    uint64_t expected_requested_ms, pj_companion_auth_response_t *response);
int pj_companion_auth_scoped_data_token(
    const char *token, const char *device_id, const char *operation_id,
    uint32_t generation, uint64_t requested_ms,
    char output[PJ_COMPANION_AUTH_MAC_HEX_BYTES]);
int pj_companion_auth_scoped_header_valid(
    const char *authorization, const char *token, const char *device_id,
    const char *operation_id, uint32_t generation, uint64_t requested_ms);
int pj_companion_auth_self_test(void);

#ifdef __cplusplus
}
#endif
