#include "pj_companion_auth.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "mbedtls/md.h"

#define REQUEST_KEY_DOMAIN "PJ-COMPANION-SYNC-REQUEST-KEY-V1"
#define RESPONSE_KEY_DOMAIN "PJ-COMPANION-SYNC-RESPONSE-KEY-V1"
#define DATA_KEY_DOMAIN "PJ-COMPANION-SYNC-DATA-KEY-V1"
#define REQUEST_DOMAIN "PJ-COMPANION-SYNC-REQUEST-V1\n"
#define RESPONSE_DOMAIN "PJ-COMPANION-SYNC-RESPONSE-V1\n"
#define DATA_DOMAIN "PJ-COMPANION-SYNC-DATA-V1\n"
#define CANONICAL_BYTES 768U
#define DEFAULT_DEVELOPMENT_TOKEN "dev-token"

static int identifier_valid(const char *value, size_t maximum)
{
    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    size_t size = strlen(value);
    if (size >= maximum) {
        return 0;
    }
    for (size_t i = 0; i < size; i++) {
        char ch = value[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')) {
            return 0;
        }
    }
    return 1;
}

static int canonical_ipv4_valid(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    const char *cursor = value;
    for (unsigned part = 0U; part < 4U; part++) {
        if (*cursor < '0' || *cursor > '9' ||
            (*cursor == '0' && cursor[1] >= '0' && cursor[1] <= '9')) {
            return 0;
        }
        unsigned number = 0U;
        unsigned digits = 0U;
        while (*cursor >= '0' && *cursor <= '9') {
            number = number * 10U + (unsigned)(*cursor - '0');
            digits++;
            cursor++;
            if (digits > 3U || number > 255U) {
                return 0;
            }
        }
        if ((part < 3U && *cursor++ != '.') ||
            (part == 3U && *cursor != '\0')) {
            return 0;
        }
    }
    return 1;
}

static int nonce_valid(const char *value)
{
    if (value == NULL || strlen(value) != 32U) {
        return 0;
    }
    for (size_t i = 0; i < 32U; i++) {
        char ch = value[i];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
            return 0;
        }
    }
    return 1;
}

int pj_companion_auth_token_provisioned(const char *token)
{
    if (token == NULL || strcmp(token, DEFAULT_DEVELOPMENT_TOKEN) == 0) {
        return 0;
    }
    size_t size = strlen(token);
    return size >= 16U && size <= 63U;
}

static int hmac_sha256(const unsigned char *key, size_t key_size,
                       const unsigned char *input, size_t input_size,
                       unsigned char output[32])
{
    if (key == NULL || input == NULL || output == NULL ||
        input_size > CANONICAL_BYTES) {
        return 0;
    }
    const mbedtls_md_info_t *info =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == NULL) {
        return 0;
    }

    unsigned char block_key[64] = {0};
    if (key_size > sizeof(block_key)) {
        if (mbedtls_md(info, key, key_size, block_key) != 0) {
            return 0;
        }
    } else {
        memcpy(block_key, key, key_size);
    }

    unsigned char inner[64U + CANONICAL_BYTES];
    unsigned char outer[64U + 32U];
    for (size_t i = 0; i < sizeof(block_key); i++) {
        inner[i] = (unsigned char)(block_key[i] ^ 0x36U);
        outer[i] = (unsigned char)(block_key[i] ^ 0x5cU);
    }
    memcpy(inner + sizeof(block_key), input, input_size);
    unsigned char inner_digest[32];
    int ok = mbedtls_md(info, inner, sizeof(block_key) + input_size,
                        inner_digest) == 0;
    if (ok) {
        memcpy(outer + sizeof(block_key), inner_digest, sizeof(inner_digest));
        ok = mbedtls_md(info, outer, sizeof(outer), output) == 0;
    }

    memset(block_key, 0, sizeof(block_key));
    memset(inner, 0, sizeof(inner));
    memset(outer, 0, sizeof(outer));
    memset(inner_digest, 0, sizeof(inner_digest));
    return ok;
}

