#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_COMPANION_SYNC_OPERATION_ID_BYTES 65U
#define PJ_COMPANION_SYNC_ERROR_BYTES 128U
#define PJ_COMPANION_SYNC_RECORD_MAGIC 0x504a5359U
#define PJ_COMPANION_SYNC_RECORD_VERSION 2U

typedef enum {
    PJ_COMPANION_SYNC_IDLE = 0,
    PJ_COMPANION_SYNC_PENDING,
    PJ_COMPANION_SYNC_DISCOVERING,
    PJ_COMPANION_SYNC_REQUESTING,
    PJ_COMPANION_SYNC_RUNNING,
    PJ_COMPANION_SYNC_SUCCEEDED,
    PJ_COMPANION_SYNC_FAILED,
    PJ_COMPANION_SYNC_OFFLINE,
    PJ_COMPANION_SYNC_AUTH_FAILED,
    PJ_COMPANION_SYNC_PROTOCOL_FAILED,
} pj_companion_sync_phase_t;

typedef enum {
    PJ_COMPANION_SYNC_TRANSPORT_NONE = 0,
    PJ_COMPANION_SYNC_TRANSPORT_LAN,
    PJ_COMPANION_SYNC_TRANSPORT_USB,
} pj_companion_sync_transport_t;

typedef enum {
    PJ_COMPANION_SYNC_CLAIM_STORE_FAILED = -1,
    PJ_COMPANION_SYNC_CLAIM_STALE = 0,
    PJ_COMPANION_SYNC_CLAIM_STARTED,
    PJ_COMPANION_SYNC_CLAIM_ATTACHED,
    PJ_COMPANION_SYNC_CLAIM_BUSY,
} pj_companion_sync_claim_result_t;

typedef enum {
    PJ_COMPANION_SYNC_APPLY_STORE_FAILED = -1,
    PJ_COMPANION_SYNC_APPLY_REJECTED = 0,
    PJ_COMPANION_SYNC_APPLY_CHANGED,
    PJ_COMPANION_SYNC_APPLY_REPLAY,
} pj_companion_sync_apply_result_t;

typedef int (*pj_companion_sync_persist_fn)(void *context);

typedef struct {
    pj_companion_sync_phase_t phase;
    pj_companion_sync_transport_t transport;
    int total;
    int pending;
    int transferred;
    int failed;
    int online;
    uint32_t requested_generation;
    uint32_t acknowledged_generation;
    uint32_t active_generation;
    uint64_t requested_ms;
    uint64_t active_requested_ms;
    uint64_t acknowledged_requested_ms;
    char operation_id[PJ_COMPANION_SYNC_OPERATION_ID_BYTES];
    char acknowledged_operation_id[PJ_COMPANION_SYNC_OPERATION_ID_BYTES];
    char error[PJ_COMPANION_SYNC_ERROR_BYTES];
    pj_companion_sync_phase_t acknowledged_phase;
    int acknowledged_total;
    int acknowledged_pending;
    int acknowledged_transferred;
    int acknowledged_failed;
    char acknowledged_error[PJ_COMPANION_SYNC_ERROR_BYTES];
} pj_companion_sync_state_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t requested_generation;
    uint32_t acknowledged_generation;
    uint32_t active_generation;
    uint32_t phase;
    uint32_t transport;
    uint32_t total;
    uint32_t pending;
    uint32_t transferred;
    uint32_t failed;
    uint64_t requested_ms;
    uint64_t active_requested_ms;
    uint64_t acknowledged_requested_ms;
    char operation_id[PJ_COMPANION_SYNC_OPERATION_ID_BYTES];
    char acknowledged_operation_id[PJ_COMPANION_SYNC_OPERATION_ID_BYTES];
    char error[PJ_COMPANION_SYNC_ERROR_BYTES];
    uint32_t acknowledged_phase;
    uint32_t acknowledged_total;
    uint32_t acknowledged_pending;
    uint32_t acknowledged_transferred;
    uint32_t acknowledged_failed;
    char acknowledged_error[PJ_COMPANION_SYNC_ERROR_BYTES];
    uint32_t checksum;
} pj_companion_sync_record_t;

void pj_companion_sync_state_init(pj_companion_sync_state_t *state);
int pj_companion_sync_state_request(pj_companion_sync_state_t *state,
                                    const char *device_id,
                                    uint64_t requested_ms);
pj_companion_sync_claim_result_t pj_companion_sync_state_claim(
    pj_companion_sync_state_t *state, uint32_t generation,
    const char *operation_id, pj_companion_sync_transport_t transport);
int pj_companion_sync_state_discovered(pj_companion_sync_state_t *state,
                                       uint32_t generation);
pj_companion_sync_apply_result_t pj_companion_sync_state_progress(
    pj_companion_sync_state_t *state, uint32_t generation,
    const char *operation_id, pj_companion_sync_transport_t transport,
    const char *phase, int total, int pending, int transferred, int failed,
    const char *error, const char *device_id);
pj_companion_sync_apply_result_t pj_companion_sync_state_progress_transactional(
    pj_companion_sync_state_t *state, uint32_t generation,
    const char *operation_id, pj_companion_sync_transport_t transport,
    const char *phase, int total, int pending, int transferred, int failed,
    const char *error, const char *device_id,
    pj_companion_sync_persist_fn persist, void *persist_context);
int pj_companion_sync_state_attempt_failed(
    pj_companion_sync_state_t *state, uint32_t generation,
    pj_companion_sync_transport_t transport,
    pj_companion_sync_phase_t failure_phase, const char *error);
int pj_companion_sync_error_valid(const char *error);
int pj_companion_sync_scope_allowed(const char *method, const char *uri);
int pj_companion_sync_state_pending(const pj_companion_sync_state_t *state);
int pj_companion_sync_state_active(const pj_companion_sync_state_t *state);
uint32_t pj_companion_sync_state_claim_generation(
    const pj_companion_sync_state_t *state);
const char *pj_companion_sync_phase_name(pj_companion_sync_phase_t phase);
const char *pj_companion_sync_transport_name(
    pj_companion_sync_transport_t transport);

void pj_companion_sync_record_from_state(
    const pj_companion_sync_state_t *state,
    pj_companion_sync_record_t *record);
int pj_companion_sync_state_from_record(
    pj_companion_sync_state_t *state,
    const pj_companion_sync_record_t *record);
int pj_companion_sync_state_from_record_bytes(
    pj_companion_sync_state_t *state, const void *record, size_t record_size);
int pj_companion_sync_record_equal(const pj_companion_sync_record_t *left,
                                   const pj_companion_sync_record_t *right);

#ifdef __cplusplus
}
#endif
