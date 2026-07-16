#include "pj_recording.h"
#include "pj_transcript_upload.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const uint8_t *expected;
    size_t expected_size;
    unsigned calls;
    int fail_after_publish;
} replacement_validation_t;

static int write_bytes(const char *path, const void *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    int written = file != NULL && fwrite(data, 1, size, file) == size;
    return file != NULL && fclose(file) == 0 && written;
}

static int read_bytes(const char *path, void *data, size_t size)
{
    FILE *file = fopen(path, "rb");
    int read = file != NULL && fread(data, 1, size, file) == size &&
               fgetc(file) == EOF;
    return file != NULL && fclose(file) == 0 && read;
}

static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    fclose(file);
    return 1;
}

static uint64_t file_hash(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    uint64_t hash = UINT64_C(1469598103934665603);
    int value;
    while ((value = fgetc(file)) != EOF) {
        hash ^= (uint8_t)value;
        hash *= UINT64_C(1099511628211);
    }
    assert(!ferror(file));
    assert(fclose(file) == 0);
    return hash;
}

static int replacement_validate(const char *path, void *opaque)
{
    replacement_validation_t *validation = opaque;
    validation->calls++;
    if (validation->fail_after_publish && validation->calls > 1U) {
        return 0;
    }
    uint8_t actual[64];
    assert(validation->expected_size <= sizeof(actual));
    return read_bytes(path, actual, validation->expected_size) &&
           memcmp(actual, validation->expected, validation->expected_size) == 0;
}

static void remove_replacement_artifacts(const char *published,
                                         const char *processed,
                                         const char *transcript)
{
    char path[128];
    (void)remove(published);
    (void)remove(processed);
    (void)remove(transcript);
    (void)snprintf(path, sizeof(path), "%s.bak", published);
    (void)remove(path);
    (void)snprintf(path, sizeof(path), "%s.tmp", transcript);
    (void)remove(path);
}

static void test_progress_comes_only_from_committed_pcm(void)
{
    pj_recording_t recording;
    pj_recording_init(&recording);
    assert(pj_recording_start(&recording, 16000, 1, 16));
    assert(pj_recording_elapsed_ms(&recording) == 0);
    assert(pj_recording_commit(&recording, 16000));
    assert(pj_recording_elapsed_ms(&recording) == 500);
    assert(!pj_recording_commit(&recording, 1));
    assert(pj_recording_elapsed_ms(&recording) == 500);
    assert(pj_recording_commit(&recording, 16000));
    assert(pj_recording_elapsed_ms(&recording) == 1000);
}

static void test_raw_publication_completes_before_optional_processing(void)
{
    pj_recording_t recording;
    int succeeded = 0;
    pj_recording_init(&recording);
    assert(pj_recording_start(&recording, 16000, 1, 16));
    assert(pj_recording_commit(&recording, 32000));
    assert(pj_recording_request_stop(&recording));
    assert(pj_recording_request_stop(&recording));
    assert(pj_recording_commit(&recording, 512));
    assert(pj_recording_finish_capture(&recording, 1));
    assert(recording.phase == PJ_RECORDING_READY);
    assert(!pj_recording_start(&recording, 16000, 1, 16));
    assert(pj_recording_take_completion(&recording, &succeeded));
    assert(succeeded);
    assert(!pj_recording_take_completion(&recording, &succeeded));
    assert(pj_recording_start(&recording, 16000, 1, 16));
}

static void test_capture_failures_never_publish_success(void)
{
    pj_recording_t recording;
    int succeeded = 1;
    pj_recording_init(&recording);

    assert(pj_recording_start(&recording, 16000, 1, 16));
    assert(pj_recording_finish_capture(&recording, 1));
    assert(recording.phase == PJ_RECORDING_FAILED);
    assert(pj_recording_take_completion(&recording, &succeeded));
    assert(!succeeded);

    assert(pj_recording_start(&recording, 16000, 1, 16));
    assert(pj_recording_commit(&recording, 32000));
    assert(pj_recording_finish_capture(&recording, 0));
    assert(pj_recording_take_completion(&recording, &succeeded));
    assert(!succeeded);
}

static void test_restart_has_no_phantom_completion(void)
{
    pj_recording_t before_restart;
    int succeeded = 0;
    pj_recording_init(&before_restart);
    assert(pj_recording_start(&before_restart, 16000, 1, 16));
    assert(pj_recording_commit(&before_restart, 32000));
    assert(pj_recording_finish_capture(&before_restart, 1));

    pj_recording_t after_restart;
    pj_recording_init(&after_restart);
    assert(after_restart.phase == PJ_RECORDING_IDLE);
    assert(!pj_recording_take_completion(&after_restart, &succeeded));
    assert(pj_recording_start(&after_restart, 16000, 1, 16));
}

