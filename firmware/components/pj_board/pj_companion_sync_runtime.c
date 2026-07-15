#include "pj_board.h"
#include "pj_companion_auth.h"
#include "pj_companion_sync.h"

#ifdef ESP_PLATFORM

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "mdns.h"
#include "nvs.h"

#define PJ_COMPANION_SERVICE "_pj-companion"
#define PJ_COMPANION_PROTO "_tcp"
#define PJ_COMPANION_DISCOVERY_TIMEOUT_MS 3000U
#define PJ_COMPANION_HTTP_TIMEOUT_MS 8000
#define PJ_COMPANION_POLL_MS 2000U
#define PJ_COMPANION_MAX_POLLS 3600U
#define PJ_COMPANION_RETRY_MS 10000U
#define PJ_COMPANION_MAX_RETRIES 720U
#define PJ_COMPANION_MAX_RESULTS 8U
#define PJ_COMPANION_RESPONSE_BYTES 1024U
#define PJ_COMPANION_TASK_STACK 8192U
#define PJ_COMPANION_NVS_NAMESPACE "pj"
#define PJ_COMPANION_NVS_KEY "sync_req"

static const char *TAG = "pj-companion-sync";

typedef struct {
    char body[PJ_COMPANION_RESPONSE_BYTES];
    size_t used;
    int truncated;
} companion_http_response_t;

static SemaphoreHandle_t g_sync_mutex;
static pj_companion_sync_state_t g_sync_state;
static int g_sync_initialized;
static int g_sync_task_running;
static int g_sync_restart_requested;
static int g_sync_update_pending;
static int g_sync_persisted_record_valid;
static pj_companion_sync_record_t g_sync_persisted_record;
static int g_sync_auth_self_test_state;

static int sync_mutex_init(void)
{
    if (g_sync_mutex != NULL) {
        return 1;
    }
    g_sync_mutex = xSemaphoreCreateMutex();
    return g_sync_mutex != NULL;
}

static int persist_state_locked(void)
{
    pj_companion_sync_record_t record;
    pj_companion_sync_record_from_state(&g_sync_state, &record);
    if (g_sync_persisted_record_valid &&
        pj_companion_sync_record_equal(&record,
                                       &g_sync_persisted_record)) {
        return 1;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PJ_COMPANION_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, PJ_COMPANION_NVS_KEY, &record,
                           sizeof(record));
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to persist sync request state: %s",
                 esp_err_to_name(err));
    }
    if (err == ESP_OK) {
        g_sync_persisted_record = record;
        g_sync_persisted_record_valid = 1;
    }
    return err == ESP_OK;
}

static int persist_state_callback(void *context)
{
    (void)context;
    return persist_state_locked();
}

static void set_runtime_error_locked(const char *message)
{
    g_sync_state.phase = PJ_COMPANION_SYNC_PROTOCOL_FAILED;
    g_sync_state.transport = PJ_COMPANION_SYNC_TRANSPORT_NONE;
    g_sync_state.online = 0;
    (void)snprintf(g_sync_state.error, sizeof(g_sync_state.error), "%s",
                   message == NULL ? "Sync request failed" : message);
    g_sync_update_pending = 1;
}

