#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PJ_STORAGE_MAINTENANCE_NONE = 0,
    PJ_STORAGE_MAINTENANCE_RECORDING_WIPE,
    PJ_STORAGE_MAINTENANCE_RECOVERY,
    PJ_STORAGE_MAINTENANCE_SLEEP,
} pj_storage_maintenance_t;

typedef enum {
    PJ_WIPE_STATE_IDLE = 0,
    PJ_WIPE_STATE_QUEUED,
    PJ_WIPE_STATE_RUNNING,
    PJ_WIPE_STATE_SUCCEEDED,
    PJ_WIPE_STATE_FAILED,
} pj_wipe_state_t;

typedef enum {
    PJ_WIPE_CODE_NONE = 0,
    PJ_WIPE_CODE_INCOMPLETE,
    PJ_WIPE_CODE_TASK_START_FAILED,
} pj_wipe_code_t;

typedef enum {
    PJ_WIPE_START_STARTED = 0,
    PJ_WIPE_START_ATTACHED,
    PJ_WIPE_START_AUDIO_ACTIVE,
    PJ_WIPE_START_STORAGE_BUSY,
    PJ_WIPE_START_STORAGE_UNAVAILABLE,
    PJ_WIPE_START_TASK_FAILED,
} pj_wipe_start_result_t;

typedef struct {
    uint32_t id;
    pj_wipe_state_t state;
    size_t audio_deleted;
    size_t transcripts_deleted;
    size_t notes_deleted;
    pj_wipe_code_t code;
    int retryable;
} pj_wipe_status_t;

#define PJ_STORAGE_REQUEST_ID_BYTES 33U
#define PJ_STORAGE_WIPE_HISTORY_CAPACITY 4U

typedef struct {
    unsigned shared_users;
    pj_storage_maintenance_t maintenance;
    uint32_t next_operation_id;
    pj_wipe_status_t wipe;
    char last_request_id[PJ_STORAGE_REQUEST_ID_BYTES];
    pj_wipe_status_t last_request_status;
    pj_wipe_status_t wipe_history[PJ_STORAGE_WIPE_HISTORY_CAPACITY];
    size_t wipe_history_count;
    size_t wipe_history_next;
} pj_storage_coordinator_t;

void pj_storage_coordinator_init(pj_storage_coordinator_t *coordinator);
int pj_storage_shared_try_acquire(pj_storage_coordinator_t *coordinator);
void pj_storage_shared_release(pj_storage_coordinator_t *coordinator);
int pj_storage_idle(const pj_storage_coordinator_t *coordinator);
pj_wipe_start_result_t pj_storage_wipe_request(pj_storage_coordinator_t *coordinator,
                                                int storage_ready, int audio_active,
                                                const char *request_id,
                                                pj_wipe_status_t *status);
void pj_storage_wipe_mark_running(pj_storage_coordinator_t *coordinator);
void pj_storage_wipe_finish(pj_storage_coordinator_t *coordinator,
                            size_t audio_deleted, size_t transcripts_deleted,
                            size_t notes_deleted, pj_wipe_code_t code,
                            int retryable);
int pj_storage_recovery_try_begin(pj_storage_coordinator_t *coordinator);
void pj_storage_recovery_finish(pj_storage_coordinator_t *coordinator);
int pj_storage_sleep_try_begin(pj_storage_coordinator_t *coordinator);
void pj_storage_sleep_finish(pj_storage_coordinator_t *coordinator);
pj_wipe_status_t pj_storage_wipe_status(const pj_storage_coordinator_t *coordinator);
size_t pj_storage_wipe_history(const pj_storage_coordinator_t *coordinator,
                               pj_wipe_status_t *statuses, size_t capacity);
const char *pj_wipe_state_name(pj_wipe_state_t state);
const char *pj_wipe_code_name(pj_wipe_code_t code);

#ifdef __cplusplus
}
#endif
