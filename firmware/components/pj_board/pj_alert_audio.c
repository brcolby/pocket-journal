#include "pj_alert_audio.h"

#include <string.h>

#define PJ_ALERT_AUDIO_ATTACK_FRAMES 160u
#define PJ_ALERT_AUDIO_RELEASE_FRAMES 160u
#define PJ_ALERT_AUDIO_PEAK 24000

typedef struct {
    uint32_t start;
    uint32_t length;
    uint32_t frequency;
} pj_alert_tone_t;

static int kind_valid(pj_alert_audio_kind_t kind)
{
    return kind == PJ_ALERT_AUDIO_ALARM || kind == PJ_ALERT_AUDIO_TIMER ||
           kind == PJ_ALERT_AUDIO_INTERVAL;
}

static uint8_t clamp_volume(uint8_t volume)
{
    return volume > 100u ? 100u : volume;
}

static size_t pattern_tones(pj_alert_audio_kind_t kind, pj_alert_tone_t tones[2])
{
    if (kind == PJ_ALERT_AUDIO_ALARM) {
        tones[0] = (pj_alert_tone_t) {.start = 0, .length = 4800, .frequency = 880};
        return 1;
    }
    if (kind == PJ_ALERT_AUDIO_TIMER) {
        tones[0] = (pj_alert_tone_t) {.start = 0, .length = 2400, .frequency = 660};
        tones[1] = (pj_alert_tone_t) {.start = 3200, .length = 2400, .frequency = 880};
        return 2;
    }
    tones[0] = (pj_alert_tone_t) {.start = 0, .length = 1200, .frequency = 1047};
    tones[1] = (pj_alert_tone_t) {.start = 2400, .length = 1200, .frequency = 1047};
    return 2;
}

static uint32_t envelope(uint32_t tone_frame, uint32_t tone_length)
{
    uint32_t attack = tone_frame < PJ_ALERT_AUDIO_ATTACK_FRAMES
                          ? tone_frame
                          : PJ_ALERT_AUDIO_ATTACK_FRAMES;
    uint32_t remaining = tone_length - tone_frame - 1u;
    uint32_t release = remaining < PJ_ALERT_AUDIO_RELEASE_FRAMES
                           ? remaining
                           : PJ_ALERT_AUDIO_RELEASE_FRAMES;
    return attack < release ? attack : release;
}

static int16_t tone_sample(uint64_t absolute_frame, const pj_alert_tone_t *tone,
                           uint32_t tone_frame, uint8_t volume)
{
    uint32_t phase =
        (uint32_t)((absolute_frame % PJ_ALERT_AUDIO_SAMPLE_RATE) *
                   tone->frequency % PJ_ALERT_AUDIO_SAMPLE_RATE);
    int32_t triangle;
    if (phase < PJ_ALERT_AUDIO_SAMPLE_RATE / 2u) {
        triangle = -32767 +
                   (int32_t)((uint64_t)phase * 65534u /
                             (PJ_ALERT_AUDIO_SAMPLE_RATE / 2u));
    } else {
        triangle = 32767 -
                   (int32_t)((uint64_t)(phase - PJ_ALERT_AUDIO_SAMPLE_RATE / 2u) *
                             65534u / (PJ_ALERT_AUDIO_SAMPLE_RATE / 2u));
    }
    int64_t scaled = (int64_t)triangle * PJ_ALERT_AUDIO_PEAK * volume;
    scaled *= envelope(tone_frame, tone->length);
    scaled /= (int64_t)32767 * 100 * PJ_ALERT_AUDIO_ATTACK_FRAMES;
    return (int16_t)scaled;
}