static int initialize_state_locked(void)
{
    if (g_sync_initialized) {
        return 1;
    }
    pj_companion_sync_state_init(&g_sync_state);
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PJ_COMPANION_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        g_sync_persisted_record_valid = 0;
        g_sync_initialized = 1;
        return 1;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to open sync request state: %s",
                 esp_err_to_name(err));
        return 0;
    }
    size_t size = 0U;
    err = nvs_get_blob(nvs, PJ_COMPANION_NVS_KEY, NULL, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs);
        g_sync_persisted_record_valid = 0;
        g_sync_initialized = 1;
        return 1;
    }
    pj_companion_sync_record_t record;
    if (err == ESP_OK && size == sizeof(record)) {
        err = nvs_get_blob(nvs, PJ_COMPANION_NVS_KEY, &record, &size);
    }
    nvs_close(nvs);
    if (err != ESP_OK ||
        !pj_companion_sync_state_from_record_bytes(&g_sync_state, &record,
                                                   size)) {
        pj_companion_sync_state_init(&g_sync_state);
        set_runtime_error_locked("Stored sync request is invalid; start Sync again");
        ESP_LOGE(TAG, "Ignoring invalid or truncated sync request record");
        g_sync_persisted_record_valid = 0;
    } else {
        g_sync_persisted_record = record;
        g_sync_persisted_record_valid = 1;
    }
    if (pj_companion_sync_state_pending(&g_sync_state)) {
        ESP_LOGI(TAG, "Restored pending sync generation=%" PRIu32,
                 pj_companion_sync_state_claim_generation(&g_sync_state));
    }
    g_sync_initialized = 1;
    g_sync_update_pending = 1;
    return 1;
}

static int txt_value_equals(const mdns_result_t *result, const char *key,
                            const char *expected)
{
    if (result == NULL || key == NULL || expected == NULL) {
        return 0;
    }
    size_t expected_size = strlen(expected);
    for (size_t i = 0; i < result->txt_count; i++) {
        if (result->txt[i].key == NULL || strcmp(result->txt[i].key, key) != 0 ||
            result->txt[i].value == NULL) {
            continue;
        }
        size_t value_size = result->txt_value_len == NULL ?
            strlen(result->txt[i].value) : result->txt_value_len[i];
        return value_size == expected_size &&
               memcmp(result->txt[i].value, expected, expected_size) == 0;
    }
    return 0;
}

static int result_ipv4(const mdns_result_t *result, esp_ip4_addr_t *address)
{
    for (const mdns_ip_addr_t *item = result == NULL ? NULL : result->addr;
         item != NULL; item = item->next) {
        if (item->addr.type == ESP_IPADDR_TYPE_V4) {
            *address = item->addr.u_addr.ip4;
            return 1;
        }
    }
    if (result != NULL && result->hostname != NULL &&
        mdns_query_a(result->hostname, PJ_COMPANION_DISCOVERY_TIMEOUT_MS,
                     address) == ESP_OK) {
        return 1;
    }
    return 0;
}

static int discover_companion(const char *device_id, char *base_url,
                              size_t base_url_size)
{
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr(PJ_COMPANION_SERVICE, PJ_COMPANION_PROTO,
                                   PJ_COMPANION_DISCOVERY_TIMEOUT_MS,
                                   PJ_COMPANION_MAX_RESULTS, &results);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Companion mDNS query failed: %s", esp_err_to_name(err));
        return 0;
    }
    int matches = 0;
    char selected[96] = {0};
    for (mdns_result_t *result = results; result != NULL; result = result->next) {
        if (result->port == 0U ||
            !txt_value_equals(result, "device_id", device_id) ||
            !txt_value_equals(result, "path", "/v1/sync") ||
            !txt_value_equals(result, "api", "2") ||
            !txt_value_equals(result, "auth", "hmac-sha256-v1")) {
            continue;
        }
        esp_ip4_addr_t address;
        if (!result_ipv4(result, &address)) {
            continue;
        }
        char candidate[96];
        (void)snprintf(candidate, sizeof(candidate), "http://" IPSTR ":%u",
                       IP2STR(&address), (unsigned)result->port);
        if (matches == 0) {
            (void)snprintf(selected, sizeof(selected), "%s", candidate);
            matches = 1;
        } else if (strcmp(selected, candidate) != 0) {
            matches++;
        }
    }
    mdns_query_results_free(results);
    if (matches != 1) {
        ESP_LOGW(TAG, "Companion discovery found %d matching services", matches);
        return 0;
    }
    (void)snprintf(base_url, base_url_size, "%s", selected);
    return 1;
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    companion_http_response_t *response = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || response == NULL ||
        event->data == NULL || event->data_len <= 0) {
        return ESP_OK;
    }
    size_t available = sizeof(response->body) - response->used - 1U;
    size_t incoming = (size_t)event->data_len;
    size_t copied = incoming < available ? incoming : available;
    if (copied > 0U) {
        memcpy(response->body + response->used, event->data, copied);
        response->used += copied;
        response->body[response->used] = '\0';
    }
    if (copied != incoming) {
        response->truncated = 1;
    }
    return ESP_OK;
}

