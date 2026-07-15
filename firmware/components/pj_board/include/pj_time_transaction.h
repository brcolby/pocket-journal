#ifndef PJ_TIME_TRANSACTION_H
#define PJ_TIME_TRANSACTION_H

#include <stdint.h>

typedef enum {
    PJ_TIME_TRANSACTION_OK = 0,
    PJ_TIME_TRANSACTION_INVALID,
    PJ_TIME_TRANSACTION_RTC_SNAPSHOT_FAILED,
    PJ_TIME_TRANSACTION_OFFSET_FAILED_ROLLED_BACK,
    PJ_TIME_TRANSACTION_RTC_FAILED_ROLLED_BACK,
    PJ_TIME_TRANSACTION_PARTIAL_COMMIT,
} pj_time_transaction_status_t;

enum {
    PJ_TIME_TRANSACTION_COMPONENT_RTC = 1u << 0,
    PJ_TIME_TRANSACTION_COMPONENT_UTC_OFFSET = 1u << 1,
};

typedef struct {
    int update_offset;
    int old_offset_known;
    int old_offset_minutes;
    int new_offset_minutes;
} pj_time_transaction_request_t;

typedef struct {
    void *context;
    int (*snapshot_rtc)(void *context);
    int (*store_offset)(void *context, int known, int offset_minutes);
    int (*write_rtc)(void *context);
    int (*restore_rtc)(void *context);
    void (*publish)(void *context);
} pj_time_transaction_ops_t;

typedef struct {
    pj_time_transaction_status_t status;
    uint32_t partial_components;
} pj_time_transaction_result_t;

pj_time_transaction_result_t pj_time_transaction_execute(
    const pj_time_transaction_request_t *request,
    const pj_time_transaction_ops_t *ops);

const char *pj_time_transaction_status_name(
    pj_time_transaction_status_t status);

#endif
