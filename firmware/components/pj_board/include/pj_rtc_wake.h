#ifndef PJ_RTC_WAKE_H
#define PJ_RTC_WAKE_H

#include <stddef.h>
#include <stdint.h>

#include "pj_time_clock.h"
#include "pj_time_model.h"

#define PJ_RTC_WAKE_PLAN_VERSION 2u
#define PJ_RTC_WAKE_CONTROL2_AIE 0x80u
#define PJ_RTC_WAKE_CONTROL2_AF 0x40u
#define PJ_RTC_WAKE_CONTROL2_TF 0x08u

typedef enum {
    PJ_RTC_WAKE_NONE = 0,
    PJ_RTC_WAKE_DUE = 1,
    PJ_RTC_WAKE_ARMED = 2,
} pj_rtc_wake_state_t;

typedef struct {
    uint32_t version;
    uint32_t token;
    uint64_t delay_ms;
    uint64_t deadline_fingerprint;
    int32_t target_local_day;
    uint32_t target_local_second;
    uint8_t state;
    uint8_t checkpoint;
    uint8_t source_mask;
    uint8_t alarm[5];
} pj_rtc_wake_plan_t;

typedef int (*pj_rtc_wake_read_fn)(void *context, uint8_t reg,
                                   uint8_t *data, size_t size);
typedef int (*pj_rtc_wake_write_fn)(void *context, uint8_t reg,
                                    const uint8_t *data, size_t size);
typedef int (*pj_rtc_wake_persist_fn)(void *context,
                                      const pj_rtc_wake_plan_t *plan);

typedef struct {
    void *context;
    pj_rtc_wake_read_fn read;
    pj_rtc_wake_write_fn write;
    pj_rtc_wake_persist_fn persist;
} pj_rtc_wake_io_t;

typedef enum {
    PJ_RTC_WAKE_OK = 0,
    PJ_RTC_WAKE_ERR_INVALID = -1,
    PJ_RTC_WAKE_ERR_READ_CONTROL1 = -2,
    PJ_RTC_WAKE_ERR_SANITIZE_CONTROL1 = -3,
    PJ_RTC_WAKE_ERR_READ_CONTROL = -4,
    PJ_RTC_WAKE_ERR_DISABLE_CONTROL = -5,
    PJ_RTC_WAKE_ERR_CLEAR_TIMER = -6,
    PJ_RTC_WAKE_ERR_DISABLE_TIMER = -7,
    PJ_RTC_WAKE_ERR_DISABLE_ALARM = -8,
    PJ_RTC_WAKE_ERR_WRITE_ALARM = -9,
    PJ_RTC_WAKE_ERR_READBACK_ALARM = -10,
    PJ_RTC_WAKE_ERR_PERSIST = -11,
    PJ_RTC_WAKE_ERR_ENABLE_CONTROL = -12,
    PJ_RTC_WAKE_ERR_VERIFY_CONTROL = -13,
} pj_rtc_wake_result_t;

int pj_rtc_wake_plan(const pj_time_state_t *state, const pj_time_clock_t *clock,
                     pj_rtc_wake_plan_t *plan);
int pj_rtc_wake_plan_valid(const pj_rtc_wake_plan_t *plan);
pj_rtc_wake_result_t pj_rtc_wake_program(const pj_rtc_wake_plan_t *plan,
                                         const pj_rtc_wake_io_t *io);
pj_rtc_wake_result_t pj_rtc_wake_disarm(const pj_rtc_wake_io_t *io,
                                        uint8_t *flags);

#endif