int pj_alert_audio_generate_block(
    pj_alert_audio_kind_t kind, uint8_t volume, uint64_t frame_offset,
    int16_t samples[PJ_ALERT_AUDIO_BLOCK_SAMPLES])
{
    if (!kind_valid(kind) || samples == NULL) {
        return 0;
    }
    volume = clamp_volume(volume);
    pj_alert_tone_t tones[2];
    size_t tone_count = pattern_tones(kind, tones);
    for (size_t frame = 0; frame < PJ_ALERT_AUDIO_BLOCK_FRAMES; ++frame) {
        uint64_t absolute_frame = frame_offset + frame;
        uint32_t pattern_frame = (uint32_t)(absolute_frame %
                                             PJ_ALERT_AUDIO_PATTERN_FRAMES);
        int16_t sample = 0;
        for (size_t tone_index = 0; tone_index < tone_count; ++tone_index) {
            const pj_alert_tone_t *tone = &tones[tone_index];
            if (pattern_frame >= tone->start &&
                pattern_frame < tone->start + tone->length) {
                sample = tone_sample(absolute_frame, tone,
                                     pattern_frame - tone->start, volume);
                break;
            }
        }
        samples[frame * PJ_ALERT_AUDIO_CHANNELS] = sample;
        samples[frame * PJ_ALERT_AUDIO_CHANNELS + 1u] = sample;
    }
    return 1;
}

static pj_alert_audio_result_t finish_stream(pj_alert_audio_t *audio)
{
    if (!audio->prepared && !audio->cleanup_pending) {
        return PJ_ALERT_AUDIO_OK;
    }
    audio->prepared = 0;
    audio->cleanup_pending = 0;
    if (!audio->io.finish(audio->io.context)) {
        audio->cleanup_pending = 1;
        return PJ_ALERT_AUDIO_ERR_FINISH;
    }
    return PJ_ALERT_AUDIO_OK;
}

static void request_cleanup(pj_alert_audio_t *audio)
{
    if (audio->prepared || audio->cleanup_pending) {
        audio->cleanup_pending = 1;
    }
}

static void select_start_state(pj_alert_audio_t *audio, int deferred)
{
    audio->frame_cursor = 0;
    audio->start_volume = audio->configured_volume;
    if (deferred || audio->recording) {
        audio->state = PJ_ALERT_AUDIO_DEFERRED;
    } else if (audio->start_volume == 0) {
        audio->state = PJ_ALERT_AUDIO_SILENT;
    } else {
        audio->state = PJ_ALERT_AUDIO_PLAYING;
    }
}

void pj_alert_audio_init(pj_alert_audio_t *audio, const pj_alert_audio_io_t *io,
                         uint8_t volume)
{
    if (audio == NULL) {
        return;
    }
    memset(audio, 0, sizeof(*audio));
    if (io != NULL) {
        audio->io = *io;
    }
    audio->configured_volume = clamp_volume(volume);
    audio->state = PJ_ALERT_AUDIO_IDLE;
}

void pj_alert_audio_set_volume(pj_alert_audio_t *audio, uint8_t volume)
{
    if (audio != NULL) {
        audio->configured_volume = clamp_volume(volume);
        if (audio->alert_id != 0 && audio->state == PJ_ALERT_AUDIO_SILENT &&
            audio->configured_volume != 0 && !audio->recording) {
            audio->cancellation_generation++;
            select_start_state(audio, 0);
        }
    }
}

int pj_alert_audio_settled(const pj_alert_audio_t *audio, uint64_t *alert_id,
                           pj_alert_audio_kind_t *kind)
{
    if (audio == NULL || alert_id == NULL || kind == NULL || audio->alert_id == 0 ||
        (audio->state != PJ_ALERT_AUDIO_COMPLETE &&
         audio->state != PJ_ALERT_AUDIO_SILENT)) {
        return 0;
    }
    *alert_id = audio->alert_id;
    *kind = audio->kind;
    return 1;
}

pj_alert_audio_result_t pj_alert_audio_present(pj_alert_audio_t *audio,
                                               uint64_t alert_id,
                                               pj_alert_audio_kind_t kind,
                                               int defer_audio)
{
    if (audio == NULL || alert_id == 0 || !kind_valid(kind)) {
        return PJ_ALERT_AUDIO_ERR_ARGUMENT;
    }
    if (audio->alert_id == alert_id) {
        return audio->kind == kind ? PJ_ALERT_AUDIO_NO_CHANGE
                                   : PJ_ALERT_AUDIO_ERR_ARGUMENT;
    }
    audio->cancellation_generation++;
    request_cleanup(audio);
    audio->alert_id = alert_id;
    audio->kind = kind;
    select_start_state(audio, defer_audio);
    return PJ_ALERT_AUDIO_OK;
}