static int companion_request(const char *url,
                             esp_http_client_method_t method, const char *body,
                             companion_http_response_t *response)
{
    memset(response, 0, sizeof(*response));
    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = PJ_COMPANION_HTTP_TIMEOUT_MS,
        .disable_auto_redirect = true,
        .event_handler = http_event,
        .user_data = response,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return -1;
    }
    esp_err_t err = esp_http_client_set_header(client, "Accept", "application/json");
    if (err == ESP_OK && body != NULL) {
        err = esp_http_client_set_header(client, "Content-Type", "application/json");
        if (err == ESP_OK) {
            err = esp_http_client_set_post_field(client, body,
                                                 (int)strlen(body));
        }
    }
    if (err == ESP_OK) {
        err = esp_http_client_perform(client);
    }
    int status = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
    esp_http_client_cleanup(client);
    if (err != ESP_OK || response->truncated) {
        ESP_LOGW(TAG, "Companion HTTP request failed: %s",
                 response->truncated ? "response too large" : esp_err_to_name(err));
        return -1;
    }
    return status;
}

static void request_nonce(char output[PJ_COMPANION_AUTH_NONCE_BYTES])
{
    static const char hex[] = "0123456789abcdef";
    unsigned char random[16];
    esp_fill_random(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); i++) {
        output[i * 2U] = hex[random[i] >> 4U];
        output[i * 2U + 1U] = hex[random[i] & 0x0fU];
    }
    output[32] = '\0';
}

static void attempt_failed(uint32_t generation,
                           pj_companion_sync_phase_t failure_phase,
                           const char *message)
{
    xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
    if (pj_companion_sync_state_attempt_failed(
            &g_sync_state, generation, PJ_COMPANION_SYNC_TRANSPORT_LAN,
            failure_phase, message)) {
        g_sync_update_pending = 1;
    }
    xSemaphoreGive(g_sync_mutex);
    ESP_LOGW(TAG, "%s", message);
}

static void publish_pending_error(pj_companion_sync_phase_t phase,
                                  const char *message)
{
    xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
    if (pj_companion_sync_state_pending(&g_sync_state) &&
        g_sync_state.active_generation == 0U) {
        g_sync_state.phase = phase;
        g_sync_state.transport = PJ_COMPANION_SYNC_TRANSPORT_NONE;
        g_sync_state.online = 0;
        (void)snprintf(g_sync_state.error, sizeof(g_sync_state.error), "%s",
                       message);
        g_sync_update_pending = 1;
    }
    xSemaphoreGive(g_sync_mutex);
    ESP_LOGW(TAG, "%s", message);
}

static pj_companion_sync_apply_result_t apply_lan_progress(
    uint32_t generation, const char *operation_id, const char *phase,
    int total, int pending, int transferred, int failed, const char *error,
    const char *device_id, pj_companion_sync_phase_t *resulting_phase)
{
    xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
    pj_companion_sync_apply_result_t applied =
        pj_companion_sync_state_progress_transactional(
        &g_sync_state, generation, operation_id,
        PJ_COMPANION_SYNC_TRANSPORT_LAN, phase, total, pending, transferred,
        failed, error, device_id, persist_state_callback, NULL);
    if (applied == PJ_COMPANION_SYNC_APPLY_CHANGED) {
        g_sync_update_pending = 1;
    }
    *resulting_phase = g_sync_state.phase;
    xSemaphoreGive(g_sync_mutex);
    return applied;
}

