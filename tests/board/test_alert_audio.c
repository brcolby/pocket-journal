#include "pj_alert_audio.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    pj_alert_audio_t *audio;
    int prepare_calls;
    int write_calls;
    int finish_calls;
    int fail_prepare;
    int fail_write;
    int fail_finish;
    int cancel_on_prepare;
    int cancel_on_write;
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t volume;
    char events[64];
    size_t event_count;
    int16_t last_block[PJ_ALERT_AUDIO_BLOCK_SAMPLES];
} mock_audio_t;

static int mock_prepare(void *context, uint32_t sample_rate, uint8_t channels,
                        uint8_t volume)
{
    mock_audio_t *mock = context;
    mock->prepare_calls++;
    if (mock->event_count < sizeof(mock->events)) {
        mock->events[mock->event_count++] = 'P';
    }
    mock->sample_rate = sample_rate;
    mock->channels = channels;
    mock->volume = volume;
    if (mock->cancel_on_prepare) {
        mock->cancel_on_prepare = 0;
        assert(pj_alert_audio_stop(mock->audio, mock->audio->alert_id) ==
               PJ_ALERT_AUDIO_OK);
    }
    if (mock->fail_prepare > 0) {
        mock->fail_prepare--;
        return 0;
    }
    return 1;
}

static int mock_write(void *context, const int16_t *samples, size_t frames)
{
    mock_audio_t *mock = context;
    mock->write_calls++;
    if (mock->event_count < sizeof(mock->events)) {
        mock->events[mock->event_count++] = 'W';
    }
    assert(frames == PJ_ALERT_AUDIO_BLOCK_FRAMES);
    memcpy(mock->last_block, samples, sizeof(mock->last_block));
    if (mock->cancel_on_write) {
        mock->cancel_on_write = 0;
        assert(pj_alert_audio_stop(mock->audio, mock->audio->alert_id) ==
               PJ_ALERT_AUDIO_OK);
    }
    if (mock->fail_write > 0) {
        mock->fail_write--;
        return 0;
    }
    return 1;
}

static int mock_finish(void *context)
{
    mock_audio_t *mock = context;
    mock->finish_calls++;
    if (mock->event_count < sizeof(mock->events)) {
        mock->events[mock->event_count++] = 'F';
    }
    if (mock->fail_finish > 0) {
        mock->fail_finish--;
        return 0;
    }
    return 1;
}

static void setup(pj_alert_audio_t *audio, mock_audio_t *mock, uint8_t volume)
{
    memset(mock, 0, sizeof(*mock));
    pj_alert_audio_io_t io = {
        .context = mock,
        .prepare = mock_prepare,
        .write = mock_write,
        .finish = mock_finish,
    };
    pj_alert_audio_init(audio, &io, volume);
    mock->audio = audio;
}

static int block_peak(const int16_t *samples)
{
    int peak = 0;
    for (size_t i = 0; i < PJ_ALERT_AUDIO_BLOCK_SAMPLES; ++i) {
        int sample = samples[i];
        if (sample < 0) {
            sample = -sample;
        }
        if (sample > peak) {
            peak = sample;
        }
    }
    return peak;
}

static void assert_silent(const int16_t *samples)
{
    for (size_t i = 0; i < PJ_ALERT_AUDIO_BLOCK_SAMPLES; ++i) {
        assert(samples[i] == 0);
    }
}

