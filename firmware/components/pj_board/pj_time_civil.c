#include "pj_time_civil.h"

#include <limits.h>
#include <stddef.h>

#define PJ_TIME_SECONDS_PER_DAY 86400ll

static int is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int days_in_month(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30,
                               31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    return month == 2 && is_leap_year(year) ? 29 : days[month - 1];
}

static int64_t days_before_year(int year)
{
    int64_t previous = (int64_t)year - 1;
    return 365 * previous + previous / 4 - previous / 100 + previous / 400;
}

static int64_t day_from_civil(int year, int month, int day)
{
    static const int days_before_month[] = {
        0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334,
    };
    int64_t result = days_before_year(year) - days_before_year(1970);
    result += days_before_month[month];
    if (month > 2 && is_leap_year(year)) {
        result++;
    }
    return result + day - 1;
}

static int civil_from_day(int64_t epoch_day, int *year, int *month, int *day)
{
    if (epoch_day < 0 || epoch_day > day_from_civil(9999, 12, 31)) {
        return 0;
    }

    int first = 1970;
    int last = 9999;
    while (first < last) {
        int candidate = first + (last - first + 1) / 2;
        if (day_from_civil(candidate, 1, 1) <= epoch_day) {
            first = candidate;
        } else {
            last = candidate - 1;
        }
    }

    int result_year = first;
    int day_of_year = (int)(epoch_day - day_from_civil(result_year, 1, 1));
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

int pj_time_utc_offset_valid(int offset_minutes)
{
    return offset_minutes >= PJ_TIME_UTC_OFFSET_MINUTES_MIN &&
           offset_minutes <= PJ_TIME_UTC_OFFSET_MINUTES_MAX;
}

int pj_time_civil_from_utc(int64_t utc_epoch_s, int offset_minutes,
                           pj_time_civil_t *civil)
{
    if (civil == NULL || !pj_time_utc_offset_valid(offset_minutes)) {
        return 0;
    }
    int64_t offset_s = (int64_t)offset_minutes * 60ll;
    if ((offset_s > 0 && utc_epoch_s > INT64_MAX - offset_s) ||
        (offset_s < 0 && utc_epoch_s < INT64_MIN - offset_s)) {
        return 0;
    }
    int64_t local_epoch_s = utc_epoch_s + offset_s;
    int64_t epoch_day = local_epoch_s / PJ_TIME_SECONDS_PER_DAY;
    int64_t second_of_day = local_epoch_s % PJ_TIME_SECONDS_PER_DAY;
    if (second_of_day < 0) {
        second_of_day += PJ_TIME_SECONDS_PER_DAY;
        epoch_day--;
    }

    pj_time_civil_t result = {0};
    if (!civil_from_day(epoch_day, &result.year, &result.month, &result.day)) {
        return 0;
    }
    result.hour = (int)(second_of_day / 3600ll);
    result.minute = (int)((second_of_day % 3600ll) / 60ll);
    result.second = (int)(second_of_day % 60ll);
    *civil = result;
    return 1;
}