static int run_one_lan_sync(void)
{
    if (g_sync_auth_self_test_state == 0) {
        g_sync_auth_self_test_state =
            pj_companion_auth_self_test() ? 1 : -1;
        if (g_sync_auth_self_test_state < 0) {
            ESP_LOGE(TAG, "Companion authentication self-test failed");
        }
    }
    if (g_sync_auth_self_test_state < 0) {
        publish_pending_error(
            PJ_COMPANION_SYNC_PROTOCOL_FAILED,
            "Companion authentication self-test failed");
        return -1;
    }
    pj_board_status_t status = pj_board_status();
    if (!pj_companion_auth_token_provisioned(status.token) ||
        !status.wifi_diagnostics.provisioned) {
        publish_pending_error(
            PJ_COMPANION_SYNC_AUTH_FAILED,
            "Pairing token is not provisioned; USB sync remains pending");
        return -1;
    }
    if (status.wifi != PJ_BOARD_SERVICE_READY || status.ip_addr[0] == '\0' ||
        strcmp(status.ip_addr, "0.0.0.0") == 0) {
        publish_pending_error(
            PJ_COMPANION_SYNC_OFFLINE,
            "Wi-Fi is not connected; USB sync remains pending");
        return 0;
    }
    char base_url[96];
    if (!discover_companion(status.device_id, base_url, sizeof(base_url))) {
        publish_pending_error(
            PJ_COMPANION_SYNC_OFFLINE,
            "No paired LAN companion; USB sync remains pending");
        return 0;
    }

    uint32_t generation;
    uint64_t requested_ms = 0U;
    char operation_id[PJ_COMPANION_SYNC_OPERATION_ID_BYTES];
    xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
    generation = pj_companion_sync_state_claim_generation(&g_sync_state);
    (void)snprintf(operation_id, sizeof(operation_id), "%s",
                   g_sync_state.operation_id);
    pj_companion_sync_state_t before = g_sync_state;
    pj_companion_sync_claim_result_t claim = pj_companion_sync_state_claim(
        &g_sync_state, generation, operation_id,
        PJ_COMPANION_SYNC_TRANSPORT_LAN);
    if (claim == PJ_COMPANION_SYNC_CLAIM_STARTED && !persist_state_locked()) {
        g_sync_state = before;
        set_runtime_error_locked("Unable to save the sync claim");
        claim = PJ_COMPANION_SYNC_CLAIM_STORE_FAILED;
    } else if (claim == PJ_COMPANION_SYNC_CLAIM_STARTED ||
               claim == PJ_COMPANION_SYNC_CLAIM_ATTACHED) {
        requested_ms = g_sync_state.active_requested_ms;
        g_sync_update_pending = 1;
    }
    xSemaphoreGive(g_sync_mutex);
    if (claim == PJ_COMPANION_SYNC_CLAIM_STORE_FAILED) {
        return -1;
    }
    if (claim == PJ_COMPANION_SYNC_CLAIM_BUSY ||
        claim == PJ_COMPANION_SYNC_CLAIM_STALE) {
        return 0;
    }

    xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
    int discovered = pj_companion_sync_state_discovered(&g_sync_state,
                                                         generation);
    if (discovered) {
        g_sync_update_pending = 1;
    }
    xSemaphoreGive(g_sync_mutex);
    if (!discovered) {
        return 0;
    }

    char start_nonce[PJ_COMPANION_AUTH_NONCE_BYTES];
    request_nonce(start_nonce);
    char *request_body = NULL;
    if (!pj_companion_auth_build_request_json(
            status.token, "start", status.device_id, status.ip_addr,
            operation_id, generation, requested_ms, start_nonce,
            &request_body)) {
        attempt_failed(generation, PJ_COMPANION_SYNC_PROTOCOL_FAILED,
                       "Unable to authenticate companion request");
        return -1;
    }

    char url[192];
    (void)snprintf(url, sizeof(url), "%s/v1/sync", base_url);
    companion_http_response_t response;
    int http_status = companion_request(url, HTTP_METHOD_POST, request_body,
                                        &response);
    cJSON_free(request_body);
    if (http_status != 200 && http_status != 202) {
        pj_companion_sync_phase_t failure = http_status < 0 ?
            PJ_COMPANION_SYNC_OFFLINE :
            http_status == 503 ? PJ_COMPANION_SYNC_OFFLINE :
            http_status == 401 ? PJ_COMPANION_SYNC_AUTH_FAILED :
            PJ_COMPANION_SYNC_PROTOCOL_FAILED;
        attempt_failed(generation, failure,
                       http_status < 0 ?
                       "Companion connection failed" :
                       http_status == 503 ?
                       "Companion sync state is temporarily unavailable" :
                       http_status == 401 ?
                       "Companion authentication failed" :
                       "Companion rejected the sync protocol");
        return failure == PJ_COMPANION_SYNC_OFFLINE ? 0 : -1;
    }

    pj_companion_auth_response_t progress;
    pj_companion_auth_result_t verified =
        pj_companion_auth_verify_response_json(
            status.token, response.body, response.used, "start", start_nonce,
            status.device_id, operation_id, generation, requested_ms,
            &progress);
    if (verified != PJ_COMPANION_AUTH_OK) {
        attempt_failed(
            generation,
            verified == PJ_COMPANION_AUTH_FAILED ?
                PJ_COMPANION_SYNC_AUTH_FAILED :
                PJ_COMPANION_SYNC_PROTOCOL_FAILED,
            verified == PJ_COMPANION_AUTH_FAILED ?
                "Companion response authentication failed" :
                "Companion returned an invalid response");
        return -1;
    }
    pj_companion_sync_phase_t current;
    pj_companion_sync_apply_result_t applied = apply_lan_progress(
        generation, operation_id, progress.state, progress.total,
        progress.pending, progress.transferred, progress.failed,
        progress.error, status.device_id, &current);
    if (applied == PJ_COMPANION_SYNC_APPLY_STORE_FAILED) {
        attempt_failed(generation, PJ_COMPANION_SYNC_PROTOCOL_FAILED,
                       "Unable to save companion sync completion");
        return -1;
    }
    if (applied == PJ_COMPANION_SYNC_APPLY_REJECTED) {
        attempt_failed(generation, PJ_COMPANION_SYNC_PROTOCOL_FAILED,
                       "Companion start progress was inconsistent");
        return -1;
    }
    if (current == PJ_COMPANION_SYNC_SUCCEEDED ||
        current == PJ_COMPANION_SYNC_FAILED ||
        current == PJ_COMPANION_SYNC_PENDING) {
        return current == PJ_COMPANION_SYNC_PENDING;
    }

    unsigned request_failures = 0U;
    for (unsigned poll = 0; poll < PJ_COMPANION_MAX_POLLS; poll++) {
        vTaskDelay(pdMS_TO_TICKS(PJ_COMPANION_POLL_MS));
        char status_nonce[PJ_COMPANION_AUTH_NONCE_BYTES];
        request_nonce(status_nonce);
        char *status_body = NULL;
        if (!pj_companion_auth_build_request_json(
                status.token, "status", status.device_id, status.ip_addr,
                operation_id, generation, requested_ms, status_nonce,
                &status_body)) {
            attempt_failed(generation, PJ_COMPANION_SYNC_PROTOCOL_FAILED,
                           "Unable to authenticate progress request");
            return -1;
        }
        (void)snprintf(url, sizeof(url), "%s/v1/sync/status", base_url);
        http_status = companion_request(url, HTTP_METHOD_POST, status_body,
                                        &response);
        cJSON_free(status_body);
        if (http_status < 0) {
            request_failures++;
            if (request_failures < 3U) {
                continue;
            }
            attempt_failed(generation, PJ_COMPANION_SYNC_OFFLINE,
                           "Companion progress connection was lost");
            return 0;
        }
        request_failures = 0U;
        if (http_status == 503) {
            attempt_failed(
                generation, PJ_COMPANION_SYNC_OFFLINE,
                "Companion sync state is temporarily unavailable");
            return 0;
        }
        verified = http_status == 200 ?
            pj_companion_auth_verify_response_json(
                status.token, response.body, response.used, "status",
                status_nonce, status.device_id, operation_id, generation,
                requested_ms, &progress) :
            (http_status == 401 ? PJ_COMPANION_AUTH_FAILED :
             PJ_COMPANION_AUTH_PROTOCOL_ERROR);
        if (verified != PJ_COMPANION_AUTH_OK) {
            attempt_failed(
                generation,
                verified == PJ_COMPANION_AUTH_FAILED ?
                    PJ_COMPANION_SYNC_AUTH_FAILED :
                    PJ_COMPANION_SYNC_PROTOCOL_FAILED,
                verified == PJ_COMPANION_AUTH_FAILED ?
                    "Companion progress authentication failed" :
                    "Companion returned invalid progress");
            return -1;
        }
        applied = apply_lan_progress(
            generation, operation_id, progress.state, progress.total,
            progress.pending, progress.transferred, progress.failed,
            progress.error, status.device_id, &current);
        if (applied == PJ_COMPANION_SYNC_APPLY_STORE_FAILED) {
            attempt_failed(generation, PJ_COMPANION_SYNC_PROTOCOL_FAILED,
                           "Unable to save companion sync completion");
            return -1;
        }
        if (applied == PJ_COMPANION_SYNC_APPLY_REJECTED) {
            attempt_failed(generation, PJ_COMPANION_SYNC_PROTOCOL_FAILED,
                           "Companion progress did not match the claimed request");
            return -1;
        }
        if (current == PJ_COMPANION_SYNC_SUCCEEDED ||
            current == PJ_COMPANION_SYNC_FAILED ||
            current == PJ_COMPANION_SYNC_PENDING) {
            ESP_LOGI(TAG, "Companion sync %s generation=%" PRIu32
                     " transferred=%d failed=%d",
                     pj_companion_sync_phase_name(current), generation,
                     progress.transferred, progress.failed);
            return current == PJ_COMPANION_SYNC_PENDING;
        }
    }
    attempt_failed(generation, PJ_COMPANION_SYNC_OFFLINE,
                   "Companion sync exceeded the two-hour timeout");
    return 0;
}

