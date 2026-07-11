#include "pj_note_model.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_recording_filename_metadata(void)
{
    pj_note_metadata_t note;
    assert(pj_note_metadata_from_audio(&note, "rec-20260711-0934-007.wav",
                                       160000, 16000, 1, 16));
    assert(strcmp(note.filename, "rec-20260711-0934-007.wav") == 0);
    assert(strcmp(note.created_at, "2026-07-11T09:34:00") == 0);
    assert(note.duration_ms == 5000U);
    assert(note.synced == 0);
    assert(note.transcript_path[0] == '\0');
}

static void test_legacy_filename_is_retained_without_fake_time(void)
{
    pj_note_metadata_t note;
    assert(pj_note_metadata_from_audio(&note, "bringup.wav", 64000, 16000, 2, 16));
    assert(strcmp(note.filename, "bringup.wav") == 0);
    assert(note.created_at[0] == '\0');
    assert(note.duration_ms == 1000U);
}

static void test_invalid_format_is_rejected(void)
{
    pj_note_metadata_t note;
    assert(!pj_note_metadata_from_audio(&note, "rec.wav", 100, 0, 1, 16));
    assert(!pj_note_metadata_from_audio(&note, "rec.wav", 100, 16000, 0, 16));
    assert(!pj_note_metadata_from_audio(&note, "rec.wav", 100, 16000, 1, 12));
}

int main(void)
{
    test_recording_filename_metadata();
    test_legacy_filename_is_retained_without_fake_time();
    test_invalid_format_is_rejected();
    puts("note model tests passed");
    return 0;
}
