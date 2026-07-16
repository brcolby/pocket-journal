#include "pj_recording.h"
#include "pj_transcript_upload.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const uint8_t *accepted[2];
    size_t accepted_size[2];
    unsigned calls;
    unsigned fail_on_call;
} replacement_validation_t;

static const char *g_fail_remove_path;
static const char *g_fail_rename_old_path;
static const char *g_fail_rename_new_path;

int pj_recording_fs_remove(const char *path)
{
    if (g_fail_remove_path != NULL &&
        strcmp(path, g_fail_remove_path) == 0) {
        g_fail_remove_path = NULL;
        errno = EIO;
        return -1;
    }
    return remove(path);
}

int pj_recording_fs_rename(const char *old_path, const char *new_path)
{
    if (g_fail_rename_old_path != NULL &&
        g_fail_rename_new_path != NULL &&
        strcmp(old_path, g_fail_rename_old_path) == 0 &&
        strcmp(new_path, g_fail_rename_new_path) == 0) {
        g_fail_rename_old_path = NULL;
        g_fail_rename_new_path = NULL;
        errno = EIO;
        return -1;
    }
    return rename(old_path, new_path);
}

static void reset_fs_faults(void)
{
    g_fail_remove_path = NULL;
    g_fail_rename_old_path = NULL;
    g_fail_rename_new_path = NULL;
}

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
    if (validation->fail_on_call == validation->calls) {
        return 0;
    }
    uint8_t actual[64];
    for (size_t i = 0; i < 2U; i++) {
        if (validation->accepted[i] == NULL) {
            continue;
        }
        assert(validation->accepted_size[i] <= sizeof(actual));
        if (read_bytes(path, actual, validation->accepted_size[i]) &&
            memcmp(actual, validation->accepted[i],
                   validation->accepted_size[i]) == 0) {
            return 1;
        }
    }
    return 0;
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
    reset_fs_faults();
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

static void test_raw_publish_success_moves_one_valid_copy(void)
{
    static const char temporary[] = "build/raw-publish.wav.tmp";
    static const char published[] = "build/raw-publish.wav";
    static const uint8_t raw[] = "finalized microphone capture";
    (void)remove(temporary);
    (void)remove(published);
    reset_fs_faults();
    assert(write_bytes(temporary, raw, sizeof(raw)));
    replacement_validation_t validation = {
        .accepted = {raw, NULL},
        .accepted_size = {sizeof(raw), 0U},
    };
    assert(pj_recording_publish_raw(
        temporary, published, replacement_validate, &validation) ==
        PJ_RECORDING_RAW_PUBLISH_SUCCEEDED);
    assert(validation.calls == 2U);
    assert(!file_exists(temporary));
    assert(file_hash(published) != 0U);
    (void)remove(published);
}

static void test_raw_publish_rename_failure_preserves_temporary(void)
{
    static const char temporary[] = "build/raw-rename-fail.wav.tmp";
    static const char published[] = "build/raw-rename-fail.wav";
    static const uint8_t raw[] = "capture retained across rename fault";
    (void)remove(temporary);
    (void)remove(published);
    reset_fs_faults();
    assert(write_bytes(temporary, raw, sizeof(raw)));
    uint64_t expected_hash = file_hash(temporary);
    replacement_validation_t validation = {
        .accepted = {raw, NULL},
        .accepted_size = {sizeof(raw), 0U},
    };
    g_fail_rename_old_path = temporary;
    g_fail_rename_new_path = published;
    assert(pj_recording_publish_raw(
        temporary, published, replacement_validate, &validation) ==
        PJ_RECORDING_RAW_PUBLISH_RETRYABLE);
    assert(file_exists(temporary));
    assert(file_hash(temporary) == expected_hash);
    assert(!file_exists(published));
    (void)remove(temporary);
    reset_fs_faults();
}

static void test_raw_publish_post_rename_validation_fault_rolls_back(void)
{
    static const char temporary[] = "build/raw-validation-fail.wav.tmp";
    static const char published[] = "build/raw-validation-fail.wav";
    static const uint8_t raw[] = "capture retained across transient read fault";
    (void)remove(temporary);
    (void)remove(published);
    reset_fs_faults();
    assert(write_bytes(temporary, raw, sizeof(raw)));
    uint64_t expected_hash = file_hash(temporary);
    replacement_validation_t validation = {
        .accepted = {raw, NULL},
        .accepted_size = {sizeof(raw), 0U},
        .fail_on_call = 2U,
    };
    assert(pj_recording_publish_raw(
        temporary, published, replacement_validate, &validation) ==
        PJ_RECORDING_RAW_PUBLISH_RETRYABLE);
    assert(validation.calls == 2U);
    assert(file_exists(temporary));
    assert(file_hash(temporary) == expected_hash);
    assert(!file_exists(published));
    (void)remove(temporary);
}