static void companion_sync_task(void *context)
{
    (void)context;
    for (;;) {
        for (unsigned retry = 0U; retry < PJ_COMPANION_MAX_RETRIES; retry++) {
            int result = run_one_lan_sync();
            if (result > 0) {
                retry = 0U;
                taskYIELD();
                continue;
            }
            if (result < 0) {
                break;
            }
            xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
            int pending = pj_companion_sync_state_pending(&g_sync_state);
            int usb_active = pj_companion_sync_state_active(&g_sync_state) &&
                             g_sync_state.transport ==
                                 PJ_COMPANION_SYNC_TRANSPORT_USB;
            xSemaphoreGive(g_sync_mutex);
            if (!pending || usb_active) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(PJ_COMPANION_RETRY_MS));
        }
        xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
        int pending = pj_companion_sync_state_pending(&g_sync_state);
        int usb_active = pj_companion_sync_state_active(&g_sync_state) &&
                         g_sync_state.transport ==
                             PJ_COMPANION_SYNC_TRANSPORT_USB;
        int restart = g_sync_restart_requested && pending && !usb_active;
        g_sync_restart_requested = 0;
        if (!restart) {
            g_sync_task_running = 0;
        }
        xSemaphoreGive(g_sync_mutex);
        if (!restart) {
            break;
        }
    }
    vTaskDelete(NULL);
}

