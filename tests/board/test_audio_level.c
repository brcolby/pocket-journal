#include "pj_audio_level.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_gain_uses_peak_headroom(void)
{
    uint32_t gain = pj_audio_normalization_gain_q16(5000, 250);
    assert(gain > 5U * PJ_AUDIO_GAIN_UNITY_Q16);
    assert(gain < 6U * PJ_AUDIO_GAIN_UNITY_Q16);
}

static void test_gain_uses_average_target(void)
{
    uint32_t gain = pj_audio_normalization_gain_q16(1000, 1000);
    assert(gain == (PJ_AUDIO_NORMALIZE_TARGET_AVG * PJ_AUDIO_GAIN_UNITY_Q16) / 1000U);
}

static void test_gain_is_capped(void)
{
    assert(pj_audio_normalization_gain_q16(200, 20) == PJ_AUDIO_NORMALIZE_MAX_GAIN_Q16);
}

static void test_absolute_peak_prevents_normalization_clipping(void)
{
    uint32_t gain = pj_audio_normalization_gain_with_headroom_q16(13951, 28138, 600);
    assert(gain == PJ_AUDIO_GAIN_UNITY_Q16);

    gain = pj_audio_normalization_gain_with_headroom_q16(5000, 10000, 500);
    assert(gain > 2U * PJ_AUDIO_GAIN_UNITY_Q16);
    assert(gain < 3U * PJ_AUDIO_GAIN_UNITY_Q16);
}

static void test_silence_and_loud_audio_are_not_boosted(void)
{
    assert(pj_audio_normalization_gain_q16(100, 10) == PJ_AUDIO_GAIN_UNITY_Q16);
    assert(pj_audio_normalization_gain_q16(PJ_AUDIO_NORMALIZE_TARGET_PEAK, 100) == PJ_AUDIO_GAIN_UNITY_Q16);
    assert(pj_audio_normalization_gain_q16(1000, PJ_AUDIO_NORMALIZE_TARGET_AVG) == PJ_AUDIO_GAIN_UNITY_Q16);
}

static void test_apply_gain_reports_output_and_saturates(void)
{
    int16_t samples[] = {1000, -2000, 20000, -20000};
    uint32_t peak = 0;
    uint32_t avg_abs = 0;
    pj_audio_apply_gain(samples, 4, 2U * PJ_AUDIO_GAIN_UNITY_Q16, &peak, &avg_abs);
    assert(samples[0] == 2000);
    assert(samples[1] == -4000);
    assert(samples[2] == INT16_MAX);
    assert(samples[3] == INT16_MIN);
    assert(peak == 32768U);
    assert(avg_abs == (2000U + 4000U + 32767U + 32768U) / 4U);
}

static void test_mono_selection_preserves_the_active_codec_slot(void)
{
    assert(pj_audio_select_mono(1200, 0) == 1200);
    assert(pj_audio_select_mono(0, -1800) == -1800);
    assert(pj_audio_select_mono(700, 700) == 700);
    assert(pj_audio_select_mono(-900, 800) == -900);
    assert(pj_audio_select_channel(1000, 900) == 0);
    assert(pj_audio_select_channel(900, 1000) == 1);
    assert(pj_audio_select_channel(1000, 1000) == 0);
}

static void test_robust_peak_ignores_sparse_full_scale_glitches(void)
{
    uint32_t histogram[PJ_AUDIO_LEVEL_HISTOGRAM_BINS] = {0};
    for (int i = 0; i < 9990; i++) {
        pj_audio_histogram_add(histogram, 2000);
    }
    for (int i = 0; i < 10; i++) {
        pj_audio_histogram_add(histogram, INT16_MIN);
    }
    uint32_t robust_peak = pj_audio_histogram_percentile(
        histogram, 10000, PJ_AUDIO_ROBUST_PEAK_PERMILLE);
    assert(robust_peak >= 2000U);
    assert(robust_peak < 2200U);
    assert(pj_audio_histogram_percentile(histogram, 10000, 1000) == 32768U);
}

static void test_filter_removes_isolated_click_and_fades_edges(void)
{
    int16_t samples[400] = {0};
    for (size_t i = 0; i < 400; i++) {
        samples[i] = 1000;
    }
    samples[200] = INT16_MAX;
    pj_audio_filter_state_t state;
    pj_audio_filter_init(&state);
    pj_audio_filter_block(&state, samples, 400, 0, 0, 0, 400);

    assert(samples[0] < 20);
    assert(samples[200] < 1000);
    assert(samples[399] < 20);
}

int main(void)
{
    test_gain_uses_peak_headroom();
    test_gain_uses_average_target();
    test_gain_is_capped();
    test_absolute_peak_prevents_normalization_clipping();
    test_silence_and_loud_audio_are_not_boosted();
    test_apply_gain_reports_output_and_saturates();
    test_mono_selection_preserves_the_active_codec_slot();
    test_robust_peak_ignores_sparse_full_scale_glitches();
    test_filter_removes_isolated_click_and_fades_edges();
    puts("audio level tests passed");
    return 0;
}