pj_alert_audio_result_t pj_alert_audio_transition(pj_alert_audio_t *audio,
                                                  uint64_t from_alert_id,
                                                  uint64_t to_alert_id,
                                                  pj_alert_audio_kind_t kind,
                                                  int defer_audio)
{
    if (audio == NULL) {
        return PJ_ALERT_AUDIO_ERR_ARGUMENT;
    }
    if (audio->alert_id == to_alert_id && to_alert_id != 0) {
        return audio->kind == kind ? PJ_ALERT_AUDIO_NO_CHANGE
                                   : PJ_ALERT_AUDIO_ERR_ARGUMENT;
    }
    if (audio->alert_id != from_alert_id) {
        return PJ_ALERT_AUDIO_NO_CHANGE;
    }
    return pj_alert_audio_present(audio, to_alert_id, kind, defer_audio);
}

pj_alert_audio_result_t pj_alert_audio_defer(pj_alert_audio_t *audio,
                                             uint64_t alert_id)
{
    if (audio == NULL || alert_id == 0) {
        return PJ_ALERT_AUDIO_ERR_ARGUMENT;
    }
    if (audio->alert_id != alert_id ||
        audio->state == PJ_ALERT_AUDIO_DEFERRED ||
        audio->state == PJ_ALERT_AUDIO_COMPLETE) {
        return PJ_ALERT_AUDIO_NO_CHANGE;
    }
    audio->cancellation_generation++;
    request_cleanup(audio);
    audio->state = PJ_ALERT_AUDIO_DEFERRED;
    audio->frame_cursor = 0;
    return PJ_ALERT_AUDIO_OK;
}

pj_alert_audio_result_t pj_alert_audio_resume(pj_alert_audio_t *audio,
                                              uint64_t alert_id)
{
    if (audio == NULL || alert_id == 0) {
        return PJ_ALERT_AUDIO_ERR_ARGUMENT;
    }
    if (audio->alert_id != alert_id || audio->state != PJ_ALERT_AUDIO_DEFERRED ||
        audio->recording) {
        return PJ_ALERT_AUDIO_NO_CHANGE;
    }
    audio->cancellation_generation++;
    select_start_state(audio, 0);
    return PJ_ALERT_AUDIO_OK;
}

pj_alert_audio_result_t pj_alert_audio_stop(pj_alert_audio_t *audio,
                                            uint64_t alert_id)
{
    if (audio == NULL || alert_id == 0) {
        return PJ_ALERT_AUDIO_ERR_ARGUMENT;
    }
    if (audio->alert_id != alert_id) {
        return PJ_ALERT_AUDIO_NO_CHANGE;
    }
    audio->cancellation_generation++;
    request_cleanup(audio);
    audio->alert_id = 0;
    audio->frame_cursor = 0;
    audio->kind = 0;
    audio->state = PJ_ALERT_AUDIO_IDLE;
    return PJ_ALERT_AUDIO_OK;
}

pj_alert_audio_result_t pj_alert_audio_set_recording(pj_alert_audio_t *audio,
                                                     int recording)
{
    if (audio == NULL) {
        return PJ_ALERT_AUDIO_ERR_ARGUMENT;
    }
    uint8_t value = recording ? 1u : 0u;
    if (audio->recording == value) {
        return PJ_ALERT_AUDIO_NO_CHANGE;
    }
    audio->recording = value;
    if (audio->alert_id == 0 || audio->state == PJ_ALERT_AUDIO_COMPLETE) {
        return PJ_ALERT_AUDIO_OK;
    }
    return value ? pj_alert_audio_defer(audio, audio->alert_id)
                 : pj_alert_audio_resume(audio, audio->alert_id);
}

