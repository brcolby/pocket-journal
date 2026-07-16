#include "pj_storage_coordinator.h"

#include <string.h>

static int wipe_active(const pj_storage_coordinator_t *coordinator)
{
    return coordinator->wipe.state == PJ_WIPE_STATE_QUEUED ||
           coordinator->wipe.state == PJ_WIPE_STATE_RUNNING;
}

static int request_id_valid(const char *request_id)
{
    return request_id != NULL && request_id[0] != '\0' &&
           strlen(request_id) < PJ_STORAGE_REQUEST_ID_BYTES;
}

static void remember_request(pj_storage_coordinator_t *coordinator,
                             const char *request_id)
{
    if (!request_id_valid(request_id)) {
        return;
    }
    size_t length = strlen(request_id);
    memcpy(coordinator->last_request_id, request_id, length + 1U);
    coordinator->last_request_status = coordinator->wipe;
}

static void refresh_remembered_status(pj_storage_coordinator_t *coordinator)
{
    if (coordinator->last_request_status.id == coordinator->wipe.id) {
        coordinator->last_request_status = coordinator->wipe;
    }
}

void pj_storage_coordinator_init(pj_storage_coordinator_t *coordinator)
{
    if (coordinator == NULL) {
        return;
    }
    memset(coordinator, 0, sizeof(*coordinator));
    coordinator->next_operation_id = 1U;
}

int pj_storage_shared_try_acquire(pj_storage_coordinator_t *coordinator)
{
    if (coordinator == NULL || coordinator->maintenance != PJ_STORAGE_MAINTENANCE_NONE) {
        return 0;
    }
    coordinator->shared_users++;
    return 1;
}

void pj_storage_shared_release(pj_storage_coordinator_t *coordinator)
{
    if (coordinator != NULL && coordinator->shared_users > 0U) {
        coordinator->shared_users--;
    }
}

int pj_storage_audio_publication_try_begin(
    pj_storage_coordinator_t *coordinator)
{
    if (coordinator == NULL ||
        coordinator->maintenance != PJ_STORAGE_MAINTENANCE_NONE ||
        coordinator->shared_users != 1U) {
        return 0;
    }
    coordinator->shared_users = 0U;
    coordinator->maintenance = PJ_STORAGE_MAINTENANCE_AUDIO_PUBLICATION;
    return 1;
}

void pj_storage_audio_publication_finish(
    pj_storage_coordinator_t *coordinator)
{
    if (coordinator != NULL &&
        coordinator->maintenance == PJ_STORAGE_MAINTENANCE_AUDIO_PUBLICATION) {
        coordinator->maintenance = PJ_STORAGE_MAINTENANCE_NONE;
    }
}

int pj_storage_idle(const pj_storage_coordinator_t *coordinator)
{
    return coordinator != NULL &&
           coordinator->maintenance == PJ_STORAGE_MAINTENANCE_NONE &&
           coordinator->shared_users == 0U;
}

pj_wipe_start_result_t pj_storage_wipe_request(pj_storage_coordinator_t *coordinator,
                                                int storage_ready, int audio_active,
                                                const char *request_id,
                                                pj_wipe_status_t *status)
{
    if (coordinator == NULL) {
        return PJ_WIPE_START_STORAGE_UNAVAILABLE;
    }
    if (request_id_valid(request_id) &&
        strcmp(request_id, coordinator->last_request_id) == 0 &&
        coordinator->last_request_status.id != 0U) {
        if (status != NULL) {
            *status = coordinator->last_request_status;
        }
        return PJ_WIPE_START_ATTACHED;
    }
    if (wipe_active(coordinator)) {
        remember_request(coordinator, request_id);
        if (status != NULL) {
            *status = coordinator->wipe;
        }
        return PJ_WIPE_START_ATTACHED;
    }
    if (!storage_ready) {
        return PJ_WIPE_START_STORAGE_UNAVAILABLE;
    }
    if (audio_active) {
        return PJ_WIPE_START_AUDIO_ACTIVE;
    }
    if (coordinator->maintenance != PJ_STORAGE_MAINTENANCE_NONE ||
        coordinator->shared_users != 0U) {
        return PJ_WIPE_START_STORAGE_BUSY;
    }

    memset(&coordinator->wipe, 0, sizeof(coordinator->wipe));
    coordinator->wipe.id = coordinator->next_operation_id++;
    if (coordinator->next_operation_id == 0U) {
        coordinator->next_operation_id = 1U;
    }
    coordinator->wipe.state = PJ_WIPE_STATE_QUEUED;
    coordinator->maintenance = PJ_STORAGE_MAINTENANCE_RECORDING_WIPE;
    remember_request(coordinator, request_id);
    if (status != NULL) {
        *status = coordinator->wipe;
    }
    return PJ_WIPE_START_STARTED;
}

void pj_storage_wipe_mark_running(pj_storage_coordinator_t *coordinator)
{
    if (coordinator != NULL && coordinator->maintenance == PJ_STORAGE_MAINTENANCE_RECORDING_WIPE &&
        coordinator->wipe.state == PJ_WIPE_STATE_QUEUED) {
        coordinator->wipe.state = PJ_WIPE_STATE_RUNNING;
        refresh_remembered_status(coordinator);
    }
}

