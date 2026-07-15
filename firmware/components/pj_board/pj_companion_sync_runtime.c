#include "pj_board.h"
#include "pj_companion_sync.h"

#ifdef ESP_PLATFORM

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
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
    return err == ESP_OK;
}

static void set_runtime_error_locked(const char *message)
{
    g_sync_state.phase = PJ_COMPANION_SYNC_FAILED;
    g_sync_state.transport = PJ_COMPANION_SYNC_TRANSPORT_NONE;
    g_sync_state.active_generation = 0U;
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
    } else if (pj_companion_sync_state_pending(&g_sync_state)) {
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
            !txt_value_equals(result, "api", "1")) {
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

static int companion_request(const char *url, const char *token,
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
    char authorization[80];
    (void)snprintf(authorization, sizeof(authorization), "Bearer %s", token);
    esp_err_t err = esp_http_client_set_header(client, "Authorization", authorization);
    if (err == ESP_OK) {
        err = esp_http_client_set_header(client, "Accept", "application/json");
    }
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

static int json_nonnegative_int(const cJSON *json, const char *name, int *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
        item->valuedouble > (double)INT_MAX ||
        item->valuedouble != (double)item->valueint) {
        return 0;
    }
    *value = item->valueint;
    return 1;
}

static int json_generation(const cJSON *json, uint32_t *generation)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, "generation");
    if (!cJSON_IsNumber(item) || item->valuedouble < 1.0 ||
        item->valuedouble > (double)UINT32_MAX) {
        return 0;
    }
    uint32_t parsed = (uint32_t)item->valuedouble;
    if ((double)parsed != item->valuedouble) {
        return 0;
    }
    *generation = parsed;
    return 1;
}

static int operation_id_valid(const char *value, size_t maximum_size)
{
    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    size_t size = strlen(value);
    if (size >= maximum_size) {
        return 0;
    }
    for (size_t i = 0; i < size; i++) {
        unsigned char ch = (unsigned char)value[i];
        if (!isalnum(ch) && ch != '_' && ch != '-') {
            return 0;
        }
    }
    return 1;
}

static int parse_progress(const char *body, uint32_t *generation,
                          char *operation_id, size_t operation_id_size,
                          char *phase, size_t phase_size, int *pending,
                          int *transferred, int *failed, char *error,
                          size_t error_size)
{
    cJSON *json = cJSON_Parse(body);
    if (!cJSON_IsObject(json)) {
        cJSON_Delete(json);
        return 0;
    }
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(json, "operation_id");
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(json, "state");
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(json, "error");
    int valid = json_generation(json, generation) && cJSON_IsString(id) &&
                operation_id_valid(id->valuestring, operation_id_size) &&
                cJSON_IsString(state) && state->valuestring[0] != '\0' &&
                strlen(state->valuestring) < phase_size &&
                json_nonnegative_int(json, "pending", pending) &&
                json_nonnegative_int(json, "transferred", transferred) &&
                json_nonnegative_int(json, "failed", failed);
    if (valid) {
        (void)snprintf(operation_id, operation_id_size, "%s", id->valuestring);
        (void)snprintf(phase, phase_size, "%s", state->valuestring);
        (void)snprintf(error, error_size, "%s",
                       cJSON_IsString(message) ? message->valuestring : "");
    }
    cJSON_Delete(json);
    return valid;
}

static void attempt_failed(uint32_t generation, const char *message)
{
    xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
    if (pj_companion_sync_state_attempt_failed(
            &g_sync_state, generation, PJ_COMPANION_SYNC_TRANSPORT_LAN,
            message)) {
        (void)persist_state_locked();
        g_sync_update_pending = 1;
    }
    xSemaphoreGive(g_sync_mutex);
    ESP_LOGW(TAG, "%s", message);
}

