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

static void test_stop_process_and_completion_are_ordered_and_one_shot(void)
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
    assert(recording.phase == PJ_RECORDING_PROCESSING);
    assert(!pj_recording_take_completion(&recording, &succeeded));
    assert(pj_recording_finish_processing(&recording, 1));
    assert(recording.phase == PJ_RECORDING_READY);
    assert(pj_recording_take_completion(&recording, &succeeded));
    assert(succeeded);
    assert(!pj_recording_take_completion(&recording, &succeeded));
}

static void test_capture_and_processing_failures_never_publish_success(void)
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

    assert(pj_recording_start(&recording, 16000, 1, 16));
    assert(pj_recording_commit(&recording, 32000));
    assert(pj_recording_finish_capture(&recording, 1));
    assert(pj_recording_finish_processing(&recording, 0));
    assert(pj_recording_take_completion(&recording, &succeeded));
    assert(!succeeded);
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
    test_stop_process_and_completion_are_ordered_and_one_shot();
    test_capture_and_processing_failures_never_publish_success();
    test_invalid_transitions_and_overflow_are_rejected();
    puts("recording lifecycle tests passed");
    return 0;
}
