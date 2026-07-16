#include "pj_recording.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#if defined(__GNUC__)
#define PJ_RECORDING_WEAK __attribute__((weak))
#else
#define PJ_RECORDING_WEAK
#endif

PJ_RECORDING_WEAK int pj_recording_fs_remove(const char *path)
{
    return remove(path);
}

PJ_RECORDING_WEAK int pj_recording_fs_rename(const char *old_path,
                                             const char *new_path)
{
    return rename(old_path, new_path);
}

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
        recording->phase == PJ_RECORDING_STOPPING) {
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
    complete(recording, succeeded && recording->captured_bytes != 0);
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

static int format_artifact_path(char *output, size_t output_size,
                                const char *path, const char *suffix)
{
    if (output == NULL || output_size == 0U || path == NULL || suffix == NULL) {
        return 0;
    }
    int written = snprintf(output, output_size, "%s%s", path, suffix);
    return written >= 0 && (size_t)written < output_size;
}

static int remove_if_present(const char *path)
{
    return pj_recording_fs_remove(path) == 0 || errno == ENOENT;
}

static int path_absent(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return 0;
    }
    return errno == ENOENT;
}

static int restore_marker(const char *temporary_path, const char *marker_path,
                          int marker_moved)
{
    if (!marker_moved) {
        return 1;
    }
    if (!remove_if_present(marker_path)) {
        return 0;
    }
    return pj_recording_fs_rename(temporary_path, marker_path) == 0;
}

static int restore_audio(const char *backup_path, const char *published_path)
{
    if (!remove_if_present(published_path)) {
        return 0;
    }
    return pj_recording_fs_rename(backup_path, published_path) == 0;
}

pj_recording_raw_publish_result_t pj_recording_publish_raw(
    const char *temporary_path, const char *published_path,
    pj_recording_path_validator_t validate, void *validate_context)
{
    if (temporary_path == NULL || published_path == NULL || validate == NULL ||
        temporary_path[0] == '\0' || published_path[0] == '\0' ||
        strcmp(temporary_path, published_path) == 0 ||
        validate(temporary_path, validate_context) <= 0) {
        return PJ_RECORDING_RAW_PUBLISH_FAILED;
    }
    if (pj_recording_fs_rename(temporary_path, published_path) != 0) {
        return PJ_RECORDING_RAW_PUBLISH_RETRYABLE;
    }
    if (validate(published_path, validate_context) > 0) {
        return PJ_RECORDING_RAW_PUBLISH_SUCCEEDED;
    }
    (void)pj_recording_fs_rename(published_path, temporary_path);
    return PJ_RECORDING_RAW_PUBLISH_RETRYABLE;
}

int pj_recording_replace_processed(
    const char *processed_path, const char *published_path,
    const char *transcript_marker_path,
    pj_recording_path_validator_t validate, void *validate_context)
{
    char audio_backup[256];
    char marker_temporary[256];
    if (processed_path == NULL || published_path == NULL ||
        transcript_marker_path == NULL || validate == NULL ||
        !format_artifact_path(audio_backup, sizeof(audio_backup),
                              published_path, ".bak") ||
        !format_artifact_path(marker_temporary, sizeof(marker_temporary),
                              transcript_marker_path, ".tmp") ||
        validate(processed_path, validate_context) <= 0 ||
        validate(published_path, validate_context) <= 0 ||
        !path_absent(audio_backup) || !path_absent(marker_temporary)) {
        return 0;
    }

    int marker_moved =
        pj_recording_fs_rename(transcript_marker_path, marker_temporary) == 0;
    if (!marker_moved && errno != ENOENT) {
        return 0;
    }
    if (pj_recording_fs_rename(published_path, audio_backup) != 0) {
        (void)restore_marker(marker_temporary, transcript_marker_path,
                             marker_moved);
        return 0;
    }
    if (pj_recording_fs_rename(processed_path, published_path) != 0) {
        int audio_restored = restore_audio(audio_backup, published_path);
        if (audio_restored) {
            (void)restore_marker(marker_temporary, transcript_marker_path,
                                 marker_moved);
        }
        return 0;
    }
    if (validate(published_path, validate_context) <= 0) {
        int audio_restored = restore_audio(audio_backup, published_path);
        if (audio_restored) {
            (void)restore_marker(marker_temporary, transcript_marker_path,
                                 marker_moved);
        }
        return 0;
    }

    /* Invalidate stale transcript provenance before removing the raw commit record. */
    if (marker_moved && !remove_if_present(marker_temporary)) {
        int audio_restored = restore_audio(audio_backup, published_path);
        if (audio_restored) {
            (void)restore_marker(marker_temporary, transcript_marker_path,
                                 marker_moved);
        }
        return 0;
    }
    if (!remove_if_present(audio_backup)) {
        (void)restore_audio(audio_backup, published_path);
        return 0;
    }
    return 1;
}