static void test_raw_publish_rollback_failure_leaves_published_copy(void)
{
    static const char temporary[] = "build/raw-rollback-fail.wav.tmp";
    static const char published[] = "build/raw-rollback-fail.wav";
    static const uint8_t raw[] = "capture remains visible when rollback fails";
    (void)remove(temporary);
    (void)remove(published);
    reset_fs_faults();
    assert(write_bytes(temporary, raw, sizeof(raw)));
    uint64_t expected_hash = file_hash(temporary);
    replacement_validation_t validation = {
        .accepted = {raw, NULL},
        .accepted_size = {sizeof(raw), 0U},
        .fail_on_call = 2U,
    };
    g_fail_rename_old_path = published;
    g_fail_rename_new_path = temporary;
    assert(pj_recording_publish_raw(
        temporary, published, replacement_validate, &validation) ==
        PJ_RECORDING_RAW_PUBLISH_RETRYABLE);
    assert(!file_exists(temporary));
    assert(file_exists(published));
    assert(file_hash(published) == expected_hash);
    (void)remove(published);
    reset_fs_faults();
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
        .accepted = {raw, enhanced},
        .accepted_size = {sizeof(raw), sizeof(enhanced)},
    };
    assert(pj_recording_replace_processed(
        processed, published, transcript, replacement_validate, &validation));
    assert(validation.calls == 3U);
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
        .accepted = {raw, enhanced},
        .accepted_size = {sizeof(raw), sizeof(enhanced)},
        .fail_on_call = 3U,
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

static void test_failed_validation_remove_keeps_raw_backup(void)
{
    static const char published[] = "build/recording-remove-failure.wav";
    static const char processed[] = "build/recording-remove-failure.processed";
    static const char transcript[] = "build/recording-remove-failure.json";
    static const char backup[] = "build/recording-remove-failure.wav.bak";
    static const char marker_temporary[] =
        "build/recording-remove-failure.json.tmp";
    static const uint8_t raw[] = "raw survives target remove failure";
    static const uint8_t enhanced[] = "invalid processed target remains";
    static const char synced[] = "{\"text\":\"raw marker\"}";
    remove_replacement_artifacts(published, processed, transcript);
    assert(write_bytes(published, raw, sizeof(raw)));
    assert(write_bytes(processed, enhanced, sizeof(enhanced)));
    assert(write_bytes(transcript, synced, sizeof(synced) - 1U));

    replacement_validation_t validation = {
        .accepted = {raw, enhanced},
        .accepted_size = {sizeof(raw), sizeof(enhanced)},
        .fail_on_call = 3U,
    };
    g_fail_remove_path = published;
    assert(!pj_recording_replace_processed(
        processed, published, transcript, replacement_validate, &validation));
    uint8_t actual_raw[sizeof(raw)];
    assert(read_bytes(backup, actual_raw, sizeof(actual_raw)));
    assert(memcmp(actual_raw, raw, sizeof(raw)) == 0);
    assert(file_exists(published));
    assert(!file_exists(transcript));
    assert(file_exists(marker_temporary));
    remove_replacement_artifacts(published, processed, transcript);
}

static void test_failed_validation_rename_keeps_raw_backup(void)
{
    static const char published[] = "build/recording-rename-failure.wav";
    static const char processed[] = "build/recording-rename-failure.processed";
    static const char transcript[] = "build/recording-rename-failure.json";
    static const char backup[] = "build/recording-rename-failure.wav.bak";
    static const char marker_temporary[] =
        "build/recording-rename-failure.json.tmp";
    static const uint8_t raw[] = "raw survives rollback rename failure";
    static const uint8_t enhanced[] = "processed target fails validation";
    static const char synced[] = "{\"text\":\"raw marker\"}";
    remove_replacement_artifacts(published, processed, transcript);
    assert(write_bytes(published, raw, sizeof(raw)));
    assert(write_bytes(processed, enhanced, sizeof(enhanced)));
    assert(write_bytes(transcript, synced, sizeof(synced) - 1U));

    replacement_validation_t validation = {
        .accepted = {raw, enhanced},
        .accepted_size = {sizeof(raw), sizeof(enhanced)},
        .fail_on_call = 3U,
    };
    g_fail_rename_old_path = backup;
    g_fail_rename_new_path = published;
    assert(!pj_recording_replace_processed(
        processed, published, transcript, replacement_validate, &validation));
    uint8_t actual_raw[sizeof(raw)];
    assert(read_bytes(backup, actual_raw, sizeof(actual_raw)));
    assert(memcmp(actual_raw, raw, sizeof(raw)) == 0);
    assert(!file_exists(published));
    assert(!file_exists(transcript));
    assert(file_exists(marker_temporary));
    remove_replacement_artifacts(published, processed, transcript);
}

