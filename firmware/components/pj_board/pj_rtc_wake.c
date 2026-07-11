#include "pj_rtc_wake.h"

#include <limits.h>
#include <string.h>

#define PCF85063_REG_CONTROL1 0x00u
#define PCF85063_REG_CONTROL2 0x01u
#define PCF85063_REG_ALARM 0x0Bu
#define PCF85063_REG_TIMER_VALUE 0x10u
#define PCF85063_REG_TIMER_MODE 0x11u
#define PCF85063_ALARM_DISABLED 0x80u
#define PCF85063_TIMER_DISABLED 0x18u
#define MS_PER_DAY 86400000ull
#define MS_PER_SECOND 1000ull
#define SECONDS_PER_DAY 86400u

static uint8_t to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10u) << 4u) | value % 10u);
}

static uint32_t plan_token(const pj_rtc_wake_plan_t *plan)
{
    uint32_t hash = 2166136261u;
#define HASH_BYTE(value) do { hash ^= (uint8_t)(value); hash *= 16777619u; } while (0)
    uint32_t local_day = (uint32_t)plan->target_local_day;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        HASH_BYTE(local_day >> shift);
        HASH_BYTE(plan->target_local_second >> shift);
    }
    HASH_BYTE(plan->checkpoint);
    HASH_BYTE(plan->source_mask);
    for (unsigned shift = 0; shift < 64; shift += 8) {
        HASH_BYTE(plan->deadline_fingerprint >> shift);
    }
    for (size_t i = 0; i < sizeof(plan->alarm); i++) {
        HASH_BYTE(plan->alarm[i]);
    }
#undef HASH_BYTE
    return hash == 0 ? 1u : hash;
}

static int next_month_start(int32_t current_day, int32_t *result)
{
    for (int offset = 1; offset <= 32 && current_day <= INT32_MAX - offset; offset++) {
        int year = 0;
        int month = 0;
        int day = 0;
        if (!pj_time_clock_civil_from_day(current_day + offset, &year, &month, &day)) {
            return 0;
        }
        if (day == 1) {
            *result = current_day + offset;
            return 1;
        }
    }
    return 0;
}

static int target_is_representable(const pj_time_clock_t *clock,
                                   int32_t target_day, uint32_t target_second)
{
    int target_year = 0;
    int target_month = 0;
    int target_dom = 0;
    if (!pj_time_clock_civil_from_day(target_day, &target_year, &target_month, &target_dom)) {
        return 0;
    }
    (void)target_year;
    (void)target_month;
    uint64_t current_ms = (uint64_t)clock->local_second * MS_PER_SECOND;
    if (clock->wall_utc_ms >= 0) {
        current_ms += (uint64_t)clock->wall_utc_ms % MS_PER_SECOND;
    }
    for (int offset = 0; offset <= 62 && clock->local_day <= INT32_MAX - offset; offset++) {
        int year = 0;
        int month = 0;
        int day = 0;
        int32_t candidate_day = clock->local_day + offset;
        if (!pj_time_clock_civil_from_day(candidate_day, &year, &month, &day)) {
            return 0;
        }
        if (day != target_dom) {
            continue;
        }
        if (offset == 0 && (uint64_t)target_second * MS_PER_SECOND <= current_ms) {
            continue;
        }
        return candidate_day == target_day;
    }
    return 0;
}

static int fill_alarm(pj_rtc_wake_plan_t *plan)
{
    int year = 0;
    int month = 0;
    int day = 0;
    if (!pj_time_clock_civil_from_day(plan->target_local_day, &year, &month, &day)) {
        return 0;
    }
    (void)year;
    (void)month;
    uint32_t seconds = plan->target_local_second;
    plan->alarm[0] = to_bcd((uint8_t)(seconds % 60u));
    plan->alarm[1] = to_bcd((uint8_t)((seconds / 60u) % 60u));
    plan->alarm[2] = to_bcd((uint8_t)(seconds / 3600u));
    plan->alarm[3] = to_bcd((uint8_t)day);
    plan->alarm[4] = PCF85063_ALARM_DISABLED;
    return 1;
}

