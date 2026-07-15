#include "pj_audio_lifecycle.h"

#include <assert.h>
#include <stdio.h>

static void test_record_stop_and_processing_handoff(void)
{
    pj_audio_lifecycle_t state;
    pj_audio_lifecycle_init(&state);

    assert(!pj_audio_lifecycle_active(&state));
    assert(pj_audio_lifecycle_request_record_stop(&state) ==
           PJ_AUDIO_STOP_INACTIVE);
    assert(pj_audio_lifecycle_begin_record(&state));
    assert(pj_audio_lifecycle_active(&state));
    assert(!pj_audio_lifecycle_begin_record(&state));
    assert(!pj_audio_lifecycle_begin_playback(&state));
    assert(pj_audio_lifecycle_request_record_stop(&state) ==
           PJ_AUDIO_STOP_SIGNAL_WORKER);
    assert(pj_audio_lifecycle_request_record_stop(&state) ==
           PJ_AUDIO_STOP_ALREADY_REQUESTED);

    assert(pj_audio_lifecycle_begin_processing(&state));
    assert(!pj_audio_lifecycle_begin_processing(&state));
    pj_audio_lifecycle_finish_record(&state);
    assert(pj_audio_lifecycle_active(&state));
    assert(!pj_audio_lifecycle_begin_record(&state));
    assert(!pj_audio_lifecycle_begin_playback(&state));

    pj_audio_lifecycle_finish_processing(&state);
    assert(!pj_audio_lifecycle_active(&state));
    assert(pj_audio_lifecycle_begin_record(&state));
    pj_audio_lifecycle_finish_record(&state);
}

static void test_failed_processing_start_can_be_rolled_back(void)
{
    pj_audio_lifecycle_t state;
    pj_audio_lifecycle_init(&state);
    assert(pj_audio_lifecycle_begin_record(&state));
    assert(pj_audio_lifecycle_begin_processing(&state));
    pj_audio_lifecycle_cancel_processing(&state);
    pj_audio_lifecycle_finish_record(&state);
    assert(!pj_audio_lifecycle_active(&state));
}

static void test_playback_stop_is_idempotent(void)
{
    pj_audio_lifecycle_t state;
    pj_audio_lifecycle_init(&state);
    assert(pj_audio_lifecycle_begin_playback(&state));
    assert(!pj_audio_lifecycle_begin_record(&state));
    assert(pj_audio_lifecycle_request_playback_stop(&state) ==
           PJ_AUDIO_STOP_SIGNAL_WORKER);
    assert(pj_audio_lifecycle_request_playback_stop(&state) ==
           PJ_AUDIO_STOP_ALREADY_REQUESTED);
    pj_audio_lifecycle_finish_playback(&state);
    assert(!pj_audio_lifecycle_active(&state));
    assert(pj_audio_lifecycle_request_playback_stop(&state) ==
           PJ_AUDIO_STOP_INACTIVE);
}

int main(void)
{
    test_record_stop_and_processing_handoff();
    test_failed_processing_start_can_be_rolled_back();
    test_playback_stop_is_idempotent();
    puts("audio lifecycle tests passed");
    return 0;
}