static void test_patterns_are_distinct_enveloped_stereo_and_bounded(void)
{
    int16_t alarm[PJ_ALERT_AUDIO_BLOCK_SAMPLES];
    int16_t timer[PJ_ALERT_AUDIO_BLOCK_SAMPLES];
    int16_t interval[PJ_ALERT_AUDIO_BLOCK_SAMPLES];
    assert(pj_alert_audio_generate_block(PJ_ALERT_AUDIO_ALARM, 100, 0, alarm));
    assert(pj_alert_audio_generate_block(PJ_ALERT_AUDIO_TIMER, 100, 0, timer));
    assert(pj_alert_audio_generate_block(PJ_ALERT_AUDIO_INTERVAL, 100, 0,
                                         interval));
    assert(memcmp(alarm, timer, sizeof(alarm)) != 0);
    assert(memcmp(timer, interval, sizeof(timer)) != 0);
    assert(memcmp(alarm, interval, sizeof(alarm)) != 0);
    assert(alarm[0] == 0 && alarm[1] == 0);
    for (size_t frame = 0; frame < PJ_ALERT_AUDIO_BLOCK_FRAMES; ++frame) {
        assert(alarm[frame * 2] == alarm[frame * 2 + 1]);
        assert(timer[frame * 2] == timer[frame * 2 + 1]);
        assert(interval[frame * 2] == interval[frame * 2 + 1]);
    }
    assert(block_peak(alarm) <= 24000);
    assert(block_peak(timer) <= 24000);
    assert(block_peak(interval) <= 24000);

    assert(pj_alert_audio_generate_block(PJ_ALERT_AUDIO_ALARM, 100, 4800,
                                         alarm));
    assert(pj_alert_audio_generate_block(PJ_ALERT_AUDIO_TIMER, 100, 1600,
                                         timer));
    assert(pj_alert_audio_generate_block(PJ_ALERT_AUDIO_INTERVAL, 100, 3600,
                                         interval));
    assert_silent(alarm);
    assert_silent(timer);
    assert_silent(interval);

    int16_t release[PJ_ALERT_AUDIO_BLOCK_SAMPLES];
    assert(pj_alert_audio_generate_block(PJ_ALERT_AUDIO_ALARM, 100, 4640,
                                         release));
    assert(release[PJ_ALERT_AUDIO_BLOCK_SAMPLES - 1] == 0);
    assert(block_peak(release) > 0);

    int16_t full[PJ_ALERT_AUDIO_BLOCK_SAMPLES];
    int16_t half[PJ_ALERT_AUDIO_BLOCK_SAMPLES];
    assert(pj_alert_audio_generate_block(PJ_ALERT_AUDIO_ALARM, 100, 160, full));
    assert(pj_alert_audio_generate_block(PJ_ALERT_AUDIO_ALARM, 50, 160, half));
    assert(block_peak(half) <= block_peak(full) / 2 + 1);
    assert(block_peak(half) >= block_peak(full) / 2 - 1);
    assert(pj_alert_audio_generate_block(PJ_ALERT_AUDIO_ALARM, 0, 160, half));
    assert_silent(half);
    assert(!pj_alert_audio_generate_block(0, 100, 0, full));
    assert(!pj_alert_audio_generate_block(PJ_ALERT_AUDIO_ALARM, 100, 0, NULL));
}

static void test_exact_id_lifecycle_and_guarded_transition(void)
{
    pj_alert_audio_t audio;
    mock_audio_t mock;
    int16_t scratch[PJ_ALERT_AUDIO_BLOCK_SAMPLES];
    setup(&audio, &mock, 60);
    assert(pj_alert_audio_present(&audio, 11, PJ_ALERT_AUDIO_ALARM, 0) ==
           PJ_ALERT_AUDIO_OK);
    assert(audio.state == PJ_ALERT_AUDIO_PLAYING && audio.alert_id == 11);
    assert(pj_alert_audio_present(&audio, 11, PJ_ALERT_AUDIO_ALARM, 0) ==
           PJ_ALERT_AUDIO_NO_CHANGE);
    assert(pj_alert_audio_present(&audio, 11, PJ_ALERT_AUDIO_TIMER, 0) ==
           PJ_ALERT_AUDIO_ERR_ARGUMENT);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_BLOCK_WRITTEN);
    assert(mock.prepare_calls == 1 && mock.write_calls == 1);
    assert(mock.sample_rate == PJ_ALERT_AUDIO_SAMPLE_RATE);
    assert(mock.channels == PJ_ALERT_AUDIO_CHANNELS);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_BLOCK_WRITTEN);
    assert(mock.prepare_calls == 1 && mock.write_calls == 2);

    assert(pj_alert_audio_defer(&audio, 10) == PJ_ALERT_AUDIO_NO_CHANGE);
    assert(pj_alert_audio_defer(&audio, 11) == PJ_ALERT_AUDIO_OK);
    assert(audio.state == PJ_ALERT_AUDIO_DEFERRED && mock.finish_calls == 0);
    assert(audio.cleanup_pending);
    assert(pj_alert_audio_defer(&audio, 11) == PJ_ALERT_AUDIO_NO_CHANGE);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_NO_CHANGE);
    assert(mock.finish_calls == 1 && !audio.cleanup_pending);
    assert(pj_alert_audio_resume(&audio, 10) == PJ_ALERT_AUDIO_NO_CHANGE);
    assert(pj_alert_audio_resume(&audio, 11) == PJ_ALERT_AUDIO_OK);
    assert(audio.state == PJ_ALERT_AUDIO_PLAYING && audio.frame_cursor == 0);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_BLOCK_WRITTEN);

    assert(pj_alert_audio_transition(&audio, 99, 12, PJ_ALERT_AUDIO_TIMER, 0) ==
           PJ_ALERT_AUDIO_NO_CHANGE);
    assert(audio.alert_id == 11);
    assert(pj_alert_audio_transition(&audio, 11, 12, PJ_ALERT_AUDIO_TIMER, 0) ==
           PJ_ALERT_AUDIO_OK);
    assert(audio.alert_id == 12 && audio.kind == PJ_ALERT_AUDIO_TIMER);
    assert(mock.finish_calls == 1 && audio.cleanup_pending);
    assert(pj_alert_audio_transition(&audio, 11, 12, PJ_ALERT_AUDIO_TIMER, 0) ==
           PJ_ALERT_AUDIO_NO_CHANGE);
    size_t transition_event = mock.event_count;
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_BLOCK_WRITTEN);
    assert(mock.finish_calls == 2 && mock.prepare_calls == 3);
    assert(mock.events[transition_event] == 'F');
    assert(mock.events[transition_event + 1] == 'P');
    assert(mock.events[transition_event + 2] == 'W');
    assert(pj_alert_audio_stop(&audio, 11) == PJ_ALERT_AUDIO_NO_CHANGE);
    assert(pj_alert_audio_stop(&audio, 12) == PJ_ALERT_AUDIO_OK);
    assert(audio.state == PJ_ALERT_AUDIO_IDLE && audio.alert_id == 0);
    assert(mock.finish_calls == 2 && audio.cleanup_pending);
    assert(pj_alert_audio_stop(&audio, 12) == PJ_ALERT_AUDIO_NO_CHANGE);
    size_t stop_event = mock.event_count;
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_NO_CHANGE);
    assert(mock.finish_calls == 3 && !audio.cleanup_pending);
    assert(mock.event_count == stop_event + 1);
    assert(mock.events[stop_event] == 'F');
    assert(pj_alert_audio_transition(&audio, 0, 13, PJ_ALERT_AUDIO_INTERVAL, 1) ==
           PJ_ALERT_AUDIO_OK);
    assert(audio.alert_id == 13 && audio.state == PJ_ALERT_AUDIO_DEFERRED);
}

