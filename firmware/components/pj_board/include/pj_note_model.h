#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_NOTE_FILENAME_LEN 96
#define PJ_NOTE_CREATED_AT_LEN 24
#define PJ_NOTE_TRANSCRIPT_PATH_LEN 176

typedef struct {
    char filename[PJ_NOTE_FILENAME_LEN];
    char created_at[PJ_NOTE_CREATED_AT_LEN];
    char transcript_path[PJ_NOTE_TRANSCRIPT_PATH_LEN];
    uint32_t duration_ms;
    int synced;
} pj_note_metadata_t;

int pj_note_metadata_from_audio(pj_note_metadata_t *note, const char *filename,
                                uint32_t data_bytes, uint32_t sample_rate,
                                uint16_t channels, uint16_t bits_per_sample);

#ifdef __cplusplus
}
#endif
