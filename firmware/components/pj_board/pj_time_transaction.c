#include "pj_time_transaction.h"

#include <stddef.h>

static pj_time_transaction_result_t transaction_result(
    pj_time_transaction_status_t status, uint32_t partial_components)
{
    return (pj_time_transaction_result_t) {
        .status = status,
        .partial_components = partial_components,
    };
}

pj_time_transaction_result_t pj_time_transaction_execute(
    const pj_time_transaction_request_t *request,
    const pj_time_transaction_ops_t *ops)
{
    if (request == NULL || ops == NULL ||
        (request->update_offset != 0 && request->update_offset != 1) ||
        (request->old_offset_known != 0 && request->old_offset_known != 1) ||
        ops->snapshot_rtc == NULL || ops->write_rtc == NULL ||
        ops->restore_rtc == NULL || ops->publish == NULL ||
        (request->update_offset && ops->store_offset == NULL)) {
        return transaction_result(PJ_TIME_TRANSACTION_INVALID, 0);
    }

    if (!ops->snapshot_rtc(ops->context)) {
        return transaction_result(PJ_TIME_TRANSACTION_RTC_SNAPSHOT_FAILED, 0);
    }

    if (request->update_offset &&
        !ops->store_offset(ops->context, 1, request->new_offset_minutes)) {
        if (!ops->store_offset(ops->context, request->old_offset_known,
                               request->old_offset_minutes)) {
            return transaction_result(PJ_TIME_TRANSACTION_PARTIAL_COMMIT,
                                      PJ_TIME_TRANSACTION_COMPONENT_UTC_OFFSET);
        }
        return transaction_result(
            PJ_TIME_TRANSACTION_OFFSET_FAILED_ROLLED_BACK, 0);
    }

    if (!ops->write_rtc(ops->context)) {
        uint32_t partial_components = 0;
        if (!ops->restore_rtc(ops->context)) {
            partial_components |= PJ_TIME_TRANSACTION_COMPONENT_RTC;
        }
        if (request->update_offset &&
            !ops->store_offset(ops->context, request->old_offset_known,
                               request->old_offset_minutes)) {
            partial_components |= PJ_TIME_TRANSACTION_COMPONENT_UTC_OFFSET;
        }
        if (partial_components != 0) {
            return transaction_result(PJ_TIME_TRANSACTION_PARTIAL_COMMIT,
                                      partial_components);
        }
        return transaction_result(PJ_TIME_TRANSACTION_RTC_FAILED_ROLLED_BACK,
                                  0);
    }

    ops->publish(ops->context);
    return transaction_result(PJ_TIME_TRANSACTION_OK, 0);
}

const char *pj_time_transaction_status_name(
    pj_time_transaction_status_t status)
{
    switch (status) {
    case PJ_TIME_TRANSACTION_OK: return "ok";
    case PJ_TIME_TRANSACTION_INVALID: return "invalid";
    case PJ_TIME_TRANSACTION_RTC_SNAPSHOT_FAILED: return "rtc_snapshot_failed";
    case PJ_TIME_TRANSACTION_OFFSET_FAILED_ROLLED_BACK:
        return "offset_failed_rolled_back";
    case PJ_TIME_TRANSACTION_RTC_FAILED_ROLLED_BACK:
        return "rtc_failed_rolled_back";
    case PJ_TIME_TRANSACTION_PARTIAL_COMMIT: return "partial_commit";
    default: return "unknown";
    }
}