static void publish_pending_error(const char *message)
{
    xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
    if (pj_companion_sync_state_pending(&g_sync_state) &&
        g_sync_state.active_generation == 0U) {
        g_sync_state.phase = PJ_COMPANION_SYNC_FAILED;
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
    int pending, int transferred, int failed, const char *error,
    const char *device_id, pj_companion_sync_phase_t *resulting_phase)
{
    xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
    pj_companion_sync_apply_result_t applied = pj_companion_sync_state_progress(
        &g_sync_state, generation, operation_id,
        PJ_COMPANION_SYNC_TRANSPORT_LAN, phase, pending, transferred, failed,
        error, device_id);
    if (applied == PJ_COMPANION_SYNC_APPLY_CHANGED) {
        if (strcmp(phase, "succeeded") == 0 || strcmp(phase, "failed") == 0) {
            (void)persist_state_locked();
        }
        g_sync_update_pending = 1;
    }
    *resulting_phase = g_sync_state.phase;
    xSemaphoreGive(g_sync_mutex);
    return applied;
}

static int run_one_lan_sync(void)
{
    pj_board_status_t status = pj_board_status();
    if (status.wifi != PJ_BOARD_SERVICE_READY || status.ip_addr[0] == '\0' ||
        strcmp(status.ip_addr, "0.0.0.0") == 0) {
        publish_pending_error(
            "Wi-Fi is not connected; USB sync remains pending");
        return 0;
    }
    char base_url[96];
    if (!discover_companion(status.device_id, base_url, sizeof(base_url))) {
        publish_pending_error(
            "No paired LAN companion; USB sync remains pending");
        return 0;
    }

    uint32_t generation;
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
        claim = PJ_COMPANION_SYNC_CLAIM_STALE;
    } else if (claim == PJ_COMPANION_SYNC_CLAIM_STARTED ||
               claim == PJ_COMPANION_SYNC_CLAIM_ATTACHED) {
        g_sync_update_pending = 1;
    }
    xSemaphoreGive(g_sync_mutex);
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

    cJSON *request_json = cJSON_CreateObject();
    if (request_json == NULL ||
        cJSON_AddStringToObject(request_json, "device_id", status.device_id) == NULL ||
        cJSON_AddNumberToObject(request_json, "generation", generation) == NULL ||
        cJSON_AddStringToObject(request_json, "request_id", operation_id) == NULL) {
        cJSON_Delete(request_json);
        attempt_failed(generation, "Unable to allocate companion request");
        return 0;
    }
    char *request_body = cJSON_PrintUnformatted(request_json);
    cJSON_Delete(request_json);
    if (request_body == NULL) {
        attempt_failed(generation, "Unable to encode companion request");
        return 0;
    }

    char url[192];
    (void)snprintf(url, sizeof(url), "%s/v1/sync", base_url);
    companion_http_response_t response;
    int http_status = companion_request(url, status.token, HTTP_METHOD_POST,
                                        request_body, &response);
    cJSON_free(request_body);
    if (http_status != 200 && http_status != 202) {
        attempt_failed(generation, http_status == 401 ?
                       "Companion rejected the paired token" :
                       "Companion did not accept the sync request");
        return -1;
    }

    uint32_t response_generation = 0U;
    char response_operation[PJ_COMPANION_SYNC_OPERATION_ID_BYTES];
    char phase[24];
    char error[PJ_COMPANION_SYNC_ERROR_BYTES];
    int pending = 0;
    int transferred = 0;
    int failed = 0;
    if (!parse_progress(response.body, &response_generation,
                        response_operation, sizeof(response_operation), phase,
                        sizeof(phase), &pending, &transferred, &failed, error,
                        sizeof(error)) || response_generation != generation ||
        strcmp(response_operation, operation_id) != 0) {
        attempt_failed(generation, "Companion returned an invalid start response");
        return -1;
    }
    pj_companion_sync_phase_t current;
    if (apply_lan_progress(generation, operation_id, phase, pending,
                           transferred, failed, error, status.device_id,
                           &current) == PJ_COMPANION_SYNC_APPLY_REJECTED) {
        attempt_failed(generation, "Companion start did not match the claimed request");
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
        (void)snprintf(url, sizeof(url), "%s/v1/sync/%s", base_url,
                       operation_id);
        http_status = companion_request(url, status.token, HTTP_METHOD_GET,
                                        NULL, &response);
        if (http_status < 0) {
            request_failures++;
            if (request_failures < 3U) {
                continue;
            }
            attempt_failed(generation, "Companion progress connection was lost");
            return 0;
        }
        request_failures = 0U;
        if (http_status != 200 ||
            !parse_progress(response.body, &response_generation,
                            response_operation, sizeof(response_operation),
                            phase, sizeof(phase), &pending, &transferred,
                            &failed, error, sizeof(error)) ||
            response_generation != generation ||
            strcmp(response_operation, operation_id) != 0) {
            attempt_failed(generation, http_status == 401 ?
                           "Companion rejected the paired token" :
                           "Companion returned invalid progress");
            return -1;
        }
        if (apply_lan_progress(generation, operation_id, phase, pending,
                               transferred, failed, error, status.device_id,
                               &current) == PJ_COMPANION_SYNC_APPLY_REJECTED) {
            attempt_failed(generation,
                           "Companion progress did not match the claimed request");
            return -1;
        }
        if (current == PJ_COMPANION_SYNC_SUCCEEDED ||
            current == PJ_COMPANION_SYNC_FAILED ||
            current == PJ_COMPANION_SYNC_PENDING) {
            ESP_LOGI(TAG, "Companion sync %s generation=%" PRIu32
                     " transferred=%d failed=%d",
                     pj_companion_sync_phase_name(current), generation,
                     transferred, failed);
            return current == PJ_COMPANION_SYNC_PENDING;
        }
    }
    attempt_failed(generation, "Companion sync exceeded the two-hour timeout");
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
    xSemaphoreTake(g_sync_mutex, portMAX_DELAY);
    if (!initialize_state_locked()) {
        xSemaphoreGive(g_sync_mutex);
        return 0;
    }
    pj_companion_sync_state_t before = g_sync_state;
    if (!pj_companion_sync_state_request(&g_sync_state, status.device_id) ||
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
        result = PJ_COMPANION_SYNC_CLAIM_STALE;
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
    int pending, int transferred, int failed, const char *error,
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
    pj_companion_sync_state_t before = g_sync_state;
    pj_companion_sync_apply_result_t result = pj_companion_sync_state_progress(
        &g_sync_state, generation, operation_id,
        PJ_COMPANION_SYNC_TRANSPORT_USB, phase, pending, transferred, failed,
        error, status.device_id);
    int terminal = strcmp(phase == NULL ? "" : phase, "succeeded") == 0 ||
                   strcmp(phase == NULL ? "" : phase, "failed") == 0;
    if (result == PJ_COMPANION_SYNC_APPLY_CHANGED && terminal &&
        !persist_state_locked()) {
        g_sync_state = before;
        result = PJ_COMPANION_SYNC_APPLY_REJECTED;
    }
    if (result == PJ_COMPANION_SYNC_APPLY_CHANGED) {
        g_sync_update_pending = 1;
    }
    if (snapshot != NULL) {
        *snapshot = g_sync_state;
    }
    xSemaphoreGive(g_sync_mutex);
    return (int)result;
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
    int pending, int transferred, int failed, const char *error,
    pj_companion_sync_state_t *snapshot)
{
    (void)generation;
    (void)operation_id;
    (void)phase;
    (void)pending;
    (void)transferred;
    (void)failed;
    (void)error;
    (void)snapshot;
    return -1;
}

int pj_board_consume_companion_sync_update(pj_ui_context_t *ui)
{
    (void)ui;
    return 0;
}

#endif
