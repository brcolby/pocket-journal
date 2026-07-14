#ifndef PJ_ALERT_AUDIO_H
#define PJ_ALERT_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#define PJ_ALERT_AUDIO_SAMPLE_RATE 16000u
#define PJ_ALERT_AUDIO_CHANNELS 2u
#define PJ_ALERT_AUDIO_BLOCK_FRAMES 160u
#define PJ_ALERT_AUDIO_BLOCK_SAMPLES \
    (PJ_ALERT_AUDIO_BLOCK_FRAMES * PJ_ALERT_AUDIO_CHANNELS)
#define PJ_ALERT_AUDIO_PATTERN_FRAMES PJ_ALERT_AUDIO_SAMPLE_RATE
#define PJ_ALERT_AUDIO_DURATION_FRAMES PJ_ALERT_AUDIO_SAMPLE_RATE

typedef enum {
    PJ_ALERT_AUDIO_ALARM = 1,
    PJ_ALERT_AUDIO_TIMER = 2,
    PJ_ALERT_AUDIO_INTERVAL = 3,
} pj_alert_audio_kind_t;

typedef enum {
    PJ_ALERT_AUDIO_IDLE = 0,
    PJ_ALERT_AUDIO_PLAYING = 1,
    PJ_ALERT_AUDIO_DEFERRED = 2,
    PJ_ALERT_AUDIO_SILENT = 3,
    PJ_ALERT_AUDIO_COMPLETE = 4,
} pj_alert_audio_state_t;

typedef enum {
    PJ_ALERT_AUDIO_OK = 0,
    PJ_ALERT_AUDIO_NO_CHANGE = 1,
    PJ_ALERT_AUDIO_BLOCK_WRITTEN = 2,
    PJ_ALERT_AUDIO_SILENT_BLOCK = 3,
    PJ_ALERT_AUDIO_CANCELLED = 4,
    PJ_ALERT_AUDIO_ERR_ARGUMENT = -1,
    PJ_ALERT_AUDIO_ERR_PREPARE = -2,
    PJ_ALERT_AUDIO_ERR_WRITE = -3,
    PJ_ALERT_AUDIO_ERR_FINISH = -4,
} pj_alert_audio_result_t;

typedef struct {
    void *context;
    int (*prepare)(void *context, uint32_t sample_rate, uint8_t channels,
                   uint8_t volume);
    int (*write)(void *context, const int16_t *samples, size_t frames);
    int (*finish)(void *context);
} pj_alert_audio_io_t;

typedef struct {
    pj_alert_audio_io_t io;
    uint64_t alert_id;
    uint64_t frame_cursor;
    uint32_t cancellation_generation;
    pj_alert_audio_kind_t kind;
    pj_alert_audio_state_t state;
    uint8_t configured_volume;
    uint8_t start_volume;
    uint8_t recording;
    uint8_t prepared;
    uint8_t cleanup_pending;
} pj_alert_audio_t;

void pj_alert_audio_init(pj_alert_audio_t *audio, const pj_alert_audio_io_t *io,
                         uint8_t volume);
void pj_alert_audio_set_volume(pj_alert_audio_t *audio, uint8_t volume);

/* Present replaces any active ID. Re-presenting the same ID and kind is a no-op. */
pj_alert_audio_result_t pj_alert_audio_present(pj_alert_audio_t *audio,
                                               uint64_t alert_id,
                                               pj_alert_audio_kind_t kind,
                                               int defer_audio);

/* Transition succeeds only when from_alert_id is still current (zero means idle). */
pj_alert_audio_result_t pj_alert_audio_transition(pj_alert_audio_t *audio,
                                                  uint64_t from_alert_id,
                                                  uint64_t to_alert_id,
                                                  pj_alert_audio_kind_t kind,
                                                  int defer_audio);

/* Exact-ID operations ignore stale events and are idempotent. */
pj_alert_audio_result_t pj_alert_audio_defer(pj_alert_audio_t *audio,
                                             uint64_t alert_id);
pj_alert_audio_result_t pj_alert_audio_resume(pj_alert_audio_t *audio,
                                              uint64_t alert_id);
pj_alert_audio_result_t pj_alert_audio_stop(pj_alert_audio_t *audio,
                                            uint64_t alert_id);

/* Recording defers the current alert and resumes it from the start when released. */
pj_alert_audio_result_t pj_alert_audio_set_recording(pj_alert_audio_t *audio,
                                                     int recording);

/*
 * Pumps at most one fixed 10 ms block. All callbacks happen here; command APIs
 * above only mutate state. Calls must be serialized by the owner.
 */
pj_alert_audio_result_t pj_alert_audio_pump(
    pj_alert_audio_t *audio,
    int16_t scratch[PJ_ALERT_AUDIO_BLOCK_SAMPLES]);

/* Pure deterministic generator used by the controller and host tests. */
int pj_alert_audio_generate_block(
    pj_alert_audio_kind_t kind, uint8_t volume, uint64_t frame_offset,
    int16_t samples[PJ_ALERT_AUDIO_BLOCK_SAMPLES]);

#endif
