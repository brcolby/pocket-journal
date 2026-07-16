#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PJ_RECORDING_IDLE = 0,
    PJ_RECORDING_CAPTURING,
    PJ_RECORDING_STOPPING,
    PJ_RECORDING_READY,
    PJ_RECORDING_FAILED,
} pj_recording_phase_t;

typedef struct {
    uint64_t captured_bytes;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    pj_recording_phase_t phase;
    uint8_t completion_pending;
    uint8_t completion_succeeded;
} pj_recording_t;

void pj_recording_init(pj_recording_t *recording);
int pj_recording_start(pj_recording_t *recording, uint32_t sample_rate,
                       uint16_t channels, uint16_t bits_per_sample);
int pj_recording_commit(pj_recording_t *recording, size_t bytes);
int pj_recording_request_stop(pj_recording_t *recording);
/* Complete capture only after the validated raw WAV has been published. */
int pj_recording_finish_capture(pj_recording_t *recording, int succeeded);
uint64_t pj_recording_elapsed_ms(const pj_recording_t *recording);

/* Returns one completion event at most once for each start. */
int pj_recording_take_completion(pj_recording_t *recording, int *succeeded);

#ifdef __cplusplus
}
#endif