static int start_task_if_pending(void)
{
    xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
    if (g_sync_task_running || !pj_companion_sync_state_pending(&g_sync_state)) {
        xSemaphoreGive(g_sync_mutex);
        return 1;
    }
    g_sync_task_running = 1;
    xSemaphoreGive(g_sync_mutex);
    if (xTaskCreate(companion_sync_task, "pj-companion-sync",
                    PJ_COMPANION_TASK_STACK, NULL, 4, NULL) == pdPASS) {
        return 1;
    }
    xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
    g_sync_task_running = 0;
    set_runtime_error_locked("Unable to start companion sync task");
    xSemaphoreGive(g_sync_mutex);
    return 0;
}

int pj_board_companion_sync_start(void)
{
    if (!sync_mutex_init()) {
        ESP_LOGE(TAG, "Unable to allocate companion sync mutex");
        return 0;
    }
    pj_board_status_t status = pj_board_status();
    struct timeval now = {0};
    uint64_t requested_ms = 0U;
    if (gettimeofday(&now, NULL) == 0 && now.tv_sec >= 0) {
        uint64_t seconds = (uint64_t)now.tv_sec;
        uint64_t milliseconds = 0U;
        if (seconds <= PJ_COMPANION_AUTH_MAX_REQUESTED_MS / 1000U) {
            milliseconds = seconds * 1000U +
                           (uint64_t)(now.tv_usec / 1000);
        }
        if (milliseconds > 0U &&
            milliseconds <= PJ_COMPANION_AUTH_MAX_REQUESTED_MS) {
            requested_ms = milliseconds;
        }
    }
    xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
    if (!initialize_state_locked()) {
        xSemaphoreGive(g_sync_mutex);
        return 0;
    }
    pj_companion_sync_state_t before = g_sync_state;
    if (!pj_companion_sync_state_request(&g_sync_state, status.device_id,
                                         requested_ms) ||
        !persist_state_locked()) {
        g_sync_state = before;
        set_runtime_error_locked("Unable to save the new sync request");
        xSemaphoreGive(g_sync_mutex);
        return 0;
    }
    g_sync_update_pending = 1;
    if (g_sync_task_running) {
        g_sync_restart_requested = 1;
    }
    ESP_LOGI(TAG, "Queued durable sync generation=%" PRIu32,
             g_sync_state.requested_generation);
    xSemaphoreGive(g_sync_mutex);
    return start_task_if_pending();
}

