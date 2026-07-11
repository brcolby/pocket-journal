#include "pj_audio_level.h"

#include <limits.h>

static uint32_t abs_sample(int16_t sample)
{
    return sample == INT16_MIN ? 32768U : (uint32_t)(sample < 0 ? -sample : sample);
}

uint32_t pj_audio_normalization_gain_q16(uint32_t peak, uint32_t avg_abs)
{
    if (peak < PJ_AUDIO_NORMALIZE_MIN_PEAK || avg_abs < PJ_AUDIO_NORMALIZE_MIN_AVG ||
        peak >= PJ_AUDIO_NORMALIZE_TARGET_PEAK || avg_abs >= PJ_AUDIO_NORMALIZE_TARGET_AVG) {
        return PJ_AUDIO_GAIN_UNITY_Q16;
    }

    uint64_t peak_gain = ((uint64_t)PJ_AUDIO_NORMALIZE_TARGET_PEAK * PJ_AUDIO_GAIN_UNITY_Q16) / peak;
    uint64_t avg_gain = ((uint64_t)PJ_AUDIO_NORMALIZE_TARGET_AVG * PJ_AUDIO_GAIN_UNITY_Q16) / avg_abs;
    uint64_t gain = peak_gain < avg_gain ? peak_gain : avg_gain;
    if (gain > PJ_AUDIO_NORMALIZE_MAX_GAIN_Q16) {
        gain = PJ_AUDIO_NORMALIZE_MAX_GAIN_Q16;
    }
    return gain < PJ_AUDIO_GAIN_UNITY_Q16 ? PJ_AUDIO_GAIN_UNITY_Q16 : (uint32_t)gain;
}

uint32_t pj_audio_normalization_gain_with_headroom_q16(uint32_t robust_peak,
                                                       uint32_t absolute_peak,
                                                       uint32_t avg_abs)
{
    uint32_t gain = pj_audio_normalization_gain_q16(robust_peak, avg_abs);
    if (absolute_peak == 0) {
        return gain;
    }
    uint64_t headroom_gain = ((uint64_t)PJ_AUDIO_NORMALIZE_TARGET_PEAK *
                              PJ_AUDIO_GAIN_UNITY_Q16) / absolute_peak;
    if (headroom_gain < PJ_AUDIO_GAIN_UNITY_Q16) {
        headroom_gain = PJ_AUDIO_GAIN_UNITY_Q16;
    }
    return gain < headroom_gain ? gain : (uint32_t)headroom_gain;
}

int16_t pj_audio_select_mono(int16_t left, int16_t right)
{
    return abs_sample(left) >= abs_sample(right) ? left : right;
}

int pj_audio_select_channel(uint64_t left_abs_sum, uint64_t right_abs_sum)
{
    return right_abs_sum > left_abs_sum ? 1 : 0;
}

void pj_audio_histogram_add(uint32_t histogram[PJ_AUDIO_LEVEL_HISTOGRAM_BINS], int16_t sample)
{
    uint32_t bin = abs_sample(sample) >> 7;
    if (bin >= PJ_AUDIO_LEVEL_HISTOGRAM_BINS) {
        bin = PJ_AUDIO_LEVEL_HISTOGRAM_BINS - 1U;
    }
    histogram[bin]++;
}

uint32_t pj_audio_histogram_percentile(const uint32_t histogram[PJ_AUDIO_LEVEL_HISTOGRAM_BINS],
                                       uint32_t sample_count, uint32_t permille)
{
    if (sample_count == 0 || permille == 0 || permille > 1000U) {
        return 0;
    }
    uint64_t target = ((uint64_t)sample_count * permille + 999U) / 1000U;
    uint64_t cumulative = 0;
    for (uint32_t i = 0; i < PJ_AUDIO_LEVEL_HISTOGRAM_BINS; i++) {
        cumulative += histogram[i];
        if (cumulative >= target) {
            if (i == PJ_AUDIO_LEVEL_HISTOGRAM_BINS - 1U) {
                return 32768U;
            }
            uint32_t upper_bound = ((i + 1U) << 7) - 1U;
            return upper_bound;
        }
    }
    return 32768U;
}

