#include "pj_ota.h"

#include "pj_auth.h"
#include "pj_ota_policy.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_efuse.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "sdkconfig.h"

#define PJ_OTA_HTTP_BODY_MAX 1024U
#define PJ_OTA_STREAM_BUFFER_BYTES 4096U
#define PJ_OTA_SIGNATURE_MAX_BYTES 128U
#define PJ_OTA_KEY_MAX_BYTES 256U
#define PJ_OTA_RESTART_DELAY_MS 750U
#define PJ_OTA_PREFLIGHT_TTL_MS (5U * 60U * 1000U)
#define PJ_OTA_NVS_NAMESPACE "pj_ota"

#ifdef CONFIG_SECURE_SIGNED_ON_UPDATE
#define PJ_OTA_IDF_SIGNED_APP_ENABLED 1
#else
#define PJ_OTA_IDF_SIGNED_APP_ENABLED 0
#endif

typedef struct {
    SemaphoreHandle_t lock;
    TimerHandle_t preflight_timer;
    pj_ota_session_t session;
    pj_ota_manifest_t manifest;
    char device_id[32];
    char token[64];
    char board[PJ_OTA_BOARD_LEN];
    char state[24];
    char reason[64];
    char target_version[PJ_OTA_TEXT_LEN];
    char target_sha256[PJ_OTA_SHA256_HEX_LEN + 1U];
    int initialized;
    int boot_pending_verify;
    int nvs_ready;
    int verification_key_ready;
    int mutations_reserved;
    pj_ota_mutation_reserve_fn reserve_mutations;
    pj_ota_mutation_release_fn release_mutations;
    TickType_t ready_expires_at;
} pj_ota_context_t;

static const char *TAG = "pj-ota";
static pj_ota_context_t g_ota;

static void preflight_timer_callback(TimerHandle_t timer);
static void ota_abort(esp_ota_handle_t handle, int begun, const char *reason);

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (capacity == 0U) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    (void)snprintf(destination, capacity, "%s", source);
}

static int auth_ok(httpd_req_t *request)
{
    char header[96];
    return httpd_req_get_hdr_value_str(request, "Authorization", header,
                                       sizeof(header)) == ESP_OK &&
           pj_auth_header_valid(header, g_ota.token);
}

static esp_err_t send_json(httpd_req_t *request, const char *status, const char *json)
{
    if (status != NULL) {
        httpd_resp_set_status(request, status);
    }
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, json);
}

static esp_err_t require_auth(httpd_req_t *request)
{
    if (auth_ok(request)) {
        return ESP_OK;
    }
    httpd_resp_set_hdr(request, "WWW-Authenticate", "Bearer realm=\"pocket-journal\"");
    httpd_resp_set_hdr(request, "Connection", "close");
    (void)send_json(request, "401 Unauthorized",
                    "{\"error\":\"unauthorized\",\"code\":\"unauthorized\"}");
    return ESP_ERR_INVALID_STATE;
}

static int hex_decode(const char *encoded, uint8_t *decoded, size_t capacity,
                      size_t *decoded_size)
{
    if (encoded == NULL) {
        return 0;
    }
    size_t length = strlen(encoded);
    if ((length & 1U) != 0U || length / 2U > capacity) {
        return 0;
    }
    for (size_t i = 0; i < length; i += 2U) {
        char pair[3] = {encoded[i], encoded[i + 1U], '\0'};
        char *end = NULL;
        unsigned long value = strtoul(pair, &end, 16);
        if (end == NULL || *end != '\0') {
            return 0;
        }
        decoded[i / 2U] = (uint8_t)value;
    }
    if (decoded_size != NULL) {
        *decoded_size = length / 2U;
    }
    return length > 0U;
}

static void bytes_to_hex(const uint8_t *bytes, size_t size, char *encoded)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < size; i++) {
        encoded[i * 2U] = digits[bytes[i] >> 4U];
        encoded[i * 2U + 1U] = digits[bytes[i] & 0x0fU];
    }
    encoded[size * 2U] = '\0';
}

static int trusted_key_parse(mbedtls_pk_context *public_key)
{
    uint8_t key[PJ_OTA_KEY_MAX_BYTES];
    size_t key_size = 0U;
    return public_key != NULL &&
           hex_decode(CONFIG_PJ_OTA_TRUSTED_PUBLIC_KEY_HEX, key, sizeof(key),
                      &key_size) &&
           mbedtls_pk_parse_public_key(public_key, key, key_size) == 0 &&
           mbedtls_pk_can_do_psa(public_key,
                                 PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                                 PSA_KEY_USAGE_VERIFY_HASH);
}