int pj_board_companion_sync_resume(void)
{
    if (!sync_mutex_init()) {
        return 0;
    }
    xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
    int initialized = initialize_state_locked();
    int pending = initialized && pj_companion_sync_state_pending(&g_sync_state);
    xSemaphoreGive(g_sync_mutex);
    return !pending || start_task_if_pending();
}

int pj_board_companion_sync_snapshot(pj_companion_sync_state_t *snapshot)
{
    if (snapshot == NULL || !sync_mutex_init()) {
        return 0;
    }
    xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
    int initialized = initialize_state_locked();
    if (initialized) {
        *snapshot = g_sync_state;
    }
    xSemaphoreGive(g_sync_mutex);
    return initialized;
}

int pj_board_companion_sync_usb_claim(
    uint32_t generation, const char *operation_id,
    pj_companion_sync_state_t *snapshot)
{
    if (!sync_mutex_init()) {
        return -1;
    }
    xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
    if (!initialize_state_locked()) {
        xSemaphoreGive(g_sync_mutex);
        return -1;
    }
    pj_companion_sync_state_t before = g_sync_state;
    pj_companion_sync_claim_result_t result = pj_companion_sync_state_claim(
        &g_sync_state, generation, operation_id,
        PJ_COMPANION_SYNC_TRANSPORT_USB);
    if (result == PJ_COMPANION_SYNC_CLAIM_STARTED && !persist_state_locked()) {
        g_sync_state = before;
        result = PJ_COMPANION_SYNC_CLAIM_STORE_FAILED;
    }
    if (result == PJ_COMPANION_SYNC_CLAIM_STARTED ||
        result == PJ_COMPANION_SYNC_CLAIM_ATTACHED) {
        g_sync_update_pending = 1;
    }
    if (snapshot != NULL) {
        *snapshot = g_sync_state;
    }
    xSemaphoreGive(g_sync_mutex);
    return (int)result;
}