static int derive_key(const char *token, const char *domain,
                      unsigned char output[32])
{
    if (!pj_companion_auth_token_provisioned(token) || domain == NULL) {
        return 0;
    }
    return hmac_sha256((const unsigned char *)token, strlen(token),
                       (const unsigned char *)domain, strlen(domain), output);
}

static int sign_canonical(const char *token, const char *key_domain,
                          const char *canonical, char output[65])
{
    unsigned char key[32];
    unsigned char digest[32];
    const mbedtls_md_info_t *info =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (canonical == NULL || info == NULL ||
        !derive_key(token, key_domain, key) ||
        !hmac_sha256(key, sizeof(key), (const unsigned char *)canonical,
                     strlen(canonical), digest)) {
        memset(key, 0, sizeof(key));
        return 0;
    }
    memset(key, 0, sizeof(key));
    for (size_t i = 0; i < sizeof(digest); i++) {
        (void)snprintf(output + i * 2U, 3U, "%02x", digest[i]);
    }
    output[64] = '\0';
    memset(digest, 0, sizeof(digest));
    return 1;
}

static int mac_equal(const char *supplied, const char expected[65])
{
    if (supplied == NULL || strlen(supplied) != 64U) {
        return 0;
    }
    unsigned char difference = 0U;
    for (size_t i = 0; i < 64U; i++) {
        unsigned char ch = (unsigned char)supplied[i];
        unsigned char valid = (unsigned char)(
            (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'));
        difference |= (unsigned char)(ch ^ (unsigned char)expected[i]);
        difference |= (unsigned char)(valid ^ 1U);
    }
    return difference == 0U;
}

static int canonical_request(
    char *output, size_t output_size, const char *action,
    const char *device_id, const char *device_ip, const char *operation_id,
    uint32_t generation, uint64_t requested_ms, const char *nonce)
{
    if (output == NULL || output_size == 0U ||
        (strcmp(action == NULL ? "" : action, "start") != 0 &&
         strcmp(action == NULL ? "" : action, "status") != 0) ||
        !identifier_valid(device_id, 32U) ||
        !canonical_ipv4_valid(device_ip) ||
        !identifier_valid(operation_id,
                          PJ_COMPANION_SYNC_OPERATION_ID_BYTES) ||
        !nonce_valid(nonce) ||
        generation == 0U ||
        requested_ms > PJ_COMPANION_AUTH_MAX_REQUESTED_MS) {
        return 0;
    }
    int written = snprintf(
        output, output_size,
        REQUEST_DOMAIN
        "version=1\n"
        "action=%zu:%s\n"
        "device_id=%zu:%s\n"
        "device_ip=%zu:%s\n"
        "operation_id=%zu:%s\n"
        "generation=%" PRIu32 "\n"
        "requested_ms=%" PRIu64 "\n"
        "nonce=%zu:%s\n",
        strlen(action), action, strlen(device_id), device_id,
        strlen(device_ip), device_ip,
        strlen(operation_id), operation_id, generation, requested_ms,
        strlen(nonce), nonce);
    return written > 0 && (size_t)written < output_size;
}

int pj_companion_auth_build_request_json(
    const char *token, const char *action, const char *device_id,
    const char *device_ip, const char *operation_id, uint32_t generation,
    uint64_t requested_ms, const char *nonce, char **body_out)
{
    if (body_out == NULL) {
        return 0;
    }
    *body_out = NULL;
    char canonical[CANONICAL_BYTES];
    char mac[65];
    if (!canonical_request(canonical, sizeof(canonical), action, device_id,
                           device_ip, operation_id, generation,
                           requested_ms, nonce) ||
        !sign_canonical(token, REQUEST_KEY_DOMAIN, canonical, mac)) {
        return 0;
    }
    cJSON *json = cJSON_CreateObject();
    if (json == NULL ||
        cJSON_AddNumberToObject(json, "version", 1) == NULL ||
        cJSON_AddStringToObject(json, "action", action) == NULL ||
        cJSON_AddStringToObject(json, "device_id", device_id) == NULL ||
        cJSON_AddStringToObject(json, "device_ip", device_ip) == NULL ||
        cJSON_AddStringToObject(json, "operation_id", operation_id) == NULL ||
        cJSON_AddNumberToObject(json, "generation", generation) == NULL ||
        cJSON_AddNumberToObject(json, "requested_ms",
                               (double)requested_ms) == NULL ||
        cJSON_AddStringToObject(json, "nonce", nonce) == NULL ||
        cJSON_AddStringToObject(json, "mac", mac) == NULL) {
        cJSON_Delete(json);
        return 0;
    }
    *body_out = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    return *body_out != NULL;
}

static int json_exact_fields(const cJSON *json,
                             const char *const *fields, size_t field_count)
{
    unsigned long long seen = 0ULL;
    size_t count = 0U;
    for (const cJSON *item = json == NULL ? NULL : json->child;
         item != NULL; item = item->next) {
        size_t index = 0U;
        while (index < field_count &&
               strcmp(item->string == NULL ? "" : item->string,
                      fields[index]) != 0) {
            index++;
        }
        if (index == field_count || index >= 64U ||
            (seen & (1ULL << index)) != 0ULL) {
            return 0;
        }
        seen |= 1ULL << index;
        count++;
    }
    return count == field_count;
}

static int json_uint(const cJSON *json, const char *name, uint64_t maximum,
                     uint64_t *output)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
        item->valuedouble > (double)maximum) {
        return 0;
    }
    uint64_t parsed = (uint64_t)item->valuedouble;
    if ((double)parsed != item->valuedouble) {
        return 0;
    }
    *output = parsed;
    return 1;
}

