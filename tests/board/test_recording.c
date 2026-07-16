#include "pj_recording.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

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

int main(void)
{
    test_progress_comes_only_from_committed_pcm();
    test_raw_publication_completes_before_optional_processing();
    test_capture_failures_never_publish_success();
    test_restart_has_no_phantom_completion();
    test_invalid_transitions_and_overflow_are_rejected();
    puts("recording lifecycle tests passed");
    return 0;
}