static void test_invalid_transitions_and_overflow_are_rejected(void)
{
    pj_recording_t recording;
    pj_recording_init(&recording);
    assert(!pj_recording_start(&recording, 0, 1, 16));
    assert(!pj_recording_request_stop(&recording));
    assert(pj_recording_start(&recording, 16000, 2, 16));
    assert(!pj_recording_start(&recording, 16000, 1, 16));
    assert(pj_recording_commit(&recording, 64000));
    assert(pj_recording_elapsed_ms(&recording) == 1000);
    recording.captured_bytes = UINT64_MAX - 1U;
    assert(!pj_recording_commit(&recording, 4));
}

static void test_raw_synced_processed_swap_becomes_pending_with_new_hash(void)
{
    static const char published[] = "build/recording-swap.wav";
    static const char processed[] = "build/recording-swap.processed";
    static const char transcript[] = "build/recording-swap.json";
    static const uint8_t raw[] = "published raw microphone samples";
    static const uint8_t enhanced[] = "filtered normalized audio samples";
    static const char synced[] =
        "{\"text\":\"raw transcript\",\"source\":{\"sha256\":\"old\"}}";
    remove_replacement_artifacts(published, processed, transcript);
    assert(write_bytes(published, raw, sizeof(raw)));
    assert(write_bytes(processed, enhanced, sizeof(enhanced)));
    assert(write_bytes(transcript, synced, sizeof(synced) - 1U));
    char transcript_label[32];
    assert(pj_transcript_marker_load(transcript, transcript_label,
                                     sizeof(transcript_label)));
    assert(strcmp(transcript_label, "raw transcript") == 0);
    uint64_t raw_hash = file_hash(published);
    uint64_t enhanced_hash = file_hash(processed);
    assert(raw_hash != enhanced_hash);

    replacement_validation_t validation = {
        .expected = enhanced,
        .expected_size = sizeof(enhanced),
    };
    assert(pj_recording_replace_processed(
        processed, published, transcript, replacement_validate, &validation));
    assert(validation.calls == 2U);
    assert(file_hash(published) == enhanced_hash);
    assert(file_hash(published) != raw_hash);
    assert(!file_exists(transcript));
    assert(!pj_transcript_marker_load(transcript, transcript_label,
                                      sizeof(transcript_label)));
    assert(!file_exists("build/recording-swap.wav.bak"));
    assert(!file_exists("build/recording-swap.json.tmp"));
    remove_replacement_artifacts(published, processed, transcript);
}

static void test_failed_processed_swap_restores_raw_and_sync_marker(void)
{
    static const char published[] = "build/recording-rollback.wav";
    static const char processed[] = "build/recording-rollback.processed";
    static const char transcript[] = "build/recording-rollback.json";
    static const uint8_t raw[] = "raw bytes retained on failure";
    static const uint8_t enhanced[] = "processed bytes rejected after publish";
    static const char synced[] = "{\"text\":\"still valid for raw\"}";
    remove_replacement_artifacts(published, processed, transcript);
    assert(write_bytes(published, raw, sizeof(raw)));
    assert(write_bytes(processed, enhanced, sizeof(enhanced)));
    assert(write_bytes(transcript, synced, sizeof(synced) - 1U));

    replacement_validation_t validation = {
        .expected = enhanced,
        .expected_size = sizeof(enhanced),
        .fail_after_publish = 1,
    };
    assert(!pj_recording_replace_processed(
        processed, published, transcript, replacement_validate, &validation));
    uint8_t actual_raw[sizeof(raw)];
    uint8_t actual_transcript[sizeof(synced) - 1U];
    assert(read_bytes(published, actual_raw, sizeof(actual_raw)));
    assert(memcmp(actual_raw, raw, sizeof(raw)) == 0);
    assert(read_bytes(transcript, actual_transcript, sizeof(actual_transcript)));
    assert(memcmp(actual_transcript, synced, sizeof(actual_transcript)) == 0);
    assert(!file_exists("build/recording-rollback.wav.bak"));
    assert(!file_exists("build/recording-rollback.json.tmp"));
    remove_replacement_artifacts(published, processed, transcript);
}

int main(void)
{
    test_progress_comes_only_from_committed_pcm();
    test_raw_publication_completes_before_optional_processing();
    test_capture_failures_never_publish_success();
    test_restart_has_no_phantom_completion();
    test_invalid_transitions_and_overflow_are_rejected();
    test_raw_synced_processed_swap_becomes_pending_with_new_hash();
    test_failed_processed_swap_restores_raw_and_sync_marker();
    puts("recording lifecycle tests passed");
    return 0;
}
