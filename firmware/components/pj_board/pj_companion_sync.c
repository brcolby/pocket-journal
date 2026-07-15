#include "pj_companion_sync.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int bounded_count(int value)
{
    return value < 0 ? 0 : value;
}

static int counts_valid(int total, int pending, int transferred, int failed)
{
    return total >= 0 && pending >= 0 && transferred >= 0 && failed >= 0 &&
           transferred <= total && failed <= total - transferred &&
           pending == total - transferred - failed;
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

int pj_companion_sync_error_valid(const char *error)
{
    if (error == NULL) {
        return 1;
    }
    size_t size = strlen(error);
    if (size >= PJ_COMPANION_SYNC_ERROR_BYTES) {
        return 0;
    }
    for (size_t i = 0; i < size; i++) {
        unsigned char ch = (unsigned char)error[i];
        if (ch < 0x20U || ch > 0x7eU) {
            return 0;
        }
    }
    return 1;
}

int pj_companion_sync_scope_allowed(const char *method, const char *uri)
{
    if (method == NULL || uri == NULL || strchr(uri, '?') != NULL ||
        strchr(uri, '#') != NULL || strstr(uri, "..") != NULL ||
        strchr(uri, '\\') != NULL) {
        return 0;
    }
    if (strcmp(method, "GET") == 0 && strcmp(uri, "/v1/audio") == 0) {
        return 1;
    }
    const char *suffix = NULL;
    if (strcmp(method, "GET") == 0 &&
        strncmp(uri, "/v1/audio/", 10U) == 0) {
        suffix = uri + 10U;
    } else if (strcmp(method, "PUT") == 0 &&
               strncmp(uri, "/v1/transcripts/", 16U) == 0) {
        suffix = uri + 16U;
    }
    return suffix != NULL && suffix[0] != '\0' && strchr(suffix, '/') == NULL;
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
    return state != NULL && state->active_generation != 0U;
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
                                    const char *device_id,
                                    uint64_t requested_ms)
{
    if (state == NULL || requested_ms > 9007199254740991ULL ||
        state->requested_generation == UINT32_MAX) {
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
    state->requested_ms = requested_ms;
    if (state->active_generation == 0U) {
        state->phase = PJ_COMPANION_SYNC_PENDING;
        state->transport = PJ_COMPANION_SYNC_TRANSPORT_NONE;
        state->total = 0;
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
    if (state->active_generation != 0U) {
        if (state->transport != PJ_COMPANION_SYNC_TRANSPORT_NONE &&
            state->transport != transport) {
            return PJ_COMPANION_SYNC_CLAIM_BUSY;
        }
        state->transport = transport;
        state->phase = transport == PJ_COMPANION_SYNC_TRANSPORT_LAN ?
                       PJ_COMPANION_SYNC_DISCOVERING :
                       PJ_COMPANION_SYNC_RUNNING;
        state->online = transport == PJ_COMPANION_SYNC_TRANSPORT_USB;
        state->error[0] = '\0';
        return PJ_COMPANION_SYNC_CLAIM_ATTACHED;
    }
    state->active_generation = generation;
    state->active_requested_ms = state->requested_ms;
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
    const char *phase, int total, int pending, int transferred, int failed,
    const char *error, const char *device_id)
{
    pj_companion_sync_phase_t next = progress_phase(phase);
    const char *safe_error = error == NULL ? "" : error;
    if (state == NULL || operation_id == NULL ||
        next == PJ_COMPANION_SYNC_IDLE ||
        !counts_valid(total, pending, transferred, failed) ||
        !pj_companion_sync_error_valid(safe_error) ||
        (next != PJ_COMPANION_SYNC_FAILED && safe_error[0] != '\0') ||
        (next == PJ_COMPANION_SYNC_SUCCEEDED &&
         (pending != 0 || failed != 0))) {
        return PJ_COMPANION_SYNC_APPLY_REJECTED;
    }
    if ((next == PJ_COMPANION_SYNC_SUCCEEDED ||
         next == PJ_COMPANION_SYNC_FAILED) &&
        generation == state->acknowledged_generation &&
        strcmp(operation_id, state->acknowledged_operation_id) == 0) {
        return next == state->acknowledged_phase &&
               total == state->acknowledged_total &&
               pending == state->acknowledged_pending &&
               transferred == state->acknowledged_transferred &&
               failed == state->acknowledged_failed &&
               strcmp(safe_error, state->acknowledged_error) == 0 ?
               PJ_COMPANION_SYNC_APPLY_REPLAY :
               PJ_COMPANION_SYNC_APPLY_REJECTED;
    }
    if (generation == 0U || state->active_generation != generation ||
        state->transport != transport ||
        strcmp(operation_id, state->operation_id) != 0) {
        return PJ_COMPANION_SYNC_APPLY_REJECTED;
    }
    state->total = total;
    state->pending = pending;
    state->transferred = transferred;
    state->failed = failed;
    state->online = 1;
    if (next == PJ_COMPANION_SYNC_RUNNING) {
        state->phase = next;
        state->error[0] = '\0';
        return PJ_COMPANION_SYNC_APPLY_CHANGED;
    }

    state->acknowledged_generation = generation;
    state->acknowledged_requested_ms = state->active_requested_ms;
    copy_text(state->acknowledged_operation_id,
              sizeof(state->acknowledged_operation_id), operation_id);
    state->acknowledged_phase = next;
    state->acknowledged_total = total;
    state->acknowledged_pending = pending;
    state->acknowledged_transferred = transferred;
    state->acknowledged_failed = failed;
    copy_text(state->acknowledged_error,
              sizeof(state->acknowledged_error), safe_error);
    state->active_generation = 0U;
    state->active_requested_ms = 0U;
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
        state->total = 0;
        state->pending = 0;
        state->transferred = 0;
        state->failed = 0;
        state->online = 0;
        state->error[0] = '\0';
    } else {
        state->phase = next;
        copy_text(state->error, sizeof(state->error),
                  next == PJ_COMPANION_SYNC_FAILED ? safe_error : "");
    }
    return PJ_COMPANION_SYNC_APPLY_CHANGED;
}

pj_companion_sync_apply_result_t pj_companion_sync_state_progress_transactional(
    pj_companion_sync_state_t *state, uint32_t generation,
    const char *operation_id, pj_companion_sync_transport_t transport,
    const char *phase, int total, int pending, int transferred, int failed,
    const char *error, const char *device_id,
    pj_companion_sync_persist_fn persist, void *persist_context)
{
    if (state == NULL) {
        return PJ_COMPANION_SYNC_APPLY_REJECTED;
    }
    pj_companion_sync_state_t before = *state;
    pj_companion_sync_apply_result_t result = pj_companion_sync_state_progress(
        state, generation, operation_id, transport, phase, total, pending,
        transferred, failed, error, device_id);
    int terminal = strcmp(phase == NULL ? "" : phase, "succeeded") == 0 ||
                   strcmp(phase == NULL ? "" : phase, "failed") == 0;
    if (result == PJ_COMPANION_SYNC_APPLY_CHANGED && terminal &&
        (persist == NULL || !persist(persist_context))) {
        *state = before;
        return PJ_COMPANION_SYNC_APPLY_STORE_FAILED;
    }
    return result;
}

int pj_companion_sync_state_attempt_failed(
    pj_companion_sync_state_t *state, uint32_t generation,
    pj_companion_sync_transport_t transport,
    pj_companion_sync_phase_t failure_phase, const char *error)
{
    if (state == NULL || generation == 0U ||
        state->active_generation != generation ||
        state->transport != transport ||
        (failure_phase != PJ_COMPANION_SYNC_OFFLINE &&
         failure_phase != PJ_COMPANION_SYNC_AUTH_FAILED &&
         failure_phase != PJ_COMPANION_SYNC_PROTOCOL_FAILED) ||
        !pj_companion_sync_error_valid(error)) {
        return 0;
    }
    state->transport = PJ_COMPANION_SYNC_TRANSPORT_NONE;
    state->phase = failure_phase;
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
    record->total = (uint32_t)bounded_count(state->total);
    record->pending = (uint32_t)bounded_count(state->pending);
    record->transferred = (uint32_t)bounded_count(state->transferred);
    record->failed = (uint32_t)bounded_count(state->failed);
    record->requested_ms = state->requested_ms;
    record->active_requested_ms = state->active_requested_ms;
    record->acknowledged_requested_ms = state->acknowledged_requested_ms;
    copy_text(record->operation_id, sizeof(record->operation_id),
              state->operation_id);
    copy_text(record->acknowledged_operation_id,
              sizeof(record->acknowledged_operation_id),
              state->acknowledged_operation_id);
    copy_text(record->error, sizeof(record->error), state->error);
    record->acknowledged_phase = (uint32_t)state->acknowledged_phase;
    record->acknowledged_total =
        (uint32_t)bounded_count(state->acknowledged_total);
    record->acknowledged_pending =
        (uint32_t)bounded_count(state->acknowledged_pending);
    record->acknowledged_transferred =
        (uint32_t)bounded_count(state->acknowledged_transferred);
    record->acknowledged_failed =
        (uint32_t)bounded_count(state->acknowledged_failed);
    copy_text(record->acknowledged_error,
              sizeof(record->acknowledged_error),
              state->acknowledged_error);
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
        record->phase > (uint32_t)PJ_COMPANION_SYNC_PROTOCOL_FAILED ||
        record->transport > (uint32_t)PJ_COMPANION_SYNC_TRANSPORT_USB ||
        record->requested_generation < record->acknowledged_generation ||
        (record->active_generation != 0U &&
         (record->active_generation <= record->acknowledged_generation ||
          record->active_generation > record->requested_generation)) ||
        record->total > (uint32_t)INT_MAX ||
        record->pending > (uint32_t)INT_MAX ||
        record->transferred > (uint32_t)INT_MAX ||
        record->failed > (uint32_t)INT_MAX ||
        record->acknowledged_total > (uint32_t)INT_MAX ||
        record->acknowledged_pending > (uint32_t)INT_MAX ||
        record->acknowledged_transferred > (uint32_t)INT_MAX ||
        record->acknowledged_failed > (uint32_t)INT_MAX ||
        record->requested_ms > 9007199254740991ULL ||
        record->active_requested_ms > 9007199254740991ULL ||
        record->acknowledged_requested_ms > 9007199254740991ULL ||
        !text_terminated(record->operation_id, sizeof(record->operation_id)) ||
        !text_terminated(record->acknowledged_operation_id,
                         sizeof(record->acknowledged_operation_id)) ||
        !text_terminated(record->error, sizeof(record->error)) ||
        !text_terminated(record->acknowledged_error,
                         sizeof(record->acknowledged_error))) {
        return 0;
    }
    if (!counts_valid((int)record->total, (int)record->pending,
                      (int)record->transferred, (int)record->failed) ||
        !counts_valid((int)record->acknowledged_total,
                      (int)record->acknowledged_pending,
                      (int)record->acknowledged_transferred,
                      (int)record->acknowledged_failed) ||
        !pj_companion_sync_error_valid(record->error) ||
        !pj_companion_sync_error_valid(record->acknowledged_error)) {
        return 0;
    }
    int request_pending =
        record->requested_generation > record->acknowledged_generation;
    if (request_pending && !operation_id_valid(record->operation_id)) {
        return 0;
    }
    if ((record->acknowledged_generation == 0U &&
         (record->acknowledged_operation_id[0] != '\0' ||
          record->acknowledged_requested_ms != 0U ||
          record->acknowledged_phase != (uint32_t)PJ_COMPANION_SYNC_IDLE ||
          record->acknowledged_total != 0U ||
          record->acknowledged_pending != 0U ||
          record->acknowledged_transferred != 0U ||
          record->acknowledged_failed != 0U ||
          record->acknowledged_error[0] != '\0')) ||
        (record->acknowledged_generation != 0U &&
         (!operation_id_valid(record->acknowledged_operation_id) ||
          (record->acknowledged_phase !=
               (uint32_t)PJ_COMPANION_SYNC_SUCCEEDED &&
           record->acknowledged_phase !=
               (uint32_t)PJ_COMPANION_SYNC_FAILED)))) {
        return 0;
    }
    if ((record->acknowledged_phase ==
             (uint32_t)PJ_COMPANION_SYNC_SUCCEEDED &&
         (record->acknowledged_pending != 0U ||
          record->acknowledged_failed != 0U ||
          record->acknowledged_error[0] != '\0')) ||
        (record->error[0] != '\0' &&
         record->phase != (uint32_t)PJ_COMPANION_SYNC_FAILED &&
         record->phase != (uint32_t)PJ_COMPANION_SYNC_OFFLINE &&
         record->phase != (uint32_t)PJ_COMPANION_SYNC_AUTH_FAILED &&
         record->phase != (uint32_t)PJ_COMPANION_SYNC_PROTOCOL_FAILED)) {
        return 0;
    }
    if ((record->active_generation == 0U &&
         (record->active_requested_ms != 0U ||
          record->transport !=
              (uint32_t)PJ_COMPANION_SYNC_TRANSPORT_NONE)) ||
        (record->active_generation != 0U &&
         !operation_id_valid(record->operation_id))) {
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
    if (!request_pending && record->requested_generation == 0U &&
        (record->phase != (uint32_t)PJ_COMPANION_SYNC_IDLE ||
         record->requested_ms != 0U || record->operation_id[0] != '\0' ||
         record->total != 0U || record->pending != 0U ||
         record->transferred != 0U || record->failed != 0U ||
         record->error[0] != '\0')) {
        return 0;
    }
    if (!request_pending && record->requested_generation != 0U &&
        (record->phase != record->acknowledged_phase ||
         record->requested_ms != record->acknowledged_requested_ms ||
         strcmp(record->operation_id,
                record->acknowledged_operation_id) != 0 ||
         record->total != record->acknowledged_total ||
         record->pending != record->acknowledged_pending ||
         record->transferred != record->acknowledged_transferred ||
         record->failed != record->acknowledged_failed ||
         strcmp(record->error, record->acknowledged_error) != 0)) {
        return 0;
    }
    pj_companion_sync_state_init(state);
    state->phase = (pj_companion_sync_phase_t)record->phase;
    state->transport = (pj_companion_sync_transport_t)record->transport;
    state->total = (int)record->total;
    state->pending = (int)record->pending;
    state->transferred = (int)record->transferred;
    state->failed = (int)record->failed;
    state->requested_generation = record->requested_generation;
    state->acknowledged_generation = record->acknowledged_generation;
    state->active_generation = record->active_generation;
    state->requested_ms = record->requested_ms;
    state->active_requested_ms = record->active_requested_ms;
    state->acknowledged_requested_ms = record->acknowledged_requested_ms;
    copy_text(state->operation_id, sizeof(state->operation_id),
              record->operation_id);
    copy_text(state->acknowledged_operation_id,
              sizeof(state->acknowledged_operation_id),
              record->acknowledged_operation_id);
    copy_text(state->error, sizeof(state->error), record->error);
    state->acknowledged_phase =
        (pj_companion_sync_phase_t)record->acknowledged_phase;
    state->acknowledged_total = (int)record->acknowledged_total;
    state->acknowledged_pending = (int)record->acknowledged_pending;
    state->acknowledged_transferred =
        (int)record->acknowledged_transferred;
    state->acknowledged_failed = (int)record->acknowledged_failed;
    copy_text(state->acknowledged_error,
              sizeof(state->acknowledged_error),
              record->acknowledged_error);
    state->online = 0;
    if (request_pending) {
        if (record->phase != (uint32_t)PJ_COMPANION_SYNC_OFFLINE &&
            record->phase != (uint32_t)PJ_COMPANION_SYNC_AUTH_FAILED &&
            record->phase != (uint32_t)PJ_COMPANION_SYNC_PROTOCOL_FAILED) {
            state->phase = PJ_COMPANION_SYNC_PENDING;
            state->error[0] = '\0';
        }
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

int pj_companion_sync_record_equal(const pj_companion_sync_record_t *left,
                                   const pj_companion_sync_record_t *right)
{
    return left != NULL && right != NULL &&
           memcmp(left, right, sizeof(*left)) == 0;
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
    case PJ_COMPANION_SYNC_OFFLINE: return "offline";
    case PJ_COMPANION_SYNC_AUTH_FAILED: return "auth_failed";
    case PJ_COMPANION_SYNC_PROTOCOL_FAILED: return "protocol_failed";
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