int pj_rtc_wake_plan(const pj_time_state_t *state, const pj_time_clock_t *clock,
                     pj_rtc_wake_plan_t *plan)
{
    if (plan == NULL) {
        return 0;
    }
    memset(plan, 0, sizeof(*plan));
    plan->version = PJ_RTC_WAKE_PLAN_VERSION;
    if (!pj_time_state_valid(state) || !pj_time_clock_valid(clock)) {
        return 0;
    }
    pj_time_wake_deadline_t deadline;
    if (!pj_time_next_wake(state, clock, &deadline)) {
        return 0;
    }
    uint64_t delay = deadline.delay_ms;
    if (delay == UINT64_MAX) {
        plan->state = PJ_RTC_WAKE_NONE;
        return 1;
    }
    if (delay == 0) {
        plan->state = PJ_RTC_WAKE_DUE;
        return 1;
    }
    uint64_t fraction = clock->wall_utc_ms >= 0 ?
        (uint64_t)clock->wall_utc_ms % MS_PER_SECOND : 0;
    uint64_t local_ms = (uint64_t)clock->local_second * MS_PER_SECOND + fraction;
    if (delay > UINT64_MAX - local_ms - (MS_PER_SECOND - 1u)) {
        return 0;
    }
    uint64_t target_seconds = (local_ms + delay + MS_PER_SECOND - 1u) / MS_PER_SECOND;
    uint64_t day_offset = target_seconds / SECONDS_PER_DAY;
    if (day_offset > (uint64_t)(INT32_MAX - clock->local_day)) {
        return 0;
    }
    plan->delay_ms = delay;
    plan->deadline_fingerprint = deadline.fingerprint;
    plan->source_mask = deadline.source_mask;
    plan->target_local_day = clock->local_day + (int32_t)day_offset;
    plan->target_local_second = (uint32_t)(target_seconds % SECONDS_PER_DAY);
    plan->state = PJ_RTC_WAKE_ARMED;
    if (!target_is_representable(clock, plan->target_local_day,
                                 plan->target_local_second)) {
        if (!next_month_start(clock->local_day, &plan->target_local_day)) {
            return 0;
        }
        plan->target_local_second = 0;
        plan->checkpoint = 1;
    }
    if (!fill_alarm(plan)) {
        return 0;
    }
    plan->token = plan_token(plan);
    return 1;
}

int pj_rtc_wake_plan_valid(const pj_rtc_wake_plan_t *plan)
{
    if (plan == NULL || plan->version != PJ_RTC_WAKE_PLAN_VERSION ||
        plan->state > PJ_RTC_WAKE_ARMED || plan->checkpoint > 1) {
        return 0;
    }
    if (plan->state != PJ_RTC_WAKE_ARMED) {
        return plan->token == 0;
    }
    return plan->token != 0 && plan->target_local_day >= 0 &&
           plan->target_local_second < SECONDS_PER_DAY && plan->source_mask != 0 &&
           plan->deadline_fingerprint != 0 &&
           plan->token == plan_token(plan);
}

static int io_valid(const pj_rtc_wake_io_t *io)
{
    return io != NULL && io->read != NULL && io->write != NULL && io->persist != NULL;
}

pj_rtc_wake_result_t pj_rtc_wake_disarm(const pj_rtc_wake_io_t *io,
                                        uint8_t *flags)
{
    if (!io_valid(io)) {
        return PJ_RTC_WAKE_ERR_INVALID;
    }
    uint8_t control1 = 0;
    if (!io->read(io->context, PCF85063_REG_CONTROL1, &control1, 1)) {
        return PJ_RTC_WAKE_ERR_READ_CONTROL1;
    }
    control1 &= 0x01u;
    if (!io->write(io->context, PCF85063_REG_CONTROL1, &control1, 1)) {
        return PJ_RTC_WAKE_ERR_SANITIZE_CONTROL1;
    }
    uint8_t control = 0;
    if (!io->read(io->context, PCF85063_REG_CONTROL2, &control, 1)) {
        return PJ_RTC_WAKE_ERR_READ_CONTROL;
    }
    if (flags != NULL) {
        *flags = control & (PJ_RTC_WAKE_CONTROL2_AF | PJ_RTC_WAKE_CONTROL2_TF);
    }
    uint8_t disabled = control & 0x07u;
    if (!io->write(io->context, PCF85063_REG_CONTROL2, &disabled, 1)) {
        return PJ_RTC_WAKE_ERR_DISABLE_CONTROL;
    }
    uint8_t timer_value = 0;
    if (!io->write(io->context, PCF85063_REG_TIMER_VALUE, &timer_value, 1)) {
        return PJ_RTC_WAKE_ERR_CLEAR_TIMER;
    }
    uint8_t timer = PCF85063_TIMER_DISABLED;
    if (!io->write(io->context, PCF85063_REG_TIMER_MODE, &timer, 1)) {
        return PJ_RTC_WAKE_ERR_DISABLE_TIMER;
    }
    const uint8_t disabled_alarm[5] = {
        PCF85063_ALARM_DISABLED, PCF85063_ALARM_DISABLED,
        PCF85063_ALARM_DISABLED, PCF85063_ALARM_DISABLED,
        PCF85063_ALARM_DISABLED,
    };
    if (!io->write(io->context, PCF85063_REG_ALARM, disabled_alarm,
                   sizeof(disabled_alarm))) {
        return PJ_RTC_WAKE_ERR_DISABLE_ALARM;
    }
    pj_rtc_wake_plan_t empty = {.version = PJ_RTC_WAKE_PLAN_VERSION};
    if (!io->persist(io->context, &empty)) {
        return PJ_RTC_WAKE_ERR_PERSIST;
    }
    return PJ_RTC_WAKE_OK;
}