static int wire_contains(const char *body, size_t body_size,
                         const char *sequence, size_t sequence_size)
{
    if (body == NULL || sequence == NULL || sequence_size == 0U ||
        body_size < sequence_size) {
        return 0;
    }
    for (size_t offset = 0U; offset <= body_size - sequence_size; offset++) {
        if (memcmp(body + offset, sequence, sequence_size) == 0) {
            return 1;
        }
    }
    return 0;
}

static int canonical_response(char *output, size_t output_size,
                              const pj_companion_auth_response_t *response)
{
    if (output == NULL || response == NULL ||
        !identifier_valid(response->device_id,
                          sizeof(response->device_id)) ||
        !identifier_valid(response->operation_id,
                          sizeof(response->operation_id)) ||
        response->generation == 0U ||
        response->requested_ms > PJ_COMPANION_AUTH_MAX_REQUESTED_MS ||
        (strcmp(response->action, "start") != 0 &&
         strcmp(response->action, "status") != 0) ||
        !nonce_valid(response->nonce) ||
        (strcmp(response->state, "queued") != 0 &&
         strcmp(response->state, "running") != 0 &&
         strcmp(response->state, "succeeded") != 0 &&
         strcmp(response->state, "failed") != 0) ||
        response->total < 0 || response->pending < 0 ||
        response->transferred < 0 || response->failed < 0 ||
        response->transferred > response->total ||
        response->failed > response->total - response->transferred ||
        response->pending != response->total - response->transferred -
                             response->failed ||
        !pj_companion_sync_error_valid(response->error) ||
        (strcmp(response->state, "failed") != 0 &&
         response->error[0] != '\0') ||
        (strcmp(response->state, "succeeded") == 0 &&
         (response->pending != 0 || response->failed != 0))) {
        return 0;
    }
    int written = snprintf(
        output, output_size,
        RESPONSE_DOMAIN
        "version=1\n"
        "action=%zu:%s\n"
        "nonce=%zu:%s\n"
        "device_id=%zu:%s\n"
        "operation_id=%zu:%s\n"
        "generation=%" PRIu32 "\n"
        "requested_ms=%" PRIu64 "\n"
        "state=%zu:%s\n"
        "total=%d\n"
        "pending=%d\n"
        "transferred=%d\n"
        "failed=%d\n"
        "error=%zu:%s\n",
        strlen(response->action), response->action,
        strlen(response->nonce), response->nonce,
        strlen(response->device_id), response->device_id,
        strlen(response->operation_id), response->operation_id,
        response->generation, response->requested_ms,
        strlen(response->state), response->state, response->total,
        response->pending, response->transferred, response->failed,
        strlen(response->error), response->error);
    return written > 0 && (size_t)written < output_size;
}

