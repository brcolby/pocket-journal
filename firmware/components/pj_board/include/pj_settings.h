#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int volume;
    int dark_mode;
    int alarm_enabled;
    int alarm_hour;
    int alarm_minute;
    int timer_seconds;
    int interval_seconds;
} pj_settings_t;

void pj_settings_defaults(pj_settings_t *settings);
int pj_settings_valid(const pj_settings_t *settings);
int pj_settings_codec_volume(int volume);

#ifdef __cplusplus
}
#endif