static void test_recording_deferral_resume_and_stale_ids(void)
{
    pj_alert_audio_t audio;
    mock_audio_t mock;
    int16_t scratch[PJ_ALERT_AUDIO_BLOCK_SAMPLES];
    setup(&audio, &mock, 35);
    assert(pj_alert_audio_set_recording(&audio, 1) == PJ_ALERT_AUDIO_OK);
    assert(pj_alert_audio_set_recording(&audio, 1) == PJ_ALERT_AUDIO_NO_CHANGE);
    assert(pj_alert_audio_present(&audio, 21, PJ_ALERT_AUDIO_TIMER, 0) ==
           PJ_ALERT_AUDIO_OK);
    assert(audio.state == PJ_ALERT_AUDIO_DEFERRED);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_NO_CHANGE);
    assert(mock.prepare_calls == 0);

    pj_alert_audio_set_volume(&audio, 70);
    assert(pj_alert_audio_set_recording(&audio, 0) == PJ_ALERT_AUDIO_OK);
    assert(audio.state == PJ_ALERT_AUDIO_PLAYING && audio.start_volume == 70);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_BLOCK_WRITTEN);
    assert(pj_alert_audio_set_recording(&audio, 1) == PJ_ALERT_AUDIO_OK);
    assert(audio.state == PJ_ALERT_AUDIO_DEFERRED && mock.finish_calls == 0);
    assert(audio.cleanup_pending);
    assert(pj_alert_audio_present(&audio, 22, PJ_ALERT_AUDIO_INTERVAL, 0) ==
           PJ_ALERT_AUDIO_OK);
    assert(audio.alert_id == 22 && audio.state == PJ_ALERT_AUDIO_DEFERRED);
    assert(pj_alert_audio_resume(&audio, 21) == PJ_ALERT_AUDIO_NO_CHANGE);
    assert(pj_alert_audio_stop(&audio, 21) == PJ_ALERT_AUDIO_NO_CHANGE);
    assert(pj_alert_audio_set_recording(&audio, 0) == PJ_ALERT_AUDIO_OK);
    assert(audio.alert_id == 22 && audio.state == PJ_ALERT_AUDIO_PLAYING);
    assert(mock.finish_calls == 0);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_BLOCK_WRITTEN);
    assert(mock.finish_calls == 1 && mock.prepare_calls == 2);
}

