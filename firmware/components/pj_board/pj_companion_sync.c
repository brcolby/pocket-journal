#include "pj_companion_sync.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int bounded_count(int value)
{
    return value < 0 ? 0 : value;
}

static void copy_text(char *target, size_t target_size, const char *source)
{
    if (target == NULL || target_size == 0U) {
        return;
    }
    (void)snprintf(target, target_size, "%s", source == NULL ? "" : source);
}

static int text_terminated(const char *text, size_t size)
{
    return text != NULL && memchr(text, '\0', size) != NULL;
}

static int operation_id_valid(const char *operation_id)
{
    if (operation_id == NULL || operation_id[0] == '\0') {
        return 0;
    }
    size_t size = strlen(operation_id);
    if (size >= PJ_COMPANION_SYNC_OPERATION_ID_BYTES) {
        return 0;
    }
    for (size_t i = 0; i < size; i++) {
        char ch = operation_id[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')) {
            return 0;
        }
    }
    return 1;
}

static int format_operation_id(char *operation_id, size_t operation_id_size,
                               const char *device_id, uint32_t generation)
{
    if (operation_id == NULL || operation_id_size == 0U ||
        device_id == NULL || device_id[0] == '\0' || generation == 0U) {
        return 0;
    }
    int written = snprintf(operation_id, operation_id_size, "%s-%08" PRIx32,
                           device_id, generation);
    return written > 0 && (size_t)written < operation_id_size &&
           operation_id_valid(operation_id);
}

static uint32_t record_checksum(const pj_companion_sync_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    size_t size = offsetof(pj_companion_sync_record_t, checksum);
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 16777619U;
    }
    return hash;
}

void pj_companion_sync_state_init(pj_companion_sync_state_t *state)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->phase = PJ_COMPANION_SYNC_IDLE;
    state->transport = PJ_COMPANION_SYNC_TRANSPORT_NONE;
}

int pj_companion_sync_state_pending(const pj_companion_sync_state_t *state)
{
    return state != NULL &&
           state->requested_generation > state->acknowledged_generation;
}

int pj_companion_sync_state_active(const pj_companion_sync_state_t *state)
{
    return state != NULL && state->active_generation != 0U &&
           (state->phase == PJ_COMPANION_SYNC_DISCOVERING ||
            state->phase == PJ_COMPANION_SYNC_REQUESTING ||
            state->phase == PJ_COMPANION_SYNC_RUNNING);
}

uint32_t pj_companion_sync_state_claim_generation(
    const pj_companion_sync_state_t *state)
{
    if (!pj_companion_sync_state_pending(state)) {
        return 0U;
    }
    return state->active_generation != 0U ? state->active_generation :
           state->requested_generation;
}

int pj_companion_sync_state_request(pj_companion_sync_state_t *state,
                                    const char *device_id)
{
    if (state == NULL || state->requested_generation == UINT32_MAX) {
        return 0;
    }
    uint32_t generation = state->requested_generation + 1U;
    if (state->active_generation == 0U &&
        !format_operation_id(state->operation_id,
                             sizeof(state->operation_id), device_id,
                             generation)) {
        return 0;
    }
    state->requested_generation = generation;
    if (state->active_generation == 0U) {
        state->phase = PJ_COMPANION_SYNC_PENDING;
        state->transport = PJ_COMPANION_SYNC_TRANSPORT_NONE;
        state->pending = 0;
        state->transferred = 0;
        state->failed = 0;
        state->online = 0;
        state->error[0] = '\0';
    }
    return 1;
}

pj_companion_sync_claim_result_t pj_companion_sync_state_claim(
    pj_companion_sync_state_t *state, uint32_t generation,
    const char *operation_id, pj_companion_sync_transport_t transport)
{
    if (state == NULL || !pj_companion_sync_state_pending(state) ||
        generation == 0U || operation_id == NULL ||
        !operation_id_valid(operation_id) ||
        (transport != PJ_COMPANION_SYNC_TRANSPORT_LAN &&
         transport != PJ_COMPANION_SYNC_TRANSPORT_USB)) {
        return PJ_COMPANION_SYNC_CLAIM_STALE;
    }
    uint32_t expected = pj_companion_sync_state_claim_generation(state);
    if (generation != expected || strcmp(operation_id, state->operation_id) != 0) {
        return PJ_COMPANION_SYNC_CLAIM_STALE;
    }
    if (state->active_generation != 0U && pj_companion_sync_state_active(state)) {
        return state->transport == transport ?
               PJ_COMPANION_SYNC_CLAIM_ATTACHED : PJ_COMPANION_SYNC_CLAIM_BUSY;
    }
    state->active_generation = generation;
    state->transport = transport;
    state->phase = transport == PJ_COMPANION_SYNC_TRANSPORT_LAN ?
                   PJ_COMPANION_SYNC_DISCOVERING :
                   PJ_COMPANION_SYNC_RUNNING;
    state->online = transport == PJ_COMPANION_SYNC_TRANSPORT_USB;
    state->error[0] = '\0';
    return PJ_COMPANION_SYNC_CLAIM_STARTED;
}

