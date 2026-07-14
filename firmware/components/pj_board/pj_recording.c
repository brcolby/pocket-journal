#include "pj_recording.h"

#include <limits.h>
#include <string.h>

static int format_valid(uint32_t sample_rate, uint16_t channels,
                        uint16_t bits_per_sample)
{
    return sample_rate != 0 && channels != 0 && bits_per_sample != 0 &&
           bits_per_sample % 8U == 0;
}

static uint64_t bytes_per_second(const pj_recording_t *recording)
{
    return (uint64_t)recording->sample_rate * recording->channels *
           (recording->bits_per_sample / 8U);
}

static void complete(pj_recording_t *recording, int succeeded)
{
    recording->phase = succeeded ? PJ_RECORDING_READY : PJ_RECORDING_FAILED;
    recording->completion_succeeded = succeeded != 0;
    recording->completion_pending = 1;
}

void pj_recording_init(pj_recording_t *recording)
{
    if (recording == NULL) {
        return;
    }
    memset(recording, 0, sizeof(*recording));
    recording->phase = PJ_RECORDING_IDLE;
}

int pj_recording_start(pj_recording_t *recording, uint32_t sample_rate,
                       uint16_t channels, uint16_t bits_per_sample)
{
    if (recording == NULL ||
        !format_valid(sample_rate, channels, bits_per_sample) ||
        recording->completion_pending ||
        recording->phase == PJ_RECORDING_CAPTURING ||
        recording->phase == PJ_RECORDING_STOPPING ||
        recording->phase == PJ_RECORDING_PROCESSING) {
        return 0;
    }
    recording->captured_bytes = 0;
    recording->sample_rate = sample_rate;
    recording->channels = channels;
    recording->bits_per_sample = bits_per_sample;
    recording->phase = PJ_RECORDING_CAPTURING;
    recording->completion_pending = 0;
    recording->completion_succeeded = 0;
    return 1;
}

int pj_recording_commit(pj_recording_t *recording, size_t bytes)
{
    if (recording == NULL ||
        (recording->phase != PJ_RECORDING_CAPTURING &&
         recording->phase != PJ_RECORDING_STOPPING) || bytes == 0) {
        return 0;
    }
    uint64_t frame_bytes = (uint64_t)recording->channels *
                           (recording->bits_per_sample / 8U);
    if (frame_bytes == 0 || bytes % frame_bytes != 0 ||
        UINT64_MAX - recording->captured_bytes < bytes) {
        return 0;
    }
    recording->captured_bytes += bytes;
    return 1;
}

int pj_recording_request_stop(pj_recording_t *recording)
{
    if (recording == NULL) {
        return 0;
    }
    if (recording->phase == PJ_RECORDING_STOPPING) {
        return 1;
    }
    if (recording->phase != PJ_RECORDING_CAPTURING) {
        return 0;
    }
    recording->phase = PJ_RECORDING_STOPPING;
    return 1;
}

int pj_recording_finish_capture(pj_recording_t *recording, int succeeded)
{
    if (recording == NULL ||
        (recording->phase != PJ_RECORDING_CAPTURING &&
         recording->phase != PJ_RECORDING_STOPPING)) {
        return 0;
    }
    if (!succeeded || recording->captured_bytes == 0) {
        complete(recording, 0);
    } else {
        recording->phase = PJ_RECORDING_PROCESSING;
    }
    return 1;
}

int pj_recording_finish_processing(pj_recording_t *recording, int succeeded)
{
    if (recording == NULL || recording->phase != PJ_RECORDING_PROCESSING) {
        return 0;
    }
    complete(recording, succeeded);
    return 1;
}

uint64_t pj_recording_elapsed_ms(const pj_recording_t *recording)
{
    if (recording == NULL) {
        return 0;
    }
    uint64_t rate = bytes_per_second(recording);
    if (rate == 0) {
        return 0;
    }
    uint64_t whole_seconds = recording->captured_bytes / rate;
    uint64_t remainder = recording->captured_bytes % rate;
    if (whole_seconds > UINT64_MAX / 1000U) {
        return UINT64_MAX;
    }
    return whole_seconds * 1000U + remainder * 1000U / rate;
}

int pj_recording_take_completion(pj_recording_t *recording, int *succeeded)
{
    if (recording == NULL || succeeded == NULL ||
        !recording->completion_pending) {
        return 0;
    }
    *succeeded = recording->completion_succeeded != 0;
    recording->completion_pending = 0;
    return 1;
}
