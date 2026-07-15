#ifndef PJ_TIME_CIVIL_H
#define PJ_TIME_CIVIL_H

#include <stdint.h>

#define PJ_TIME_UTC_OFFSET_MINUTES_MIN (-14 * 60)
#define PJ_TIME_UTC_OFFSET_MINUTES_MAX (14 * 60)

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
} pj_time_civil_t;

int pj_time_utc_offset_valid(int offset_minutes);
int pj_time_civil_from_utc(int64_t utc_epoch_s, int offset_minutes,
                           pj_time_civil_t *civil);

#endif