pj_companion_auth_result_t pj_companion_auth_verify_response_json(
    const char *token, const char *body, size_t body_size,
    const char *expected_action, const char *expected_nonce,
    const char *expected_device_id,
    const char *expected_operation_id, uint32_t expected_generation,
    uint64_t expected_requested_ms, pj_companion_auth_response_t *response)
{
    static const char *const fields[] = {
        "version", "action", "nonce", "device_id", "operation_id", "generation",
        "requested_ms", "state", "total", "pending", "transferred",
        "failed", "error", "mac",
    };
    if (response == NULL || !pj_companion_auth_token_provisioned(token)) {
        return PJ_COMPANION_AUTH_FAILED;
    }
    if (body == NULL || body_size == 0U ||
        memchr(body, '\0', body_size) != NULL ||
        wire_contains(body, body_size, "\\u0000", 6U)) {
        return PJ_COMPANION_AUTH_PROTOCOL_ERROR;
    }
    memset(response, 0, sizeof(*response));
    const char *parse_end = NULL;
    cJSON *json = cJSON_ParseWithLengthOpts(
        body, body_size, &parse_end, 0);
    if (!cJSON_IsObject(json) ||
        parse_end != body + body_size ||
        !json_exact_fields(json, fields, sizeof(fields) / sizeof(fields[0]))) {
        cJSON_Delete(json);
        return PJ_COMPANION_AUTH_PROTOCOL_ERROR;
    }
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
    const cJSON *action = cJSON_GetObjectItemCaseSensitive(json, "action");
    const cJSON *nonce = cJSON_GetObjectItemCaseSensitive(json, "nonce");
    const cJSON *device = cJSON_GetObjectItemCaseSensitive(json, "device_id");
    const cJSON *operation =
        cJSON_GetObjectItemCaseSensitive(json, "operation_id");
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(json, "state");
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(json, "error");
    const cJSON *mac = cJSON_GetObjectItemCaseSensitive(json, "mac");
    uint64_t generation = 0U;
    uint64_t requested_ms = 0U;
    uint64_t total = 0U;
    uint64_t pending = 0U;
    uint64_t transferred = 0U;
    uint64_t failed = 0U;
    int parsed = cJSON_IsNumber(version) && version->valuedouble == 1.0 &&
        cJSON_IsString(action) && cJSON_IsString(nonce) &&
        cJSON_IsString(device) && cJSON_IsString(operation) &&
        cJSON_IsString(state) && cJSON_IsString(error) &&
        cJSON_IsString(mac) &&
        json_uint(json, "generation", UINT32_MAX, &generation) &&
        generation != 0U &&
        json_uint(json, "requested_ms",
                  PJ_COMPANION_AUTH_MAX_REQUESTED_MS, &requested_ms) &&
        json_uint(json, "total", INT_MAX, &total) &&
        json_uint(json, "pending", INT_MAX, &pending) &&
        json_uint(json, "transferred", INT_MAX, &transferred) &&
        json_uint(json, "failed", INT_MAX, &failed);
    if (!parsed ||
        snprintf(response->action, sizeof(response->action), "%s",
                 action->valuestring) >= (int)sizeof(response->action) ||
        snprintf(response->nonce, sizeof(response->nonce), "%s",
                 nonce->valuestring) >= (int)sizeof(response->nonce) ||
        snprintf(response->device_id, sizeof(response->device_id), "%s",
                 device->valuestring) >= (int)sizeof(response->device_id) ||
        snprintf(response->operation_id, sizeof(response->operation_id), "%s",
                 operation->valuestring) >=
            (int)sizeof(response->operation_id) ||
        snprintf(response->state, sizeof(response->state), "%s",
                 state->valuestring) >= (int)sizeof(response->state) ||
        snprintf(response->error, sizeof(response->error), "%s",
                 error->valuestring) >= (int)sizeof(response->error)) {
        cJSON_Delete(json);
        return PJ_COMPANION_AUTH_PROTOCOL_ERROR;
    }
    response->generation = (uint32_t)generation;
    response->requested_ms = requested_ms;
    response->total = (int)total;
    response->pending = (int)pending;
    response->transferred = (int)transferred;
    response->failed = (int)failed;
    char canonical[CANONICAL_BYTES];
    char expected_mac[65];
    if (!canonical_response(canonical, sizeof(canonical), response)) {
        cJSON_Delete(json);
        return PJ_COMPANION_AUTH_PROTOCOL_ERROR;
    }
    int authenticated =
        sign_canonical(token, RESPONSE_KEY_DOMAIN, canonical, expected_mac) &&
        mac_equal(mac->valuestring, expected_mac);
    cJSON_Delete(json);
    if (!authenticated || expected_action == NULL || expected_nonce == NULL ||
        expected_device_id == NULL || expected_operation_id == NULL ||
        strcmp(response->action, expected_action) != 0 ||
        strcmp(response->nonce, expected_nonce) != 0 ||
        strcmp(response->device_id, expected_device_id) != 0 ||
        strcmp(response->operation_id, expected_operation_id) != 0 ||
        response->generation != expected_generation ||
        response->requested_ms != expected_requested_ms) {
        return PJ_COMPANION_AUTH_FAILED;
    }
    return PJ_COMPANION_AUTH_OK;
}

