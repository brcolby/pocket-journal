#include "pj_time_clock.h"

#include <limits.h>
#include <stddef.h>

#define PJ_TIME_SECONDS_PER_DAY 86400ull
#define PJ_TIME_MS_PER_SECOND 1000ull

static int is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int days_in_month(int year, int month)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30,
                                   31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days[month - 1];
}

static int64_t days_before_year(int year)
{
    int64_t previous = (int64_t)year - 1;
    return 365 * previous + previous / 4 - previous / 100 + previous / 400;
}

static int32_t civil_day(int year, int month, int day)
{
    static const uint16_t days_before_month[] = {
        0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334,
    };
    int64_t result = days_before_year(year) - days_before_year(1970);
    result += days_before_month[month];
    if (month > 2 && is_leap_year(year)) {
        result++;
    }
    result += day - 1;
    return (int32_t)result;
}

int pj_time_clock_civil_valid(int year, int month, int day,
                              int hour, int minute, int second)
{
    return year >= 1970 && year <= 9999 && month >= 1 && month <= 12 &&
           day >= 1 && day <= days_in_month(year, month) &&
           hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 &&
           second >= 0 && second <= 59;
}

int pj_time_clock_civil_from_day(int32_t local_day,
                                 int *year, int *month, int *day)
{
    if (local_day < 0 || local_day > civil_day(9999, 12, 31) ||
        year == NULL || month == NULL || day == NULL) {
        return 0;
    }

    int first = 1970;
    int last = 9999;
    while (first < last) {
        int candidate = first + (last - first + 1) / 2;
        if (civil_day(candidate, 1, 1) <= local_day) {
            first = candidate;
        } else {
            last = candidate - 1;
        }
    }

    int result_year = first;
    int day_of_year = local_day - civil_day(result_year, 1, 1);
    int result_month = 1;
    while (day_of_year >= days_in_month(result_year, result_month)) {
        day_of_year -= days_in_month(result_year, result_month);
        result_month++;
    }

    *year = result_year;
    *month = result_month;
    *day = day_of_year + 1;
    return 1;
}

int pj_time_clock_anchor_set(pj_time_clock_anchor_t *anchor,
                             int year, int month, int day,
                             int hour, int minute, int second,
                             uint64_t monotonic_ms)
{
    if (anchor == NULL ||
        !pj_time_clock_civil_valid(year, month, day, hour, minute, second)) {
        return 0;
    }
    int32_t local_day = civil_day(year, month, day);
    uint32_t local_second = (uint32_t)(hour * 3600 + minute * 60 + second);
    int64_t wall_seconds = (int64_t)local_day * (int64_t)PJ_TIME_SECONDS_PER_DAY +
                           (int64_t)local_second;
    if (wall_seconds > INT64_MAX / (int64_t)PJ_TIME_MS_PER_SECOND) {
        return 0;
    }
    anchor->local_day = local_day;
    anchor->local_second = local_second;
    anchor->wall_ms = wall_seconds * (int64_t)PJ_TIME_MS_PER_SECOND;
    anchor->monotonic_ms = monotonic_ms;
    anchor->valid = 1;
    return 1;
}

int pj_time_clock_snapshot(const pj_time_clock_anchor_t *anchor,
                           uint32_t boot_id, uint64_t monotonic_ms,
                           pj_time_clock_t *clock)
{
    if (anchor == NULL || clock == NULL || !anchor->valid || boot_id == 0 ||
        monotonic_ms < anchor->monotonic_ms) {
        return 0;
    }
    uint64_t elapsed_ms = monotonic_ms - anchor->monotonic_ms;
    uint64_t elapsed_seconds = elapsed_ms / PJ_TIME_MS_PER_SECOND;
    if (elapsed_seconds > UINT64_MAX - anchor->local_second ||
        elapsed_ms > (uint64_t)(INT64_MAX - anchor->wall_ms)) {
        return 0;
    }
    uint64_t civil_seconds = (uint64_t)anchor->local_second + elapsed_seconds;
    uint64_t day_offset = civil_seconds / PJ_TIME_SECONDS_PER_DAY;
    if (day_offset > (uint64_t)(INT32_MAX - anchor->local_day)) {
        return 0;
    }
    *clock = (pj_time_clock_t) {
        .boot_id = boot_id,
        .monotonic_ms = monotonic_ms,
        /* The product has no timezone setting yet; civil epoch time is still a
         * stable wall-time delta for reboot recovery. */
        .wall_utc_ms = anchor->wall_ms + (int64_t)elapsed_ms,
        .local_day = anchor->local_day + (int32_t)day_offset,
        .local_second = (uint32_t)(civil_seconds % PJ_TIME_SECONDS_PER_DAY),
    };
    return pj_time_clock_valid(clock);
}
