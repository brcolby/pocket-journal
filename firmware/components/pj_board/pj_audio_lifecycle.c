#include "pj_audio_lifecycle.h"

#include <string.h>

void pj_audio_lifecycle_init(pj_audio_lifecycle_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

int pj_audio_lifecycle_active(const pj_audio_lifecycle_t *state)
{
    return state != NULL &&
           (state->record_worker || state->process_worker ||
            state->playback_worker);
}

int pj_audio_lifecycle_begin_record(pj_audio_lifecycle_t *state)
{
    if (state == NULL || state->record_worker || state->playback_worker) {
        return 0;
    }
    state->record_worker = 1;
    state->record_stop_requested = 0;
    return 1;
}

pj_audio_stop_result_t pj_audio_lifecycle_request_record_stop(
    pj_audio_lifecycle_t *state)
{
    if (state == NULL || !state->record_worker) {
        return PJ_AUDIO_STOP_INACTIVE;
    }
    if (state->record_stop_requested) {
        return PJ_AUDIO_STOP_ALREADY_REQUESTED;
    }
    state->record_stop_requested = 1;
    return PJ_AUDIO_STOP_SIGNAL_WORKER;
}

int pj_audio_lifecycle_begin_processing(pj_audio_lifecycle_t *state)
{
    if (state == NULL || state->process_worker) {
        return 0;
    }
    state->process_worker = 1;
    return 1;
}

void pj_audio_lifecycle_finish_record(pj_audio_lifecycle_t *state)
{
    if (state != NULL) {
        state->record_worker = 0;
        state->record_stop_requested = 0;
    }
}

void pj_audio_lifecycle_cancel_processing(pj_audio_lifecycle_t *state)
{
    if (state != NULL) {
        state->process_worker = 0;
    }
}

void pj_audio_lifecycle_finish_processing(pj_audio_lifecycle_t *state)
{
    pj_audio_lifecycle_cancel_processing(state);
}

int pj_audio_lifecycle_begin_playback(pj_audio_lifecycle_t *state)
{
    if (state == NULL || state->record_worker || state->playback_worker) {
        return 0;
    }
    state->playback_worker = 1;
    state->playback_stop_requested = 0;
    return 1;
}

pj_audio_stop_result_t pj_audio_lifecycle_request_playback_stop(
    pj_audio_lifecycle_t *state)
{
    if (state == NULL || !state->playback_worker) {
        return PJ_AUDIO_STOP_INACTIVE;
    }
    if (state->playback_stop_requested) {
        return PJ_AUDIO_STOP_ALREADY_REQUESTED;
    }
    state->playback_stop_requested = 1;
    return PJ_AUDIO_STOP_SIGNAL_WORKER;
}

void pj_audio_lifecycle_finish_playback(pj_audio_lifecycle_t *state)
{
    if (state != NULL) {
        state->playback_worker = 0;
        state->playback_stop_requested = 0;
    }
}
