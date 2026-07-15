#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned record_worker : 1;
    unsigned process_worker : 1;
    unsigned playback_worker : 1;
    unsigned record_stop_requested : 1;
    unsigned playback_stop_requested : 1;
} pj_audio_lifecycle_t;

typedef enum {
    PJ_AUDIO_STOP_INACTIVE = 0,
    PJ_AUDIO_STOP_ALREADY_REQUESTED,
    PJ_AUDIO_STOP_SIGNAL_WORKER,
} pj_audio_stop_result_t;

void pj_audio_lifecycle_init(pj_audio_lifecycle_t *state);
int pj_audio_lifecycle_begin_record(pj_audio_lifecycle_t *state);
pj_audio_stop_result_t pj_audio_lifecycle_request_record_stop(
    pj_audio_lifecycle_t *state);
int pj_audio_lifecycle_begin_processing(pj_audio_lifecycle_t *state);
void pj_audio_lifecycle_finish_record(pj_audio_lifecycle_t *state);
void pj_audio_lifecycle_cancel_processing(pj_audio_lifecycle_t *state);
void pj_audio_lifecycle_finish_processing(pj_audio_lifecycle_t *state);
int pj_audio_lifecycle_begin_playback(pj_audio_lifecycle_t *state);
pj_audio_stop_result_t pj_audio_lifecycle_request_playback_stop(
    pj_audio_lifecycle_t *state);
void pj_audio_lifecycle_finish_playback(pj_audio_lifecycle_t *state);
int pj_audio_lifecycle_active(const pj_audio_lifecycle_t *state);

#ifdef __cplusplus
}
#endif