int pj_board_companion_sync_usb_progress(
    uint32_t generation, const char *operation_id, const char *phase,
    int total, int pending, int transferred, int failed, const char *error,
    pj_companion_sync_state_t *snapshot)
{
    if (!sync_mutex_init()) {
        return -1;
    }
    pj_board_status_t status = pj_board_status();
    xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
    if (!initialize_state_locked()) {
        xSemaphoreGive(g_sync_mutex);
        return -1;
    }
    pj_companion_sync_apply_result_t result =
        pj_companion_sync_state_progress_transactional(
        &g_sync_state, generation, operation_id,
        PJ_COMPANION_SYNC_TRANSPORT_USB, phase, total, pending, transferred,
        failed, error, status.device_id, persist_state_callback, NULL);
    if (result == PJ_COMPANION_SYNC_APPLY_CHANGED) {
        g_sync_update_pending = 1;
    }
    if (snapshot != NULL) {
        *snapshot = g_sync_state;
    }
    xSemaphoreGive(g_sync_mutex);
    return (int)result;
}

int pj_board_companion_sync_scoped_auth_valid(const char *authorization,
                                               const char *method,
                                               const char *uri,
                                               const char *token)
{
    if (authorization == NULL || token == NULL ||
        !pj_companion_sync_scope_allowed(method, uri) ||
        !sync_mutex_init()) {
        return 0;
    }
    pj_board_status_t status = pj_board_status();
    xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
    int initialized = initialize_state_locked();
    int valid = initialized && g_sync_state.active_generation != 0U &&
        g_sync_state.transport == PJ_COMPANION_SYNC_TRANSPORT_LAN &&
        pj_companion_auth_scoped_header_valid(
            authorization, token, status.device_id,
            g_sync_state.operation_id, g_sync_state.active_generation,
            g_sync_state.active_requested_ms);
    xSemaphoreGive(g_sync_mutex);
    return valid;
}

int pj_board_consume_companion_sync_update(pj_ui_context_t *ui)
{
    if (ui == NULL || g_sync_mutex == NULL) {
        return 0;
    }
    pj_companion_sync_state_t snapshot;
    xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
    if (!g_sync_update_pending) {
        xSemaphoreGive(g_sync_mutex);
        return 0;
    }
    snapshot = g_sync_state;
    g_sync_update_pending = 0;
    xSemaphoreGive(g_sync_mutex);
    pj_ui_set_sync_state(ui, snapshot.pending, snapshot.transferred,
                         snapshot.online);
    pj_ui_set_sync_detail(ui, pj_companion_sync_phase_name(snapshot.phase),
                          snapshot.failed, snapshot.error,
                          pj_companion_sync_state_pending(&snapshot));
    return 1;
}

#else

int pj_board_companion_sync_start(void)
{
    return 0;
}

int pj_board_companion_sync_resume(void)
{
    return 0;
}

int pj_board_companion_sync_snapshot(pj_companion_sync_state_t *snapshot)
{
    (void)snapshot;
    return 0;
}

int pj_board_companion_sync_usb_claim(
    uint32_t generation, const char *operation_id,
    pj_companion_sync_state_t *snapshot)
{
    (void)generation;
    (void)operation_id;
    (void)snapshot;
    return -1;
}

int pj_board_companion_sync_usb_progress(
    uint32_t generation, const char *operation_id, const char *phase,
    int total, int pending, int transferred, int failed, const char *error,
    pj_companion_sync_state_t *snapshot)
{
    (void)generation;
    (void)operation_id;
    (void)phase;
    (void)total;
    (void)pending;
    (void)transferred;
    (void)failed;
    (void)error;
    (void)snapshot;
    return -1;
}

int pj_board_companion_sync_scoped_auth_valid(const char *authorization,
                                               const char *method,
                                               const char *uri,
                                               const char *token)
{
    (void)authorization;
    (void)method;
    (void)uri;
    (void)token;
    return 0;
}

int pj_board_consume_companion_sync_update(pj_ui_context_t *ui)
{
    (void)ui;
    return 0;
}

#endif