static void test_backup_cleanup_failure_rolls_back_to_raw(void)
{
    static const char published[] = "build/recording-cleanup-failure.wav";
    static const char processed[] = "build/recording-cleanup-failure.processed";
    static const char transcript[] = "build/recording-cleanup-failure.json";
    static const char backup[] = "build/recording-cleanup-failure.wav.bak";
    static const uint8_t raw[] = "raw restored when backup cleanup fails";
    static const uint8_t enhanced[] = "valid processed target not committed";
    static const char synced[] = "{\"text\":\"raw marker\"}";
    remove_replacement_artifacts(published, processed, transcript);
    assert(write_bytes(published, raw, sizeof(raw)));
    assert(write_bytes(processed, enhanced, sizeof(enhanced)));
    assert(write_bytes(transcript, synced, sizeof(synced) - 1U));

    replacement_validation_t validation = {
        .accepted = {raw, enhanced},
        .accepted_size = {sizeof(raw), sizeof(enhanced)},
    };
    g_fail_remove_path = backup;
    assert(!pj_recording_replace_processed(
        processed, published, transcript, replacement_validate, &validation));
    uint8_t actual_raw[sizeof(raw)];
    assert(read_bytes(published, actual_raw, sizeof(actual_raw)));
    assert(memcmp(actual_raw, raw, sizeof(raw)) == 0);
    assert(!file_exists(backup));
    assert(!file_exists(transcript));
    assert(!file_exists("build/recording-cleanup-failure.json.tmp"));
    remove_replacement_artifacts(published, processed, transcript);
}

static void test_marker_cleanup_failure_rolls_back_before_commit(void)
{
    static const char published[] = "build/recording-marker-failure.wav";
    static const char processed[] = "build/recording-marker-failure.processed";
    static const char transcript[] = "build/recording-marker-failure.json";
    static const char backup[] = "build/recording-marker-failure.wav.bak";
    static const char marker_temporary[] =
        "build/recording-marker-failure.json.tmp";
    static const uint8_t raw[] = "raw retained until marker invalidation";
    static const uint8_t enhanced[] = "processed audio awaiting commit";
    static const char synced[] = "{\"text\":\"raw marker\"}";
    remove_replacement_artifacts(published, processed, transcript);
    assert(write_bytes(published, raw, sizeof(raw)));
    assert(write_bytes(processed, enhanced, sizeof(enhanced)));
    assert(write_bytes(transcript, synced, sizeof(synced) - 1U));

    replacement_validation_t validation = {
        .accepted = {raw, enhanced},
        .accepted_size = {sizeof(raw), sizeof(enhanced)},
    };
    g_fail_remove_path = marker_temporary;
    assert(!pj_recording_replace_processed(
        processed, published, transcript, replacement_validate, &validation));
    uint8_t actual_raw[sizeof(raw)];
    uint8_t actual_transcript[sizeof(synced) - 1U];
    assert(read_bytes(published, actual_raw, sizeof(actual_raw)));
    assert(memcmp(actual_raw, raw, sizeof(raw)) == 0);
    assert(!file_exists(backup));
    assert(read_bytes(transcript, actual_transcript, sizeof(actual_transcript)));
    assert(memcmp(actual_transcript, synced, sizeof(actual_transcript)) == 0);
    assert(!file_exists(marker_temporary));
    remove_replacement_artifacts(published, processed, transcript);
}

static void test_existing_transaction_artifacts_are_never_overwritten(void)
{
    static const char published[] = "build/recording-stale.wav";
    static const char processed[] = "build/recording-stale.processed";
    static const char transcript[] = "build/recording-stale.json";
    static const char backup[] = "build/recording-stale.wav.bak";
    static const uint8_t raw[] = "current raw recording";
    static const uint8_t older_raw[] = "only raw backup from prior transaction";
    static const uint8_t enhanced[] = "new processed candidate";
    remove_replacement_artifacts(published, processed, transcript);
    assert(write_bytes(published, raw, sizeof(raw)));
    assert(write_bytes(processed, enhanced, sizeof(enhanced)));
    assert(write_bytes(backup, older_raw, sizeof(older_raw)));

    replacement_validation_t validation = {
        .accepted = {raw, enhanced},
        .accepted_size = {sizeof(raw), sizeof(enhanced)},
    };
    assert(!pj_recording_replace_processed(
        processed, published, transcript, replacement_validate, &validation));
    uint8_t actual_backup[sizeof(older_raw)];
    assert(read_bytes(backup, actual_backup, sizeof(actual_backup)));
    assert(memcmp(actual_backup, older_raw, sizeof(older_raw)) == 0);
    assert(file_exists(processed));
    remove_replacement_artifacts(published, processed, transcript);
}

int main(void)
{
    test_progress_comes_only_from_committed_pcm();
    test_raw_publication_completes_before_optional_processing();
    test_capture_failures_never_publish_success();
    test_restart_has_no_phantom_completion();
    test_invalid_transitions_and_overflow_are_rejected();
    test_raw_publish_success_moves_one_valid_copy();
    test_raw_publish_rename_failure_preserves_temporary();
    test_raw_publish_post_rename_validation_fault_rolls_back();
    test_raw_publish_rollback_failure_leaves_published_copy();
    test_raw_synced_processed_swap_becomes_pending_with_new_hash();
    test_failed_processed_swap_restores_raw_and_sync_marker();
    test_failed_validation_remove_keeps_raw_backup();
    test_failed_validation_rename_keeps_raw_backup();
    test_backup_cleanup_failure_rolls_back_to_raw();
    test_marker_cleanup_failure_rolls_back_before_commit();
    test_existing_transaction_artifacts_are_never_overwritten();
    puts("recording lifecycle tests passed");
    return 0;
}