pj_rtc_wake_result_t pj_rtc_wake_program(const pj_rtc_wake_plan_t *plan,
                                         const pj_rtc_wake_io_t *io)
{
    if (!io_valid(io) || !pj_rtc_wake_plan_valid(plan) ||
        plan->state != PJ_RTC_WAKE_ARMED) {
        return PJ_RTC_WAKE_ERR_INVALID;
    }
    uint8_t control1 = 0;
    if (!io->read(io->context, PCF85063_REG_CONTROL1, &control1, 1)) {
        return PJ_RTC_WAKE_ERR_READ_CONTROL1;
    }
    control1 &= 0x01u;
    if (!io->write(io->context, PCF85063_REG_CONTROL1, &control1, 1)) {
        return PJ_RTC_WAKE_ERR_SANITIZE_CONTROL1;
    }
    uint8_t control = 0;
    if (!io->read(io->context, PCF85063_REG_CONTROL2, &control, 1)) {
        return PJ_RTC_WAKE_ERR_READ_CONTROL;
    }
    uint8_t disabled = control & 0x07u;
    if (!io->write(io->context, PCF85063_REG_CONTROL2, &disabled, 1)) {
        return PJ_RTC_WAKE_ERR_DISABLE_CONTROL;
    }
    uint8_t timer_value = 0;
    if (!io->write(io->context, PCF85063_REG_TIMER_VALUE, &timer_value, 1)) {
        return PJ_RTC_WAKE_ERR_CLEAR_TIMER;
    }
    uint8_t timer = PCF85063_TIMER_DISABLED;
    if (!io->write(io->context, PCF85063_REG_TIMER_MODE, &timer, 1)) {
        return PJ_RTC_WAKE_ERR_DISABLE_TIMER;
    }
    const uint8_t disabled_alarm[5] = {
        PCF85063_ALARM_DISABLED, PCF85063_ALARM_DISABLED,
        PCF85063_ALARM_DISABLED, PCF85063_ALARM_DISABLED,
        PCF85063_ALARM_DISABLED,
    };
    if (!io->write(io->context, PCF85063_REG_ALARM, disabled_alarm,
                   sizeof(disabled_alarm))) {
        return PJ_RTC_WAKE_ERR_DISABLE_ALARM;
    }
    if (!io->write(io->context, PCF85063_REG_ALARM, plan->alarm,
                   sizeof(plan->alarm))) {
        return PJ_RTC_WAKE_ERR_WRITE_ALARM;
    }
    uint8_t alarm[sizeof(plan->alarm)] = {0};
    if (!io->read(io->context, PCF85063_REG_ALARM, alarm, sizeof(alarm)) ||
        memcmp(alarm, plan->alarm, sizeof(alarm)) != 0) {
        return PJ_RTC_WAKE_ERR_READBACK_ALARM;
    }
    if (!io->persist(io->context, plan)) {
        return PJ_RTC_WAKE_ERR_PERSIST;
    }
    uint8_t enabled = disabled | PJ_RTC_WAKE_CONTROL2_AIE;
    if (!io->write(io->context, PCF85063_REG_CONTROL2, &enabled, 1)) {
        return PJ_RTC_WAKE_ERR_ENABLE_CONTROL;
    }
    uint8_t verified = 0;
    if (!io->read(io->context, PCF85063_REG_CONTROL2, &verified, 1) ||
        (verified & (PJ_RTC_WAKE_CONTROL2_AIE | PJ_RTC_WAKE_CONTROL2_AF |
                     PJ_RTC_WAKE_CONTROL2_TF)) != PJ_RTC_WAKE_CONTROL2_AIE) {
        (void)io->write(io->context, PCF85063_REG_CONTROL2, &disabled, 1);
        return PJ_RTC_WAKE_ERR_VERIFY_CONTROL;
    }
    return PJ_RTC_WAKE_OK;
}