static void test_volume_applies_on_next_start_and_zero_is_visual_only(void)
{
    pj_alert_audio_t audio;
    mock_audio_t mock;
    int16_t scratch[PJ_ALERT_AUDIO_BLOCK_SAMPLES];
    setup(&audio, &mock, 25);
    assert(pj_alert_audio_present(&audio, 31, PJ_ALERT_AUDIO_ALARM, 0) ==
           PJ_ALERT_AUDIO_OK);
    assert(audio.start_volume == 25);
    pj_alert_audio_set_volume(&audio, 80);
    assert(audio.start_volume == 25);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_BLOCK_WRITTEN);
    int fixed_peak = block_peak(mock.last_block);
    assert(mock.volume == 25 && mock.prepare_calls == 1);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_BLOCK_WRITTEN);
    assert(mock.volume == 25 && mock.prepare_calls == 1);
    while (audio.frame_cursor != 0) {
        pj_alert_audio_result_t result = pj_alert_audio_pump(&audio, scratch);
        assert(result == PJ_ALERT_AUDIO_BLOCK_WRITTEN ||
               result == PJ_ALERT_AUDIO_SILENT_BLOCK);
    }
    assert(audio.start_volume == 80 && !audio.prepared);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_BLOCK_WRITTEN);
    assert(mock.volume == 80 && mock.prepare_calls == 2);
    assert(block_peak(mock.last_block) == fixed_peak);

    pj_alert_audio_set_volume(&audio, 0);
    assert(pj_alert_audio_present(&audio, 32, PJ_ALERT_AUDIO_TIMER, 0) ==
           PJ_ALERT_AUDIO_OK);
    assert(audio.state == PJ_ALERT_AUDIO_SILENT);
    int prepares = mock.prepare_calls;
    int writes = mock.write_calls;
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_NO_CHANGE);
    assert(mock.prepare_calls == prepares && mock.write_calls == writes);
    pj_alert_audio_set_volume(&audio, 255);
    assert(audio.configured_volume == 100);
    assert(audio.state == PJ_ALERT_AUDIO_PLAYING && audio.start_volume == 100);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_BLOCK_WRITTEN);
    assert(mock.volume == 100);
}

static void test_pattern_silence_finishes_stream(void)
{
    pj_alert_audio_t audio;
    mock_audio_t mock;
    int16_t scratch[PJ_ALERT_AUDIO_BLOCK_SAMPLES];
    setup(&audio, &mock, 50);
    assert(pj_alert_audio_present(&audio, 35, PJ_ALERT_AUDIO_TIMER, 0) ==
           PJ_ALERT_AUDIO_OK);
    for (int block = 0; block < 10; ++block) {
        assert(pj_alert_audio_pump(&audio, scratch) ==
               PJ_ALERT_AUDIO_BLOCK_WRITTEN);
    }
    assert(mock.prepare_calls == 1 && mock.write_calls == 10);
    assert(mock.finish_calls == 0 && audio.prepared);
    assert(pj_alert_audio_pump(&audio, scratch) ==
           PJ_ALERT_AUDIO_SILENT_BLOCK);
    assert(mock.finish_calls == 1 && !audio.prepared);
    assert(mock.write_calls == 10);
    assert(pj_alert_audio_pump(&audio, scratch) ==
           PJ_ALERT_AUDIO_SILENT_BLOCK);
    assert(mock.finish_calls == 1 && mock.prepare_calls == 1);
}