int pj_companion_auth_scoped_data_token(
    const char *token, const char *device_id, const char *operation_id,
    uint32_t generation, uint64_t requested_ms, char output[65])
{
    char canonical[CANONICAL_BYTES];
    if (!identifier_valid(device_id, 32U) ||
        !identifier_valid(operation_id,
                          PJ_COMPANION_SYNC_OPERATION_ID_BYTES) ||
        generation == 0U ||
        requested_ms > PJ_COMPANION_AUTH_MAX_REQUESTED_MS) {
        return 0;
    }
    int written = snprintf(
        canonical, sizeof(canonical),
        DATA_DOMAIN
        "version=1\n"
        "device_id=%zu:%s\n"
        "operation_id=%zu:%s\n"
        "generation=%" PRIu32 "\n"
        "requested_ms=%" PRIu64 "\n",
        strlen(device_id), device_id, strlen(operation_id), operation_id,
        generation, requested_ms);
    return written > 0 && (size_t)written < sizeof(canonical) &&
           sign_canonical(token, DATA_KEY_DOMAIN, canonical, output);
}

int pj_companion_auth_scoped_header_valid(
    const char *authorization, const char *token, const char *device_id,
    const char *operation_id, uint32_t generation, uint64_t requested_ms)
{
    static const char prefix[] = "Bearer ";
    char expected[65];
    return authorization != NULL &&
           strncmp(authorization, prefix, sizeof(prefix) - 1U) == 0 &&
           pj_companion_auth_scoped_data_token(
               token, device_id, operation_id, generation, requested_ms,
               expected) &&
           mac_equal(authorization + sizeof(prefix) - 1U, expected);
}

