#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_AUDIO_GAIN_UNITY_Q16 65536U
#define PJ_AUDIO_NORMALIZE_TARGET_PEAK 28000U
#define PJ_AUDIO_NORMALIZE_TARGET_AVG 2500U
#define PJ_AUDIO_NORMALIZE_MAX_GAIN_Q16 (8U * PJ_AUDIO_GAIN_UNITY_Q16)
#define PJ_AUDIO_NORMALIZE_MIN_PEAK 128U
#define PJ_AUDIO_NORMALIZE_MIN_AVG 16U
#define PJ_AUDIO_LEVEL_HISTOGRAM_BINS 256U
#define PJ_AUDIO_ROBUST_PEAK_PERMILLE 999U
#define PJ_AUDIO_FILTER_FADE_SAMPLES 160U

typedef struct {
    int16_t previous_original;
    int32_t dc_previous_input;
    int32_t dc_previous_output;
    int32_t lowpass_previous_output;
    int has_previous;
} pj_audio_filter_state_t;

uint32_t pj_audio_normalization_gain_q16(uint32_t peak, uint32_t avg_abs);
uint32_t pj_audio_normalization_gain_with_headroom_q16(uint32_t robust_peak,
                                                       uint32_t absolute_peak,
                                                       uint32_t avg_abs);
int16_t pj_audio_select_mono(int16_t left, int16_t right);
int pj_audio_select_channel(uint64_t left_abs_sum, uint64_t right_abs_sum);
void pj_audio_histogram_add(uint32_t histogram[PJ_AUDIO_LEVEL_HISTOGRAM_BINS], int16_t sample);
uint32_t pj_audio_histogram_percentile(const uint32_t histogram[PJ_AUDIO_LEVEL_HISTOGRAM_BINS],
                                       uint32_t sample_count, uint32_t permille);
void pj_audio_filter_init(pj_audio_filter_state_t *state);
void pj_audio_filter_block(pj_audio_filter_state_t *state, int16_t *samples, size_t count,
                           int16_t next_sample, int has_next, uint32_t sample_offset,
                           uint32_t total_samples);
void pj_audio_apply_gain(int16_t *samples, size_t count, uint32_t gain_q16,
                         uint32_t *peak, uint32_t *avg_abs);

#ifdef __cplusplus
}
#endif
