#include "pj_time_civil.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

static void assert_civil(int64_t utc_epoch_s, int offset_minutes,
                         int year, int month, int day,
                         int hour, int minute, int second)
{
    pj_time_civil_t civil;
    assert(pj_time_civil_from_utc(utc_epoch_s, offset_minutes, &civil));
    assert(civil.year == year);
    assert(civil.month == month);
    assert(civil.day == day);
    assert(civil.hour == hour);
    assert(civil.minute == minute);
    assert(civil.second == second);
}

static void test_offset_bounds(void)
{
    assert(pj_time_utc_offset_valid(PJ_TIME_UTC_OFFSET_MINUTES_MIN));
    assert(pj_time_utc_offset_valid(PJ_TIME_UTC_OFFSET_MINUTES_MAX));
    assert(pj_time_utc_offset_valid(0));
    assert(!pj_time_utc_offset_valid(PJ_TIME_UTC_OFFSET_MINUTES_MIN - 1));
    assert(!pj_time_utc_offset_valid(PJ_TIME_UTC_OFFSET_MINUTES_MAX + 1));
}

static void test_day_and_year_crossings(void)
{
    assert_civil(1783989000ll, -7 * 60,
                 2026, 7, 13, 17, 30, 0);
    assert_civil(1735687800ll, 60,
                 2025, 1, 1, 0, 30, 0);
    assert_civil(1735687800ll, -60,
                 2024, 12, 31, 22, 30, 0);
    assert_civil(1704069045ll, -60,
                 2023, 12, 31, 23, 30, 45);
}

static void test_leap_day_and_maximum_offsets(void)
{
    assert_civil(1709250300ll, 0,
                 2024, 2, 29, 23, 45, 0);
    assert_civil(0, PJ_TIME_UTC_OFFSET_MINUTES_MAX,
                 1970, 1, 1, 14, 0, 0);
    assert(!pj_time_civil_from_utc(
        0, PJ_TIME_UTC_OFFSET_MINUTES_MIN, &(pj_time_civil_t){0}));
}

static void test_invalid_inputs_do_not_publish(void)
{
    pj_time_civil_t civil = {2026, 7, 14, 8, 42, 0};
    assert(!pj_time_civil_from_utc(0, 0, NULL));
    assert(!pj_time_civil_from_utc(
        0, PJ_TIME_UTC_OFFSET_MINUTES_MAX + 1, &civil));
    assert(civil.year == 2026 && civil.month == 7 && civil.day == 14);
    assert(!pj_time_civil_from_utc(INT64_MAX,
                                   PJ_TIME_UTC_OFFSET_MINUTES_MAX, &civil));
    assert(!pj_time_civil_from_utc(INT64_MIN,
                                   PJ_TIME_UTC_OFFSET_MINUTES_MIN, &civil));
}

int main(void)
{
    test_offset_bounds();
    test_day_and_year_crossings();
    test_leap_day_and_maximum_offsets();
    test_invalid_inputs_do_not_publish();
    puts("time civil tests passed");
    return 0;
}