int pj_companion_sync_state_discovered(pj_companion_sync_state_t *state,
                                       uint32_t generation)
{
    if (state == NULL || state->active_generation != generation ||
        state->transport != PJ_COMPANION_SYNC_TRANSPORT_LAN ||
        state->phase != PJ_COMPANION_SYNC_DISCOVERING) {
        return 0;
    }
    state->phase = PJ_COMPANION_SYNC_REQUESTING;
    state->online = 1;
    return 1;
}

static pj_companion_sync_phase_t progress_phase(const char *phase)
{
    if (phase == NULL) {
        return PJ_COMPANION_SYNC_IDLE;
    }
    if (strcmp(phase, "queued") == 0 || strcmp(phase, "running") == 0) {
        return PJ_COMPANION_SYNC_RUNNING;
    }
    if (strcmp(phase, "succeeded") == 0) {
        return PJ_COMPANION_SYNC_SUCCEEDED;
    }
    if (strcmp(phase, "failed") == 0) {
        return PJ_COMPANION_SYNC_FAILED;
    }
    return PJ_COMPANION_SYNC_IDLE;
}

pj_companion_sync_apply_result_t pj_companion_sync_state_progress(
    pj_companion_sync_state_t *state, uint32_t generation,
    const char *operation_id, pj_companion_sync_transport_t transport,
    const char *phase, int pending, int transferred, int failed,
    const char *error, const char *device_id)
{
    pj_companion_sync_phase_t next = progress_phase(phase);
    if (state == NULL || operation_id == NULL || next == PJ_COMPANION_SYNC_IDLE) {
        return PJ_COMPANION_SYNC_APPLY_REJECTED;
    }
    if ((next == PJ_COMPANION_SYNC_SUCCEEDED ||
         next == PJ_COMPANION_SYNC_FAILED) &&
        generation == state->acknowledged_generation &&
        strcmp(operation_id, state->acknowledged_operation_id) == 0) {
        return PJ_COMPANION_SYNC_APPLY_REPLAY;
    }
    if (generation == 0U || state->active_generation != generation ||
        state->transport != transport ||
        strcmp(operation_id, state->operation_id) != 0) {
        return PJ_COMPANION_SYNC_APPLY_REJECTED;
    }
    state->pending = bounded_count(pending);
    state->transferred = bounded_count(transferred);
    state->failed = bounded_count(failed);
    state->online = 1;
    if (next == PJ_COMPANION_SYNC_RUNNING) {
        state->phase = next;
        state->error[0] = '\0';
        return PJ_COMPANION_SYNC_APPLY_CHANGED;
    }

    state->acknowledged_generation = generation;
    copy_text(state->acknowledged_operation_id,
              sizeof(state->acknowledged_operation_id), operation_id);
    state->active_generation = 0U;
    state->transport = PJ_COMPANION_SYNC_TRANSPORT_NONE;
    if (state->requested_generation > state->acknowledged_generation) {
        if (!format_operation_id(state->operation_id,
                                 sizeof(state->operation_id), device_id,
                                 state->requested_generation)) {
            state->phase = PJ_COMPANION_SYNC_FAILED;
            state->online = 0;
            copy_text(state->error, sizeof(state->error),
                      "Unable to prepare the queued sync request");
            return PJ_COMPANION_SYNC_APPLY_CHANGED;
        }
        state->phase = PJ_COMPANION_SYNC_PENDING;
        state->pending = 0;
        state->transferred = 0;
        state->failed = 0;
        state->online = 0;
        state->error[0] = '\0';
    } else {
        state->phase = next;
        copy_text(state->error, sizeof(state->error),
                  next == PJ_COMPANION_SYNC_FAILED ? error : "");
    }
    return PJ_COMPANION_SYNC_APPLY_CHANGED;
}

int pj_companion_sync_state_attempt_failed(
    pj_companion_sync_state_t *state, uint32_t generation,
    pj_companion_sync_transport_t transport, const char *error)
{
    if (state == NULL || generation == 0U ||
        state->active_generation != generation ||
        state->transport != transport) {
        return 0;
    }
    state->active_generation = 0U;
    state->transport = PJ_COMPANION_SYNC_TRANSPORT_NONE;
    state->phase = PJ_COMPANION_SYNC_FAILED;
    state->online = 0;
    copy_text(state->error, sizeof(state->error), error);
    return 1;
}