static int signature_verify(const pj_ota_manifest_t *manifest,
                            const char *signature_hex)
{
    if (!g_ota.verification_key_ready) {
        return 0;
    }
    uint8_t signature[PJ_OTA_SIGNATURE_MAX_BYTES];
    size_t signature_size = 0U;
    if (!hex_decode(signature_hex, signature, sizeof(signature), &signature_size)) {
        return 0;
    }
    char canonical[PJ_OTA_CANONICAL_MAX];
    size_t canonical_size = 0U;
    if (!pj_ota_manifest_canonicalize(manifest, canonical, sizeof(canonical),
                                      &canonical_size)) {
        return 0;
    }
    uint8_t digest[32];
    size_t digest_size = 0U;
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_compute(PSA_ALG_SHA_256, (const uint8_t *)canonical,
                         canonical_size, digest, sizeof(digest),
                         &digest_size) != PSA_SUCCESS ||
        digest_size != sizeof(digest)) {
        return 0;
    }
    mbedtls_pk_context public_key;
    mbedtls_pk_init(&public_key);
    int verified = trusted_key_parse(&public_key) &&
                   mbedtls_pk_verify(&public_key, MBEDTLS_MD_SHA256,
                                     digest, sizeof(digest), signature,
                                     signature_size) == 0;
    mbedtls_pk_free(&public_key);
    return verified;
}

static int json_string(cJSON *object, const char *name, char *destination,
                       size_t capacity)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsString(item) || item->valuestring == NULL ||
        strlen(item->valuestring) >= capacity) {
        return 0;
    }
    copy_text(destination, capacity, item->valuestring);
    return 1;
}

static int json_u32(cJSON *object, const char *name, uint32_t *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
        item->valuedouble > (double)UINT32_MAX ||
        item->valuedouble != (double)(uint32_t)item->valuedouble) {
        return 0;
    }
    *value = (uint32_t)item->valuedouble;
    return 1;
}

static int json_u64(cJSON *object, const char *name, uint64_t *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || item->valuedouble <= 0.0 ||
        item->valuedouble > (double)CONFIG_PJ_OTA_MAX_IMAGE_BYTES ||
        item->valuedouble != (double)(uint64_t)item->valuedouble) {
        return 0;
    }
    *value = (uint64_t)item->valuedouble;
    return 1;
}

static int preflight_schema_exact(const cJSON *root)
{
    static const char *const fields[] = {
        "size", "sha256", "project", "board", "target", "version",
        "secure_version", "signature",
    };
    unsigned seen = 0U;
    for (const cJSON *item = root == NULL ? NULL : root->child;
         item != NULL; item = item->next) {
        size_t index = 0U;
        while (index < sizeof(fields) / sizeof(fields[0]) &&
               (item->string == NULL || strcmp(item->string, fields[index]) != 0)) {
            index++;
        }
        if (index == sizeof(fields) / sizeof(fields[0]) ||
            (seen & (1U << index)) != 0U) {
            return 0;
        }
        seen |= 1U << index;
    }
    return seen == (1U << (sizeof(fields) / sizeof(fields[0]))) - 1U;
}

static int parse_preflight(const char *body, pj_ota_manifest_t *manifest,
                           char *signature_hex, size_t signature_capacity)
{
    cJSON *root = cJSON_ParseWithOpts(body, NULL, 1);
    if (!cJSON_IsObject(root) || !preflight_schema_exact(root)) {
        cJSON_Delete(root);
        return 0;
    }
    memset(manifest, 0, sizeof(*manifest));
    int valid = json_u64(root, "size", &manifest->size) &&
                json_string(root, "sha256", manifest->sha256,
                            sizeof(manifest->sha256)) &&
                json_string(root, "project", manifest->project,
                            sizeof(manifest->project)) &&
                json_string(root, "board", manifest->board,
                            sizeof(manifest->board)) &&
                json_string(root, "target", manifest->target,
                            sizeof(manifest->target)) &&
                json_string(root, "version", manifest->version,
                            sizeof(manifest->version)) &&
                json_u32(root, "secure_version", &manifest->secure_version) &&
                json_string(root, "signature", signature_hex,
                            signature_capacity) &&
                pj_ota_manifest_valid(manifest);
    cJSON_Delete(root);
    return valid;
}

static int read_request_body(httpd_req_t *request, char *body, size_t capacity)
{
    if (request->content_len <= 0 || (size_t)request->content_len >= capacity) {
        return 0;
    }
    int received = 0;
    while (received < request->content_len) {
        int result = httpd_req_recv(request, body + received,
                                    request->content_len - received);
        if (result <= 0) {
            return 0;
        }
        received += result;
    }
    body[received] = '\0';
    return 1;
}

static void state_set(const char *state, const char *reason)
{
    copy_text(g_ota.state, sizeof(g_ota.state), state);
    copy_text(g_ota.reason, sizeof(g_ota.reason), reason);
}

