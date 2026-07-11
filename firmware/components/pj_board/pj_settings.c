#include "pj_settings.h"

#include <stddef.h>

void pj_settings_defaults(pj_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }
    *settings = (pj_settings_t) {
        .volume = 10,
        .dark_mode = 0,
        .alarm_enabled = 0,
        .alarm_hour = 7,
        .alarm_minute = 30,
        .timer_seconds = 300,
        .interval_seconds = 1500,
    };
}

int pj_settings_valid(const pj_settings_t *settings)
{
    return settings != NULL &&
           settings->volume >= 0 && settings->volume <= 10 &&
           (settings->dark_mode == 0 || settings->dark_mode == 1) &&
           (settings->alarm_enabled == 0 || settings->alarm_enabled == 1) &&
           settings->alarm_hour >= 0 && settings->alarm_hour <= 23 &&
           settings->alarm_minute >= 0 && settings->alarm_minute <= 59 &&
           settings->timer_seconds >= 30 && settings->timer_seconds <= 86400 &&
           settings->interval_seconds >= 60 && settings->interval_seconds <= 86400;
}

int pj_settings_codec_volume(int volume)
{
    if (volume <= 0) {
        return 0;
    }
    if (volume >= 10) {
        return 100;
    }
    return volume * 10;
}