pj_alert_audio_result_t pj_alert_audio_pump(
    pj_alert_audio_t *audio,
    int16_t scratch[PJ_ALERT_AUDIO_BLOCK_SAMPLES])
{
    if (audio == NULL || scratch == NULL || audio->io.prepare == NULL ||
        audio->io.write == NULL || audio->io.finish == NULL) {
        return PJ_ALERT_AUDIO_ERR_ARGUMENT;
    }
    if (audio->cleanup_pending) {
        pj_alert_audio_result_t cleanup = finish_stream(audio);
        if (cleanup != PJ_ALERT_AUDIO_OK) {
            return cleanup;
        }
    }
    if (audio->state == PJ_ALERT_AUDIO_IDLE ||
        audio->state == PJ_ALERT_AUDIO_DEFERRED ||
        audio->state == PJ_ALERT_AUDIO_SILENT ||
        audio->state == PJ_ALERT_AUDIO_COMPLETE) {
        return PJ_ALERT_AUDIO_NO_CHANGE;
    }

    uint32_t generation = audio->cancellation_generation;
    uint64_t alert_id = audio->alert_id;
    if (!pj_alert_audio_generate_block(audio->kind, 100, audio->frame_cursor,
                                       scratch)) {
        return PJ_ALERT_AUDIO_ERR_ARGUMENT;
    }
    int silent = 1;
    for (size_t sample = 0; sample < PJ_ALERT_AUDIO_BLOCK_SAMPLES; ++sample) {
        if (scratch[sample] != 0) {
            silent = 0;
            break;
        }
    }
    if (!silent && !audio->prepared) {
        if (!audio->io.prepare(audio->io.context, PJ_ALERT_AUDIO_SAMPLE_RATE,
                               PJ_ALERT_AUDIO_CHANNELS, audio->start_volume)) {
            audio->cleanup_pending = 1;
            (void)finish_stream(audio);
            return PJ_ALERT_AUDIO_ERR_PREPARE;
        }
        audio->prepared = 1;
        if (audio->cancellation_generation != generation ||
            audio->alert_id != alert_id ||
            audio->state != PJ_ALERT_AUDIO_PLAYING) {
            pj_alert_audio_result_t result = finish_stream(audio);
            return result == PJ_ALERT_AUDIO_OK ? PJ_ALERT_AUDIO_CANCELLED : result;
        }
    }
    if (audio->cancellation_generation != generation ||
        audio->alert_id != alert_id || audio->state != PJ_ALERT_AUDIO_PLAYING) {
        pj_alert_audio_result_t result = finish_stream(audio);
        return result == PJ_ALERT_AUDIO_OK ? PJ_ALERT_AUDIO_CANCELLED : result;
    }
    if (audio->prepared && !audio->io.write(audio->io.context, scratch,
                                            PJ_ALERT_AUDIO_BLOCK_FRAMES)) {
        audio->cleanup_pending = 1;
        (void)finish_stream(audio);
        return PJ_ALERT_AUDIO_ERR_WRITE;
    }
    if (audio->cancellation_generation != generation ||
        audio->alert_id != alert_id || audio->state != PJ_ALERT_AUDIO_PLAYING) {
        pj_alert_audio_result_t result = finish_stream(audio);
        return result == PJ_ALERT_AUDIO_OK ? PJ_ALERT_AUDIO_CANCELLED : result;
    }
    audio->frame_cursor += PJ_ALERT_AUDIO_BLOCK_FRAMES;
    if (audio->frame_cursor >= PJ_ALERT_AUDIO_DURATION_FRAMES) {
        audio->frame_cursor = PJ_ALERT_AUDIO_DURATION_FRAMES;
        audio->state = PJ_ALERT_AUDIO_COMPLETE;
        pj_alert_audio_result_t result = finish_stream(audio);
        if (result != PJ_ALERT_AUDIO_OK) {
            return result;
        }
    }
    return silent ? PJ_ALERT_AUDIO_SILENT_BLOCK : PJ_ALERT_AUDIO_BLOCK_WRITTEN;
}