static int mutations_reserve_locked(void)
{
    if (g_ota.mutations_reserved) {
        return 1;
    }
    if (g_ota.reserve_mutations == NULL || !g_ota.reserve_mutations()) {
        return 0;
    }
    g_ota.mutations_reserved = 1;
    return 1;
}

static void mutations_release_locked(void)
{
    if (!g_ota.mutations_reserved) {
        return;
    }
    g_ota.mutations_reserved = 0;
    if (g_ota.release_mutations != NULL) {
        g_ota.release_mutations();
    }
}

static esp_err_t record_write(const char *state)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(PJ_OTA_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_set_str(handle, "state", state);
    if (result == ESP_OK) {
        result = nvs_set_str(handle, "version", g_ota.target_version);
    }
    if (result == ESP_OK) {
        result = nvs_set_str(handle, "sha256", g_ota.target_sha256);
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

static void record_load(void)
{
    nvs_handle_t handle;
    g_ota.nvs_ready = nvs_open(PJ_OTA_NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK;
    if (!g_ota.nvs_ready) {
        state_set("failed", "ota_state_unavailable");
        return;
    }
    char persisted_state[24] = {0};
    size_t state_size = sizeof(persisted_state);
    size_t version_size = sizeof(g_ota.target_version);
    size_t sha_size = sizeof(g_ota.target_sha256);
    esp_err_t state_result = nvs_get_str(handle, "state", persisted_state,
                                         &state_size);
    esp_err_t version_result = nvs_get_str(handle, "version",
                                           g_ota.target_version,
                                           &version_size);
    esp_err_t sha_result = nvs_get_str(handle, "sha256", g_ota.target_sha256,
                                       &sha_size);
    nvs_close(handle);
    if (state_result == ESP_ERR_NVS_NOT_FOUND) {
        state_set("idle", "");
        return;
    }
    if (state_result != ESP_OK || version_result != ESP_OK || sha_result != ESP_OK) {
        state_set("failed", "ota_state_corrupt");
        return;
    }
    const esp_app_desc_t *running = esp_app_get_description();
    if (strcmp(running->version, g_ota.target_version) != 0) {
        state_set("rolled_back", "target_not_running");
        (void)record_write("rolled_back");
        return;
    }
    const esp_partition_t *partition = esp_ota_get_running_partition();
    esp_ota_img_states_t image_state = ESP_OTA_IMG_UNDEFINED;
    if (partition != NULL &&
        esp_ota_get_state_partition(partition, &image_state) == ESP_OK &&
        image_state == ESP_OTA_IMG_PENDING_VERIFY) {
        g_ota.boot_pending_verify = 1;
        state_set("testing", "awaiting_health_confirmation");
        return;
    }
    state_set("confirmed", "");
    (void)record_write("confirmed");
}

void pj_ota_init(const char *device_id, const char *token, const char *board,
                 pj_ota_mutation_reserve_fn reserve_mutations,
                 pj_ota_mutation_release_fn release_mutations)
{
    if (g_ota.initialized) {
        return;
    }
    memset(&g_ota, 0, sizeof(g_ota));
    copy_text(g_ota.device_id, sizeof(g_ota.device_id), device_id);
    copy_text(g_ota.token, sizeof(g_ota.token), token);
    copy_text(g_ota.board, sizeof(g_ota.board), board);
    g_ota.reserve_mutations = reserve_mutations;
    g_ota.release_mutations = release_mutations;
    pj_ota_session_init(&g_ota.session);
    g_ota.lock = xSemaphoreCreateMutex();
    if (g_ota.lock == NULL) {
        state_set("failed", "ota_lock_unavailable");
        g_ota.initialized = 1;
        return;
    }
    g_ota.preflight_timer = xTimerCreate(
        "pj-ota-ttl", pdMS_TO_TICKS(PJ_OTA_PREFLIGHT_TTL_MS), pdFALSE,
        NULL, preflight_timer_callback);
    mbedtls_pk_context public_key;
    mbedtls_pk_init(&public_key);
    g_ota.verification_key_ready = trusted_key_parse(&public_key);
    mbedtls_pk_free(&public_key);
    record_load();
    if (g_ota.preflight_timer == NULL) {
        state_set("failed", "ota_timer_unavailable");
    }
    g_ota.initialized = 1;
    ESP_LOGI(TAG, "OTA state=%s trusted_key=%s", g_ota.state,
             g_ota.verification_key_ready ? "configured" : "unconfigured");
}

static void upload_id_generate(char output[PJ_OTA_UPLOAD_ID_LEN + 1U])
{
    uint8_t random_bytes[PJ_OTA_UPLOAD_ID_LEN / 2U];
    esp_fill_random(random_bytes, sizeof(random_bytes));
    bytes_to_hex(random_bytes, sizeof(random_bytes), output);
}

static void preflight_timer_callback(TimerHandle_t timer)
{
    if (timer != g_ota.preflight_timer || g_ota.lock == NULL) {
        return;
    }
    if (xSemaphoreTake(g_ota.lock, 0) != pdTRUE) {
        (void)xTimerChangePeriod(timer, pdMS_TO_TICKS(100), 0);
        return;
    }
    if (g_ota.session.state == PJ_OTA_TRANSFER_READY &&
        (int32_t)(xTaskGetTickCount() - g_ota.ready_expires_at) >= 0) {
        pj_ota_session_abort(&g_ota.session);
        state_set("failed", "preflight_expired");
        mutations_release_locked();
    }
    xSemaphoreGive(g_ota.lock);
}

static esp_err_t ota_status_handler(httpd_req_t *request)
{
    if (require_auth(request) != ESP_OK) {
        return ESP_OK;
    }
    if (g_ota.lock == NULL ||
        xSemaphoreTake(g_ota.lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return send_json(request, "503 Service Unavailable",
                         "{\"error\":\"OTA status busy\","
                         "\"code\":\"ota_busy\"}");
    }
    char device_id[sizeof(g_ota.device_id)];
    char state[sizeof(g_ota.state)];
    char reason[sizeof(g_ota.reason)];
    char target_version[sizeof(g_ota.target_version)];
    char target_sha256[sizeof(g_ota.target_sha256)];
    copy_text(device_id, sizeof(device_id), g_ota.device_id);
    copy_text(state, sizeof(state), g_ota.state);
    copy_text(reason, sizeof(reason), g_ota.reason);
    copy_text(target_version, sizeof(target_version), g_ota.target_version);
    copy_text(target_sha256, sizeof(target_sha256), g_ota.target_sha256);
    uint64_t received_bytes = g_ota.session.received_bytes;
    uint64_t total_bytes = g_ota.session.expected_bytes;
    int verification_key_ready = g_ota.verification_key_ready;
    int mutations_reserved = g_ota.mutations_reserved;
    xSemaphoreGive(g_ota.lock);

    const esp_app_desc_t *running = esp_app_get_description();
    cJSON *json = cJSON_CreateObject();
    int valid = json != NULL &&
        cJSON_AddStringToObject(json, "device_id", device_id) != NULL &&
        cJSON_AddStringToObject(json, "state", state) != NULL &&
        cJSON_AddStringToObject(json, "reason", reason) != NULL &&
        cJSON_AddStringToObject(json, "running_version", running->version) != NULL &&
        cJSON_AddStringToObject(json, "target_version", target_version) != NULL &&
        cJSON_AddStringToObject(json, "target_sha256", target_sha256) != NULL &&
        cJSON_AddNumberToObject(json, "received_bytes", (double)received_bytes) != NULL &&
        cJSON_AddNumberToObject(json, "total_bytes", (double)total_bytes) != NULL &&
        cJSON_AddBoolToObject(json, "rollback_enabled", 1) != NULL &&
        cJSON_AddBoolToObject(json, "verification_key_configured",
                              verification_key_ready) != NULL &&
        cJSON_AddBoolToObject(json, "idf_signed_app_verification",
                              PJ_OTA_IDF_SIGNED_APP_ENABLED) != NULL &&
        cJSON_AddBoolToObject(json, "mutations_reserved", mutations_reserved) != NULL;
    char *response = valid ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    if (response == NULL) {
        return send_json(request, "503 Service Unavailable",
                         "{\"error\":\"OTA status allocation failed\","
                         "\"code\":\"out_of_memory\"}");
    }
    esp_err_t result = send_json(request, NULL, response);
    cJSON_free(response);
    return result;
}

static esp_err_t ota_preflight_handler(httpd_req_t *request)
{
    if (require_auth(request) != ESP_OK) {
        return ESP_OK;
    }
    char body[PJ_OTA_HTTP_BODY_MAX + 1U];
    if (!read_request_body(request, body, sizeof(body))) {
        return send_json(request, "400 Bad Request",
                         "{\"accepted\":false,\"error\":\"invalid preflight body\","
                         "\"reason\":\"invalid preflight body\","
                         "\"code\":\"manifest_invalid\"}");
    }
    pj_ota_manifest_t candidate;
    char signature_hex[PJ_OTA_SIGNATURE_MAX_BYTES * 2U + 1U];
    if (!parse_preflight(body, &candidate, signature_hex,
                         sizeof(signature_hex))) {
        return send_json(request, "400 Bad Request",
                         "{\"accepted\":false,\"error\":\"invalid signed manifest\","
                         "\"reason\":\"invalid signed manifest\","
                         "\"code\":\"manifest_invalid\"}");
    }
    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    const esp_app_desc_t *running = esp_app_get_description();
    pj_ota_identity_t identity = {
        .project = running->project_name,
        .board = g_ota.board,
        .target = CONFIG_IDF_TARGET,
        .version = running->version,
        .secure_version = running->secure_version,
        .partition_capacity = update == NULL ? 0U : update->size,
    };
    int locked = g_ota.lock != NULL &&
                 xSemaphoreTake(g_ota.lock, pdMS_TO_TICKS(100)) == pdTRUE;
    if (!locked) {
        return send_json(request, "409 Conflict",
                         "{\"accepted\":false,\"error\":\"OTA busy\","
                         "\"reason\":\"OTA busy\","
                         "\"code\":\"ota_busy\"}");
    }
    int active = g_ota.session.state == PJ_OTA_TRANSFER_READY ||
                 g_ota.session.state == PJ_OTA_TRANSFER_WRITING ||
                 g_ota.session.state == PJ_OTA_TRANSFER_PENDING_REBOOT;
    if (g_ota.session.state == PJ_OTA_TRANSFER_READY &&
        (int32_t)(xTaskGetTickCount() - g_ota.ready_expires_at) >= 0) {
        pj_ota_session_abort(&g_ota.session);
        state_set("failed", "preflight_expired");
        mutations_release_locked();
        active = 0;
    }
    int signature_valid = signature_verify(&candidate, signature_hex);
    pj_ota_check_t check = pj_ota_preflight_check(
        &candidate, &identity,
        g_ota.verification_key_ready, PJ_OTA_IDF_SIGNED_APP_ENABLED,
        signature_valid,
        active);
    if (check == PJ_OTA_CHECK_ACCEPTED &&
        !esp_efuse_check_secure_version(candidate.secure_version)) {
        check = PJ_OTA_CHECK_SECURE_VERSION;
    }
    if (check == PJ_OTA_CHECK_ACCEPTED &&
        (!g_ota.nvs_ready || update == NULL || g_ota.preflight_timer == NULL ||
         g_ota.reserve_mutations == NULL || g_ota.release_mutations == NULL)) {
        check = PJ_OTA_CHECK_IMAGE_INVALID;
    }
    char upload_id[PJ_OTA_UPLOAD_ID_LEN + 1U] = {0};
    if (check == PJ_OTA_CHECK_ACCEPTED) {
        upload_id_generate(upload_id);
        if (pj_ota_session_prepare(&g_ota.session, upload_id,
                                   candidate.size) != PJ_OTA_TRANSFER_OK) {
            check = PJ_OTA_CHECK_BUSY;
        } else {
            g_ota.manifest = candidate;
            copy_text(g_ota.target_version, sizeof(g_ota.target_version),
                      candidate.version);
            copy_text(g_ota.target_sha256, sizeof(g_ota.target_sha256),
                      candidate.sha256);
            g_ota.ready_expires_at = xTaskGetTickCount() +
                                     pdMS_TO_TICKS(PJ_OTA_PREFLIGHT_TTL_MS);
            if (xTimerReset(g_ota.preflight_timer, 0) != pdPASS) {
                pj_ota_session_abort(&g_ota.session);
                check = PJ_OTA_CHECK_IMAGE_INVALID;
            } else {
                state_set("ready", "");
            }
        }
    }
    xSemaphoreGive(g_ota.lock);
    if (check != PJ_OTA_CHECK_ACCEPTED) {
        char response[256];
        (void)snprintf(response, sizeof(response),
                       "{\"accepted\":false,\"error\":\"%s\",\"reason\":\"%s\","
                       "\"code\":\"%s\"}", pj_ota_check_code(check),
                       pj_ota_check_code(check),
                       pj_ota_check_code(check));
        const char *status = check == PJ_OTA_CHECK_BUSY ?
            "409 Conflict" : "422 Unprocessable Entity";
        return send_json(request, status, response);
    }
    cJSON *response = cJSON_CreateObject();
    int response_valid = response != NULL &&
        cJSON_AddBoolToObject(response, "accepted", 1) != NULL &&
        cJSON_AddStringToObject(response, "upload_id", upload_id) != NULL &&
        cJSON_AddStringToObject(response, "device_id", g_ota.device_id) != NULL &&
        cJSON_AddStringToObject(response, "running_version", running->version) != NULL &&
        cJSON_AddStringToObject(response, "target_version", candidate.version) != NULL &&
        cJSON_AddNumberToObject(response, "size", (double)candidate.size) != NULL;
    char *encoded = response_valid ? cJSON_PrintUnformatted(response) : NULL;
    cJSON_Delete(response);
    if (encoded == NULL) {
        ota_abort(0, 0, "response_allocation_failed");
        return send_json(request, "503 Service Unavailable",
                         "{\"error\":\"OTA response allocation failed\","
                         "\"code\":\"out_of_memory\"}");
    }
    esp_err_t result = send_json(request, NULL, encoded);
    cJSON_free(encoded);
    return result;
}

static void ota_abort(esp_ota_handle_t handle, int begun, const char *reason)
{
    if (begun) {
        (void)esp_ota_abort(handle);
    }
    if (g_ota.preflight_timer != NULL) {
        (void)xTimerStop(g_ota.preflight_timer, 0);
    }
    if (g_ota.lock != NULL &&
        xSemaphoreTake(g_ota.lock, portMAX_DELAY) == pdTRUE) {
        pj_ota_session_abort(&g_ota.session);
        state_set("failed", reason);
        mutations_release_locked();
        xSemaphoreGive(g_ota.lock);
    }
    ESP_LOGW(TAG, "OTA upload aborted: %s", reason);
}

static void restart_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(PJ_OTA_RESTART_DELAY_MS));
    esp_restart();
}

static esp_err_t ota_upload_handler(httpd_req_t *request)
{
    if (require_auth(request) != ESP_OK) {
        return ESP_OK;
    }
    char upload_id[PJ_OTA_UPLOAD_ID_LEN + 1U];
    if (httpd_req_get_hdr_value_str(request, "X-PJ-Upload-ID", upload_id,
                                    sizeof(upload_id)) != ESP_OK) {
        return send_json(request, "400 Bad Request",
                         "{\"error\":\"missing upload id\","
                         "\"code\":\"upload_id_missing\"}");
    }
    if (g_ota.lock == NULL ||
        xSemaphoreTake(g_ota.lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return send_json(request, "409 Conflict",
                         "{\"error\":\"OTA busy\",\"code\":\"ota_busy\"}");
    }
    if (g_ota.session.state == PJ_OTA_TRANSFER_READY &&
        (int32_t)(xTaskGetTickCount() - g_ota.ready_expires_at) >= 0) {
        pj_ota_session_abort(&g_ota.session);
        state_set("failed", "preflight_expired");
        mutations_release_locked();
    }
    char image_sha256[PJ_OTA_SHA256_HEX_LEN + 1U];
    char activate[6];
    int headers_match =
        httpd_req_get_hdr_value_str(request, "X-PJ-Image-SHA256", image_sha256,
                                    sizeof(image_sha256)) == ESP_OK &&
        strcmp(image_sha256, g_ota.manifest.sha256) == 0 &&
        httpd_req_get_hdr_value_str(request, "X-PJ-Activate", activate,
                                    sizeof(activate)) == ESP_OK &&
        strcmp(activate, "true") == 0;
    int length_matches = request->content_len > 0 &&
                         (uint64_t)request->content_len ==
                         g_ota.session.expected_bytes;
    pj_ota_transfer_result_t begin = PJ_OTA_TRANSFER_INVALID_STATE;
    pj_ota_transfer_result_t reservation = PJ_OTA_TRANSFER_INVALID_STATE;
    if (headers_match && length_matches) {
        int reserved = mutations_reserve_locked();
        reservation = pj_ota_session_reserve(&g_ota.session, upload_id,
                                             reserved);
        if (reservation == PJ_OTA_TRANSFER_OK) {
            begin = pj_ota_session_begin(&g_ota.session, upload_id);
        }
        if (reservation != PJ_OTA_TRANSFER_OK ||
            begin != PJ_OTA_TRANSFER_OK) {
            mutations_release_locked();
        }
    }
    if (!headers_match || !length_matches || begin != PJ_OTA_TRANSFER_OK) {
        const char *code = !headers_match ? "upload_headers_invalid" :
            !length_matches ? "image_size_invalid" :
            reservation == PJ_OTA_TRANSFER_BUSY ? "ota_busy" :
            reservation == PJ_OTA_TRANSFER_REPLAY ? "upload_replay" :
            reservation == PJ_OTA_TRANSFER_ID_MISMATCH ? "upload_id_mismatch" :
            begin == PJ_OTA_TRANSFER_REPLAY ? "upload_replay" :
            begin == PJ_OTA_TRANSFER_BUSY ? "ota_busy" :
            begin == PJ_OTA_TRANSFER_ID_MISMATCH ? "upload_id_mismatch" :
            "upload_state_invalid";
        const char *status = (!headers_match || !length_matches) ?
            "422 Unprocessable Entity" : "409 Conflict";
        char response[192];
        (void)snprintf(response, sizeof(response),
                       "{\"error\":\"OTA upload rejected\",\"code\":\"%s\"}",
                       code);
        xSemaphoreGive(g_ota.lock);
        return send_json(request, status, response);
    }
    (void)xTimerStop(g_ota.preflight_timer, 0);
    state_set("uploading", "");
    pj_ota_manifest_t manifest = g_ota.manifest;
    xSemaphoreGive(g_ota.lock);

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *update = esp_ota_get_next_update_partition(running);
    if (update == NULL || update == running || manifest.size > update->size) {
        ota_abort(0, 0, "inactive_partition_unavailable");
        return send_json(request, "422 Unprocessable Entity",
                         "{\"error\":\"inactive partition unavailable\","
                         "\"code\":\"active_partition_rejected\"}");
    }
    esp_ota_handle_t handle = 0;
    esp_err_t error = esp_ota_begin(update, (size_t)manifest.size, &handle);
    if (error != ESP_OK) {
        ota_abort(handle, 0, "ota_begin_failed");
        return send_json(request, "503 Service Unavailable",
                         "{\"error\":\"OTA begin failed\","
                         "\"code\":\"ota_begin_failed\"}");
    }
    uint8_t *buffer = malloc(PJ_OTA_STREAM_BUFFER_BYTES);
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    if (buffer == NULL || psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_setup(&hash, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        free(buffer);
        ota_abort(handle, 1, "ota_hash_unavailable");
        return send_json(request, "503 Service Unavailable",
                         "{\"error\":\"OTA resources unavailable\","
                         "\"code\":\"ota_resources_unavailable\"}");
    }
    uint64_t received = 0U;
    while (received < manifest.size) {
        size_t remaining = (size_t)(manifest.size - received);
        int wanted = (int)(remaining < PJ_OTA_STREAM_BUFFER_BYTES ?
                           remaining : PJ_OTA_STREAM_BUFFER_BYTES);
        int count = httpd_req_recv(request, (char *)buffer, wanted);
        if (count <= 0 || psa_hash_update(&hash, buffer, (size_t)count) != PSA_SUCCESS ||
            esp_ota_write(handle, buffer, (size_t)count) != ESP_OK) {
            (void)psa_hash_abort(&hash);
            free(buffer);
            ota_abort(handle, 1, count <= 0 ? "upload_interrupted" :
                      "flash_write_failed");
            return ESP_FAIL;
        }
        int locked = g_ota.lock != NULL &&
                     xSemaphoreTake(g_ota.lock, pdMS_TO_TICKS(100)) == pdTRUE;
        if (!locked ||
            pj_ota_session_write(&g_ota.session, upload_id, received,
                                 (size_t)count) != PJ_OTA_TRANSFER_OK) {
            if (locked) {
                xSemaphoreGive(g_ota.lock);
            }
            (void)psa_hash_abort(&hash);
            free(buffer);
            ota_abort(handle, 1, "upload_state_failed");
            return ESP_FAIL;
        }
        xSemaphoreGive(g_ota.lock);
        received += (uint64_t)count;
    }
    free(buffer);
    uint8_t digest[32];
    size_t digest_size = 0U;
    if (psa_hash_finish(&hash, digest, sizeof(digest), &digest_size) != PSA_SUCCESS ||
        digest_size != sizeof(digest)) {
        ota_abort(handle, 1, "image_digest_failed");
        return send_json(request, "422 Unprocessable Entity",
                         "{\"error\":\"image digest failed\","
                         "\"code\":\"image_digest_mismatch\"}");
    }
    char digest_hex[PJ_OTA_SHA256_HEX_LEN + 1U];
    bytes_to_hex(digest, sizeof(digest), digest_hex);
    int digest_matches = strcmp(digest_hex, manifest.sha256) == 0;
    if (!digest_matches) {
        ota_abort(handle, 1, "image_digest_mismatch");
        return send_json(request, "422 Unprocessable Entity",
                         "{\"error\":\"image digest mismatch\","
                         "\"code\":\"image_digest_mismatch\"}");
    }
    error = esp_ota_end(handle);
    if (error != ESP_OK) {
        ota_abort(0, 0, "image_invalid");
        return send_json(request, "422 Unprocessable Entity",
                         "{\"error\":\"invalid ESP application image\","
                         "\"code\":\"image_invalid\"}");
    }

    esp_app_desc_t description;
    esp_partition_pos_t position = {.offset = update->address, .size = update->size};
    esp_image_metadata_t metadata = {.start_addr = update->address};
    int descriptor_valid = esp_ota_get_partition_description(update, &description) == ESP_OK;
    int image_valid = esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &position,
                                       &metadata) == ESP_OK;
    int target_valid = image_valid &&
                       metadata.image.chip_id == CONFIG_IDF_FIRMWARE_CHIP_ID;
    pj_ota_check_t check = pj_ota_image_check(
        &manifest, descriptor_valid ? description.project_name : NULL,
        descriptor_valid ? description.version : NULL,
        descriptor_valid ? description.secure_version : 0U,
        digest_matches, image_valid && descriptor_valid, target_valid,
        update != running);
    if (check == PJ_OTA_CHECK_ACCEPTED &&
        !esp_efuse_check_secure_version(description.secure_version)) {
        check = PJ_OTA_CHECK_SECURE_VERSION;
    }
    if (check != PJ_OTA_CHECK_ACCEPTED) {
        ota_abort(0, 0, pj_ota_check_code(check));
        char response[256];
        (void)snprintf(response, sizeof(response),
                       "{\"error\":\"flashed image rejected\","
                       "\"code\":\"%s\"}", pj_ota_check_code(check));
        return send_json(request, "422 Unprocessable Entity", response);
    }
    if (record_write("pending_reboot") != ESP_OK ||
        esp_ota_set_boot_partition(update) != ESP_OK) {
        ota_abort(0, 0, "activation_failed");
        return send_json(request, "503 Service Unavailable",
                         "{\"error\":\"OTA activation failed\","
                         "\"code\":\"activation_failed\"}");
    }
    if (xSemaphoreTake(g_ota.lock, portMAX_DELAY) == pdTRUE) {
        (void)pj_ota_session_finish(&g_ota.session, upload_id);
        state_set("pending_reboot", "");
        xSemaphoreGive(g_ota.lock);
    }
    char response[384];
    (void)snprintf(response, sizeof(response),
                   "{\"device_id\":\"%s\",\"state\":\"pending_reboot\","
                   "\"upload_id\":\"%s\",\"version\":\"%s\","
                   "\"sha256\":\"%s\"}", g_ota.device_id, upload_id,
                   manifest.version, manifest.sha256);
    esp_err_t result = send_json(request, "202 Accepted", response);
    if (xTaskCreate(restart_task, "pj-ota-restart", 2048, NULL, 5,
                    NULL) != pdPASS) {
        vTaskDelay(pdMS_TO_TICKS(PJ_OTA_RESTART_DELAY_MS));
        esp_restart();
    }
    return result;
}

static esp_err_t register_uri(httpd_handle_t server, const char *uri,
                              httpd_method_t method,
                              esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t specification = {
        .uri = uri,
        .method = method,
        .handler = handler,
        .user_ctx = NULL,
    };
    return httpd_register_uri_handler(server, &specification);
}

esp_err_t pj_ota_register_http(httpd_handle_t server)
{
    if (!g_ota.initialized || server == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = register_uri(server, "/v1/ota", HTTP_GET,
                                    ota_status_handler);
    if (result == ESP_OK) {
        result = register_uri(server, "/v1/ota/preflight", HTTP_POST,
                              ota_preflight_handler);
    }
    if (result == ESP_OK) {
        result = register_uri(server, "/v1/ota", HTTP_POST,
                              ota_upload_handler);
    }
    return result;
}

void pj_ota_confirm_boot_health(int healthy)
{
    if (!g_ota.initialized || g_ota.lock == NULL ||
        xSemaphoreTake(g_ota.lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (!g_ota.boot_pending_verify) {
        xSemaphoreGive(g_ota.lock);
        return;
    }
    if (healthy) {
        esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
        if (result == ESP_OK) {
            g_ota.boot_pending_verify = 0;
            state_set("confirmed", "");
            (void)record_write("confirmed");
            ESP_LOGI(TAG, "OTA image confirmed healthy");
            xSemaphoreGive(g_ota.lock);
            return;
        }
        state_set("failed", "health_confirmation_failed");
        ESP_LOGE(TAG, "OTA health confirmation failed: %s",
                 esp_err_to_name(result));
        xSemaphoreGive(g_ota.lock);
        return;
    }
    state_set("rollback_requested", "post_boot_health_failed");
    (void)record_write("rollback_requested");
    ESP_LOGE(TAG, "Post-boot health failed; rolling back OTA image");
    esp_err_t result = esp_ota_mark_app_invalid_rollback_and_reboot();
    if (result != ESP_OK) {
        state_set("failed", "rollback_unavailable");
        ESP_LOGE(TAG, "OTA rollback failed: %s", esp_err_to_name(result));
    }
    xSemaphoreGive(g_ota.lock);
}

int pj_ota_write_enabled(void)
{
    return g_ota.initialized && g_ota.nvs_ready &&
           g_ota.verification_key_ready && PJ_OTA_IDF_SIGNED_APP_ENABLED &&
           g_ota.preflight_timer != NULL &&
           g_ota.reserve_mutations != NULL && g_ota.release_mutations != NULL;
}