void pj_storage_wipe_finish(pj_storage_coordinator_t *coordinator,
                            size_t audio_deleted, size_t transcripts_deleted,
                            size_t notes_deleted, pj_wipe_code_t code,
                            int retryable)
{
    if (coordinator == NULL || coordinator->maintenance != PJ_STORAGE_MAINTENANCE_RECORDING_WIPE) {
        return;
    }
    coordinator->wipe.audio_deleted = audio_deleted;
    coordinator->wipe.transcripts_deleted = transcripts_deleted;
    coordinator->wipe.notes_deleted = notes_deleted;
    coordinator->wipe.code = code;
    coordinator->wipe.retryable = retryable != 0;
    coordinator->wipe.state = code == PJ_WIPE_CODE_NONE ?
                              PJ_WIPE_STATE_SUCCEEDED : PJ_WIPE_STATE_FAILED;
    refresh_remembered_status(coordinator);
    coordinator->wipe_history[coordinator->wipe_history_next] = coordinator->wipe;
    coordinator->wipe_history_next =
        (coordinator->wipe_history_next + 1U) % PJ_STORAGE_WIPE_HISTORY_CAPACITY;
    if (coordinator->wipe_history_count < PJ_STORAGE_WIPE_HISTORY_CAPACITY) {
        coordinator->wipe_history_count++;
    }
    coordinator->maintenance = PJ_STORAGE_MAINTENANCE_NONE;
}

int pj_storage_recovery_try_begin(pj_storage_coordinator_t *coordinator)
{
    if (coordinator == NULL || coordinator->maintenance != PJ_STORAGE_MAINTENANCE_NONE ||
        coordinator->shared_users != 0U) {
        return 0;
    }
    coordinator->maintenance = PJ_STORAGE_MAINTENANCE_RECOVERY;
    return 1;
}

void pj_storage_recovery_finish(pj_storage_coordinator_t *coordinator)
{
    if (coordinator != NULL && coordinator->maintenance == PJ_STORAGE_MAINTENANCE_RECOVERY) {
        coordinator->maintenance = PJ_STORAGE_MAINTENANCE_NONE;
    }
}

int pj_storage_sleep_try_begin(pj_storage_coordinator_t *coordinator)
{
    if (!pj_storage_idle(coordinator)) {
        return 0;
    }
    coordinator->maintenance = PJ_STORAGE_MAINTENANCE_SLEEP;
    return 1;
}

void pj_storage_sleep_finish(pj_storage_coordinator_t *coordinator)
{
    if (coordinator != NULL && coordinator->maintenance == PJ_STORAGE_MAINTENANCE_SLEEP) {
        coordinator->maintenance = PJ_STORAGE_MAINTENANCE_NONE;
    }
}

int pj_storage_ota_try_begin(pj_storage_coordinator_t *coordinator)
{
    if (!pj_storage_idle(coordinator)) {
        return 0;
    }
    coordinator->maintenance = PJ_STORAGE_MAINTENANCE_OTA;
    return 1;
}

void pj_storage_ota_finish(pj_storage_coordinator_t *coordinator)
{
    if (coordinator != NULL &&
        coordinator->maintenance == PJ_STORAGE_MAINTENANCE_OTA) {
        coordinator->maintenance = PJ_STORAGE_MAINTENANCE_NONE;
    }
}

pj_wipe_status_t pj_storage_wipe_status(const pj_storage_coordinator_t *coordinator)
{
    pj_wipe_status_t status = {0};
    if (coordinator != NULL) {
        status = coordinator->wipe;
    }
    return status;
}

size_t pj_storage_wipe_history(const pj_storage_coordinator_t *coordinator,
                               pj_wipe_status_t *statuses, size_t capacity)
{
    if (coordinator == NULL || statuses == NULL || capacity == 0U) {
        return 0U;
    }
    size_t count = coordinator->wipe_history_count < capacity ?
                   coordinator->wipe_history_count : capacity;
    for (size_t i = 0; i < count; i++) {
        size_t index = (coordinator->wipe_history_next + PJ_STORAGE_WIPE_HISTORY_CAPACITY - 1U - i) %
                       PJ_STORAGE_WIPE_HISTORY_CAPACITY;
        statuses[i] = coordinator->wipe_history[index];
    }
    return count;
}

const char *pj_wipe_state_name(pj_wipe_state_t state)
{
    switch (state) {
    case PJ_WIPE_STATE_QUEUED: return "queued";
    case PJ_WIPE_STATE_RUNNING: return "running";
    case PJ_WIPE_STATE_SUCCEEDED: return "succeeded";
    case PJ_WIPE_STATE_FAILED: return "failed";
    case PJ_WIPE_STATE_IDLE:
    default: return "idle";
    }
}

const char *pj_wipe_code_name(pj_wipe_code_t code)
{
    switch (code) {
    case PJ_WIPE_CODE_INCOMPLETE: return "wipe_incomplete";
    case PJ_WIPE_CODE_TASK_START_FAILED: return "wipe_task_start_failed";
    case PJ_WIPE_CODE_SYNC_STATE_FAILED: return "wipe_sync_state_failed";
    case PJ_WIPE_CODE_NONE:
    default: return "none";
    }
}
