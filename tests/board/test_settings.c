#include "pj_settings.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    pj_settings_t settings;
    pj_settings_defaults(&settings);
    assert(pj_settings_valid(&settings));
    assert(settings.volume == 10);
    assert(settings.dark_mode == 0);
    assert(settings.alarm_hour == 7);
    assert(settings.alarm_minute == 30);
    assert(settings.timer_seconds == 300);
    assert(settings.interval_seconds == 1500);

    assert(pj_settings_codec_volume(-1) == 0);
    assert(pj_settings_codec_volume(0) == 0);
    assert(pj_settings_codec_volume(1) == 10);
    assert(pj_settings_codec_volume(5) == 50);
    assert(pj_settings_codec_volume(10) == 100);
    assert(pj_settings_codec_volume(11) == 100);

    settings.volume = 11;
    assert(!pj_settings_valid(&settings));
    pj_settings_defaults(&settings);
    settings.alarm_hour = 24;
    assert(!pj_settings_valid(&settings));
    pj_settings_defaults(&settings);
    settings.timer_seconds = 29;
    assert(!pj_settings_valid(&settings));
    pj_settings_defaults(&settings);
    settings.interval_seconds = 86401;
    assert(!pj_settings_valid(&settings));

    puts("settings tests passed");
    return 0;
}