static void test_prepare_write_finish_failures_cleanup_and_retry(void)
{
    pj_alert_audio_t audio;
    mock_audio_t mock;
    int16_t scratch[PJ_ALERT_AUDIO_BLOCK_SAMPLES];
    setup(&audio, &mock, 50);
    assert(pj_alert_audio_present(&audio, 41, PJ_ALERT_AUDIO_TIMER, 0) ==
           PJ_ALERT_AUDIO_OK);
    mock.fail_prepare = 1;
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_ERR_PREPARE);
    assert(mock.prepare_calls == 1 && mock.finish_calls == 1);
    assert(!audio.prepared && !audio.cleanup_pending);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_BLOCK_WRITTEN);

    uint64_t cursor = audio.frame_cursor;
    mock.fail_write = 1;
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_ERR_WRITE);
    assert(!audio.prepared && audio.frame_cursor == cursor);
    assert(mock.finish_calls == 2);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_BLOCK_WRITTEN);
    assert(audio.frame_cursor != cursor);

    mock.fail_finish = 1;
    assert(pj_alert_audio_transition(&audio, 41, 42, PJ_ALERT_AUDIO_INTERVAL, 0) ==
           PJ_ALERT_AUDIO_OK);
    assert(audio.alert_id == 42 && audio.cleanup_pending);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_ERR_FINISH);
    assert(audio.cleanup_pending && !audio.prepared);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_BLOCK_WRITTEN);
    assert(!audio.cleanup_pending && audio.prepared);

    mock.fail_finish = 2;
    assert(pj_alert_audio_stop(&audio, 42) == PJ_ALERT_AUDIO_OK);
    assert(audio.state == PJ_ALERT_AUDIO_IDLE && audio.cleanup_pending);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_ERR_FINISH);
    assert(audio.cleanup_pending);
    assert(pj_alert_audio_present(&audio, 43, PJ_ALERT_AUDIO_ALARM, 0) ==
           PJ_ALERT_AUDIO_OK);
    assert(audio.alert_id == 43 && audio.cleanup_pending);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_ERR_FINISH);
    assert(audio.cleanup_pending);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_BLOCK_WRITTEN);
}

static void test_cancellation_generation_checked_at_block_boundaries(void)
{
    pj_alert_audio_t audio;
    mock_audio_t mock;
    int16_t scratch[PJ_ALERT_AUDIO_BLOCK_SAMPLES];
    setup(&audio, &mock, 50);
    assert(pj_alert_audio_present(&audio, 51, PJ_ALERT_AUDIO_ALARM, 0) ==
           PJ_ALERT_AUDIO_OK);
    mock.cancel_on_prepare = 1;
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_CANCELLED);
    assert(audio.state == PJ_ALERT_AUDIO_IDLE);
    assert(mock.write_calls == 0 && mock.finish_calls == 1);

    assert(pj_alert_audio_present(&audio, 52, PJ_ALERT_AUDIO_TIMER, 0) ==
           PJ_ALERT_AUDIO_OK);
    mock.cancel_on_write = 1;
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_CANCELLED);
    assert(audio.state == PJ_ALERT_AUDIO_IDLE);
    assert(mock.write_calls == 1 && mock.finish_calls == 2);
}

static void test_repeated_operation_stays_bounded(void)
{
    pj_alert_audio_t audio;
    mock_audio_t mock;
    int16_t scratch[PJ_ALERT_AUDIO_BLOCK_SAMPLES];
    setup(&audio, &mock, 100);
    assert(pj_alert_audio_present(&audio, 61, PJ_ALERT_AUDIO_INTERVAL, 0) ==
           PJ_ALERT_AUDIO_OK);
    for (int block = 0; block < 10000; ++block) {
        pj_alert_audio_result_t result = pj_alert_audio_pump(&audio, scratch);
        assert(result == PJ_ALERT_AUDIO_BLOCK_WRITTEN ||
               result == PJ_ALERT_AUDIO_SILENT_BLOCK);
        assert(audio.frame_cursor < PJ_ALERT_AUDIO_PATTERN_FRAMES);
    }
    assert(mock.prepare_calls == 400 && mock.write_calls == 3200);
    assert(pj_alert_audio_stop(&audio, 61) == PJ_ALERT_AUDIO_OK);
    assert(mock.finish_calls == 400);
    assert(pj_alert_audio_pump(&audio, scratch) == PJ_ALERT_AUDIO_NO_CHANGE);

    pj_alert_audio_t invalid;
    pj_alert_audio_init(&invalid, NULL, 50);
    assert(pj_alert_audio_present(&invalid, 1, PJ_ALERT_AUDIO_ALARM, 0) ==
           PJ_ALERT_AUDIO_OK);
    assert(pj_alert_audio_pump(&invalid, scratch) ==
           PJ_ALERT_AUDIO_ERR_ARGUMENT);
}

int main(void)
{
    test_patterns_are_distinct_enveloped_stereo_and_bounded();
    test_exact_id_lifecycle_and_guarded_transition();
    test_recording_deferral_resume_and_stale_ids();
    test_volume_applies_on_next_start_and_zero_is_visual_only();
    test_pattern_silence_finishes_stream();
    test_prepare_write_finish_failures_cleanup_and_retry();
    test_cancellation_generation_checked_at_block_boundaries();
    test_repeated_operation_stays_bounded();
    puts("alert audio tests passed");
    return 0;
}