int pj_companion_auth_self_test(void)
{
    static const char token[] = "paired-token-0123456789";
    static const char device_id[] = "pj-test";
    static const char operation_id[] = "pj-test-00000007";
    static const char nonce[] = "00112233445566778899aabbccddeeff";
    static const uint32_t generation = 7U;
    static const uint64_t requested_ms = 123456789U;
    static const char expected_request[] =
        "{\"version\":1,\"action\":\"start\",\"device_id\":\"pj-test\","
        "\"device_ip\":\"192.0.2.10\",\"operation_id\":"
        "\"pj-test-00000007\",\"generation\":7,\"requested_ms\":"
        "123456789,\"nonce\":\"00112233445566778899aabbccddeeff\","
        "\"mac\":\"312b32119bcec58bcf833b11fa22683ef79944fca2fed53d"
        "60136546b48c7641\"}";
    static const char response[] =
        "{\"version\":1,\"action\":\"start\",\"nonce\":"
        "\"00112233445566778899aabbccddeeff\",\"device_id\":\"pj-test\","
        "\"operation_id\":"
        "\"pj-test-00000007\",\"generation\":7,\"requested_ms\":"
        "123456789,\"state\":\"failed\",\"total\":4,\"pending\":1,"
        "\"transferred\":2,\"failed\":1,\"error\":"
        "\"One recording failed\",\"mac\":"
        "\"750053ebe806b0ca64a6cb3140bc724b4c430f6e835118b432468cc363"
        "9c96c5\"}";
    static const char expected_scoped[] =
        "abd23c23cf3c885614ec9ccda61bc8390901642c25cf1ade60bc16fa40e31681";

    char *request = NULL;
    int request_ok = pj_companion_auth_build_request_json(
        token, "start", device_id, "192.0.2.10", operation_id, generation,
        requested_ms, nonce, &request);
    int request_matches = request_ok && strcmp(request, expected_request) == 0;
    cJSON_free(request);

    pj_companion_auth_response_t parsed;
    int response_matches = pj_companion_auth_verify_response_json(
        token, response, strlen(response), "start", nonce, device_id,
        operation_id, generation, requested_ms, &parsed) ==
        PJ_COMPANION_AUTH_OK &&
        parsed.total == 4 && parsed.pending == 1 && parsed.transferred == 2 &&
        parsed.failed == 1 && strcmp(parsed.state, "failed") == 0 &&
        strcmp(parsed.error, "One recording failed") == 0;

    char trailing[sizeof(response) + 1U];
    memcpy(trailing, response, sizeof(response) - 1U);
    trailing[sizeof(response) - 1U] = 'x';
    trailing[sizeof(response)] = '\0';
    int malformed_rejected = pj_companion_auth_verify_response_json(
        token, trailing, sizeof(response), "start", nonce, device_id,
        operation_id, generation, requested_ms, &parsed) ==
        PJ_COMPANION_AUTH_PROTOCOL_ERROR;
    char embedded_nul[sizeof(response)];
    memcpy(embedded_nul, response, sizeof(response));
    embedded_nul[10] = '\0';
    malformed_rejected = malformed_rejected &&
        pj_companion_auth_verify_response_json(
            token, embedded_nul, sizeof(response) - 1U, "start", nonce,
            device_id, operation_id, generation, requested_ms, &parsed) ==
            PJ_COMPANION_AUTH_PROTOCOL_ERROR;
    static const char unicode_nul[] = "{\"error\":\"\\u0000\"}";
    malformed_rejected = malformed_rejected &&
        pj_companion_auth_verify_response_json(
            token, unicode_nul, strlen(unicode_nul), "start", nonce,
            device_id, operation_id, generation, requested_ms, &parsed) ==
            PJ_COMPANION_AUTH_PROTOCOL_ERROR;

    char scoped[PJ_COMPANION_AUTH_MAC_HEX_BYTES];
    int scoped_matches = pj_companion_auth_scoped_data_token(
        token, device_id, operation_id, generation, requested_ms, scoped) &&
        strcmp(scoped, expected_scoped) == 0;
    return request_matches && response_matches && malformed_rejected &&
           scoped_matches;
}
