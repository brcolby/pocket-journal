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

/* Returns positive for valid, zero for invalid/missing, negative for I/O error. */
typedef int (*pj_recording_path_validator_t)(const char *path, void *context);

typedef enum {
    PJ_RECORDING_RAW_PUBLISH_FAILED = 0,
    PJ_RECORDING_RAW_PUBLISH_SUCCEEDED,
    PJ_RECORDING_RAW_PUBLISH_RETRYABLE,
} pj_recording_raw_publish_result_t;

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

/*
 * Publishes a prevalidated finalized WAV without deleting the only copy on an
 * I/O fault. A failed post-rename validation is rolled back when possible.
 */
pj_recording_raw_publish_result_t pj_recording_publish_raw(
    const char *temporary_path, const char *published_path,
    pj_recording_path_validator_t validate, void *validate_context);

/*
 * Atomically replace a published raw recording with processed audio. Any
 * transcript marker is moved aside before the audio bytes change, so a crash
 * or successful replacement leaves the new audio pending for Sync.
 */
int pj_recording_replace_processed(
    const char *processed_path, const char *published_path,
    const char *transcript_marker_path,
    pj_recording_path_validator_t validate, void *validate_context);

#ifdef __cplusplus
}
#endif