void pj_companion_sync_record_from_state(
    const pj_companion_sync_state_t *state,
    pj_companion_sync_record_t *record)
{
    if (state == NULL || record == NULL) {
        return;
    }
    memset(record, 0, sizeof(*record));
    record->magic = PJ_COMPANION_SYNC_RECORD_MAGIC;
    record->version = PJ_COMPANION_SYNC_RECORD_VERSION;
    record->requested_generation = state->requested_generation;
    record->acknowledged_generation = state->acknowledged_generation;
    record->active_generation = state->active_generation;
    record->phase = (uint32_t)state->phase;
    record->transport = (uint32_t)state->transport;
    record->pending = (uint32_t)bounded_count(state->pending);
    record->transferred = (uint32_t)bounded_count(state->transferred);
    record->failed = (uint32_t)bounded_count(state->failed);
    copy_text(record->operation_id, sizeof(record->operation_id),
              state->operation_id);
    copy_text(record->acknowledged_operation_id,
              sizeof(record->acknowledged_operation_id),
              state->acknowledged_operation_id);
    copy_text(record->error, sizeof(record->error), state->error);
    record->checksum = record_checksum(record);
}

int pj_companion_sync_state_from_record(
    pj_companion_sync_state_t *state,
    const pj_companion_sync_record_t *record)
{
    if (state == NULL || record == NULL ||
        record->magic != PJ_COMPANION_SYNC_RECORD_MAGIC ||
        record->version != PJ_COMPANION_SYNC_RECORD_VERSION ||
        record->checksum != record_checksum(record) ||
        record->phase > (uint32_t)PJ_COMPANION_SYNC_FAILED ||
        record->transport > (uint32_t)PJ_COMPANION_SYNC_TRANSPORT_USB ||
        record->requested_generation < record->acknowledged_generation ||
        (record->active_generation != 0U &&
         (record->active_generation <= record->acknowledged_generation ||
          record->active_generation > record->requested_generation)) ||
        record->pending > (uint32_t)INT_MAX ||
        record->transferred > (uint32_t)INT_MAX ||
        record->failed > (uint32_t)INT_MAX ||
        !text_terminated(record->operation_id, sizeof(record->operation_id)) ||
        !text_terminated(record->acknowledged_operation_id,
                         sizeof(record->acknowledged_operation_id)) ||
        !text_terminated(record->error, sizeof(record->error))) {
        return 0;
    }
    int request_pending =
        record->requested_generation > record->acknowledged_generation;
    if (request_pending && !operation_id_valid(record->operation_id)) {
        return 0;
    }
    if (!request_pending &&
        (record->active_generation != 0U ||
         record->transport != (uint32_t)PJ_COMPANION_SYNC_TRANSPORT_NONE ||
         (record->phase != (uint32_t)PJ_COMPANION_SYNC_IDLE &&
          record->phase != (uint32_t)PJ_COMPANION_SYNC_SUCCEEDED &&
          record->phase != (uint32_t)PJ_COMPANION_SYNC_FAILED))) {
        return 0;
    }
    pj_companion_sync_state_init(state);
    state->phase = (pj_companion_sync_phase_t)record->phase;
    state->transport = (pj_companion_sync_transport_t)record->transport;
    state->pending = (int)record->pending;
    state->transferred = (int)record->transferred;
    state->failed = (int)record->failed;
    state->requested_generation = record->requested_generation;
    state->acknowledged_generation = record->acknowledged_generation;
    state->active_generation = record->active_generation;
    copy_text(state->operation_id, sizeof(state->operation_id),
              record->operation_id);
    copy_text(state->acknowledged_operation_id,
              sizeof(state->acknowledged_operation_id),
              record->acknowledged_operation_id);
    copy_text(state->error, sizeof(state->error), record->error);
    state->online = 0;
    if (request_pending) {
        state->phase = record->phase == (uint32_t)PJ_COMPANION_SYNC_FAILED ?
                       PJ_COMPANION_SYNC_FAILED : PJ_COMPANION_SYNC_PENDING;
        state->transport = PJ_COMPANION_SYNC_TRANSPORT_NONE;
    }
    return 1;
}

int pj_companion_sync_state_from_record_bytes(
    pj_companion_sync_state_t *state, const void *record, size_t record_size)
{
    if (record == NULL || record_size != sizeof(pj_companion_sync_record_t)) {
        return 0;
    }
    return pj_companion_sync_state_from_record(
        state, (const pj_companion_sync_record_t *)record);
}

const char *pj_companion_sync_phase_name(pj_companion_sync_phase_t phase)
{
    switch (phase) {
    case PJ_COMPANION_SYNC_IDLE: return "idle";
    case PJ_COMPANION_SYNC_PENDING: return "pending";
    case PJ_COMPANION_SYNC_DISCOVERING: return "discovering";
    case PJ_COMPANION_SYNC_REQUESTING: return "requesting";
    case PJ_COMPANION_SYNC_RUNNING: return "running";
    case PJ_COMPANION_SYNC_SUCCEEDED: return "succeeded";
    case PJ_COMPANION_SYNC_FAILED: return "failed";
    default: return "unknown";
    }
}

const char *pj_companion_sync_transport_name(
    pj_companion_sync_transport_t transport)
{
    switch (transport) {
    case PJ_COMPANION_SYNC_TRANSPORT_NONE: return "none";
    case PJ_COMPANION_SYNC_TRANSPORT_LAN: return "lan";
    case PJ_COMPANION_SYNC_TRANSPORT_USB: return "usb";
    default: return "unknown";
    }
}
