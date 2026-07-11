#include "pj_note_model.h"

#include <stdio.h>
#include <string.h>

static void created_at_from_filename(char *out, size_t out_size, const char *filename)
{
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int sequence = 0;
    int matched = sscanf(filename, "rec-%4d%2d%2d-%2d%2d-%3d.wav",
                         &year, &month, &day, &hour, &minute, &sequence);
    (void)sequence;
    if (matched != 6 || year < 2000 || year > 2099 || month < 1 || month > 12 ||
        day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        out[0] = '\0';
        return;
    }
    (void)snprintf(out, out_size, "%04d-%02d-%02dT%02d:%02d:00",
                   year, month, day, hour, minute);
}

int pj_note_metadata_from_audio(pj_note_metadata_t *note, const char *filename,
                                uint32_t data_bytes, uint32_t sample_rate,
                                uint16_t channels, uint16_t bits_per_sample)
{
    if (note == NULL || filename == NULL || filename[0] == '\0' ||
        sample_rate == 0 || channels == 0 || bits_per_sample == 0 ||
        (bits_per_sample % 8U) != 0) {
        return 0;
    }
    uint32_t bytes_per_frame = channels * (bits_per_sample / 8U);
    if (bytes_per_frame == 0) {
        return 0;
    }

    memset(note, 0, sizeof(*note));
    (void)snprintf(note->filename, sizeof(note->filename), "%s", filename);
    created_at_from_filename(note->created_at, sizeof(note->created_at), filename);
    uint64_t frames = data_bytes / bytes_per_frame;
    note->duration_ms = (uint32_t)((frames * 1000U) / sample_rate);
    return 1;
}
