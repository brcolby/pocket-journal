#include "pj_time_clock.h"

#include <assert.h>
#include <stdio.h>

static void test_civil_validation(void)
{
    assert(pj_time_clock_civil_valid(2024, 2, 29, 23, 59, 59));
    assert(!pj_time_clock_civil_valid(2025, 2, 29, 0, 0, 0));
    assert(!pj_time_clock_civil_valid(2026, 4, 31, 0, 0, 0));
    assert(!pj_time_clock_civil_valid(2026, 1, 1, 24, 0, 0));
    assert(!pj_time_clock_civil_valid(2026, 1, 1, 0, 60, 0));
}

static int32_t local_day_for(int year, int month, int day)
{
    pj_time_clock_anchor_t anchor = {0};
    assert(pj_time_clock_anchor_set(&anchor, year, month, day, 0, 0, 0, 0));
    return anchor.local_day;
}

static void assert_civil_from_day(int expected_year, int expected_month,
                                  int expected_day)
{
    int year = 0;
    int month = 0;
    int day = 0;
    assert(pj_time_clock_civil_from_day(
        local_day_for(expected_year, expected_month, expected_day),
        &year, &month, &day));
    assert(year == expected_year);
    assert(month == expected_month);
    assert(day == expected_day);
}

static void test_civil_from_day_boundaries(void)
{
    int year = 0;
    int month = 0;
    int day = 0;
    assert(pj_time_clock_civil_from_day(0, &year, &month, &day));
    assert(year == 1970);
    assert(month == 1);
    assert(day == 1);

    assert_civil_from_day(2023, 1, 31);
    assert_civil_from_day(2023, 2, 1);
    assert_civil_from_day(2024, 2, 29);
    assert_civil_from_day(2024, 3, 1);
    assert_civil_from_day(2100, 2, 28);
    assert_civil_from_day(2100, 3, 1);
    assert_civil_from_day(9999, 12, 31);
}

static void test_civil_from_day_rejects_invalid_inputs(void)
{
    int year = 7;
    int month = 8;
    int day = 9;
    assert(!pj_time_clock_civil_from_day(-1, &year, &month, &day));
    assert(year == 7 && month == 8 && day == 9);

    int32_t after_supported_range = local_day_for(9999, 12, 31) + 1;
    assert(!pj_time_clock_civil_from_day(after_supported_range,
                                         &year, &month, &day));
    assert(year == 7 && month == 8 && day == 9);
    assert(!pj_time_clock_civil_from_day(0, NULL, &month, &day));
    assert(!pj_time_clock_civil_from_day(0, &year, NULL, &day));
    assert(!pj_time_clock_civil_from_day(0, &year, &month, NULL));
}

static void test_snapshot_carries_seconds_and_days(void)
{
    pj_time_clock_anchor_t anchor = {0};
    assert(pj_time_clock_anchor_set(&anchor, 2026, 7, 11, 23, 59, 58, 500));

    pj_time_clock_t clock;
    assert(pj_time_clock_snapshot(&anchor, 7, 2999, &clock));
    assert(clock.local_day == anchor.local_day + 1);
    assert(clock.local_second == 0);
    assert(clock.monotonic_ms == 2999);
    assert(clock.wall_utc_ms == anchor.wall_ms + 2499);

    assert(pj_time_clock_snapshot(&anchor, 7, 3500, &clock));
    assert(clock.local_day == anchor.local_day + 1);
    assert(clock.local_second == 1);
}

static void test_civil_day_tracks_calendar_boundaries(void)
{
    pj_time_clock_anchor_t leap_day = {0};
    pj_time_clock_anchor_t march = {0};
    pj_time_clock_anchor_t next_year = {0};
    assert(pj_time_clock_anchor_set(&leap_day, 2024, 2, 29, 0, 0, 0, 0));
    assert(pj_time_clock_anchor_set(&march, 2024, 3, 1, 0, 0, 0, 0));
    assert(pj_time_clock_anchor_set(&next_year, 2025, 1, 1, 0, 0, 0, 0));
    assert(march.local_day == leap_day.local_day + 1);
    assert(next_year.local_day == leap_day.local_day + 307);
}

static void test_snapshot_preserves_subsecond_wall_time(void)
{
    pj_time_clock_anchor_t anchor = {0};
    assert(pj_time_clock_anchor_set(&anchor, 2026, 1, 1, 12, 0, 0, 1000));

    pj_time_clock_t clock;
    assert(pj_time_clock_snapshot(&anchor, 9, 1999, &clock));
    assert(clock.local_second == 12u * 3600u);
    assert(clock.wall_utc_ms == anchor.wall_ms + 999);
    assert(!clock.reboot_elapsed_valid);
}

static void test_invalid_snapshot_inputs(void)
{
    pj_time_clock_anchor_t anchor = {0};
    pj_time_clock_t clock;
    assert(!pj_time_clock_snapshot(&anchor, 1, 0, &clock));
    assert(pj_time_clock_anchor_set(&anchor, 2026, 1, 1, 0, 0, 0, 10));
    assert(!pj_time_clock_snapshot(&anchor, 0, 10, &clock));
    assert(!pj_time_clock_snapshot(&anchor, 1, 9, &clock));
}

int main(void)
{
    test_civil_validation();
    test_civil_from_day_boundaries();
    test_civil_from_day_rejects_invalid_inputs();
    test_snapshot_carries_seconds_and_days();
    test_civil_day_tracks_calendar_boundaries();
    test_snapshot_preserves_subsecond_wall_time();
    test_invalid_snapshot_inputs();
    puts("time clock tests passed");
    return 0;
}