static int16_t median3(int16_t a, int16_t b, int16_t c)
{
    if (a > b) {
        int16_t swap = a;
        a = b;
        b = swap;
    }
    if (b > c) {
        b = c;
    }
    return a > b ? a : b;
}

void pj_audio_filter_init(pj_audio_filter_state_t *state)
{
    state->previous_original = 0;
    state->dc_previous_input = 0;
    state->dc_previous_output = 0;
    state->lowpass_previous_output = 0;
    state->has_previous = 0;
}

void pj_audio_filter_block(pj_audio_filter_state_t *state, int16_t *samples, size_t count,
                           int16_t next_sample, int has_next, uint32_t sample_offset,
                           uint32_t total_samples)
{
    const int32_t dc_coefficient_q15 = 31785; /* 0.970, about 78 Hz at 16 kHz. */
    const int32_t lowpass_alpha_q15 = 27853; /* 0.85, about 4.8 kHz at 16 kHz. */
    for (size_t i = 0; i < count; i++) {
        int16_t original = samples[i];
        int16_t previous = state->has_previous ? state->previous_original : original;
        int16_t next = i + 1U < count ? samples[i + 1U] : (has_next ? next_sample : original);
        int32_t filtered = median3(previous, original, next);
        state->previous_original = original;
        state->has_previous = 1;

        int32_t dc_blocked = filtered - state->dc_previous_input +
                             (int32_t)(((int64_t)dc_coefficient_q15 * state->dc_previous_output) >> 15);
        state->dc_previous_input = filtered;
        state->dc_previous_output = dc_blocked;

        int32_t lowpass = state->lowpass_previous_output +
                          (int32_t)(((int64_t)lowpass_alpha_q15 *
                                     (dc_blocked - state->lowpass_previous_output)) >> 15);
        state->lowpass_previous_output = lowpass;

        uint32_t index = sample_offset + (uint32_t)i;
        uint32_t fade_q16 = PJ_AUDIO_GAIN_UNITY_Q16;
        if (index < PJ_AUDIO_FILTER_FADE_SAMPLES) {
            fade_q16 = ((index + 1U) * PJ_AUDIO_GAIN_UNITY_Q16) / PJ_AUDIO_FILTER_FADE_SAMPLES;
        }
        uint32_t samples_left = total_samples > index ? total_samples - index : 0;
        if (samples_left <= PJ_AUDIO_FILTER_FADE_SAMPLES) {
            uint32_t fade_out = (samples_left * PJ_AUDIO_GAIN_UNITY_Q16) / PJ_AUDIO_FILTER_FADE_SAMPLES;
            if (fade_out < fade_q16) {
                fade_q16 = fade_out;
            }
        }
        int64_t faded = ((int64_t)lowpass * fade_q16) / PJ_AUDIO_GAIN_UNITY_Q16;
        if (faded > INT16_MAX) {
            faded = INT16_MAX;
        } else if (faded < INT16_MIN) {
            faded = INT16_MIN;
        }
        samples[i] = (int16_t)faded;
    }
}

void pj_audio_apply_gain(int16_t *samples, size_t count, uint32_t gain_q16,
                         uint32_t *peak, uint32_t *avg_abs)
{
    uint32_t result_peak = 0;
    uint64_t abs_sum = 0;
    for (size_t i = 0; i < count; i++) {
        int64_t scaled = ((int64_t)samples[i] * gain_q16) / PJ_AUDIO_GAIN_UNITY_Q16;
        if (scaled > INT16_MAX) {
            scaled = INT16_MAX;
        } else if (scaled < INT16_MIN) {
            scaled = INT16_MIN;
        }
        samples[i] = (int16_t)scaled;
        uint32_t magnitude = abs_sample(samples[i]);
        if (magnitude > result_peak) {
            result_peak = magnitude;
        }
        abs_sum += magnitude;
    }
    if (peak != NULL) {
        *peak = result_peak;
    }
    if (avg_abs != NULL) {
        *avg_abs = count > 0 ? (uint32_t)(abs_sum / count) : 0;
    }
}
