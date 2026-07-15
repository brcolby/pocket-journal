#include "pj_time_transaction.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int snapshot_ok;
    int new_offset_ok;
    int old_offset_ok;
    int write_rtc_ok;
    int restore_rtc_ok;
    int old_offset_known;
    int old_offset_minutes;
    int new_offset_minutes;
    unsigned store_calls;
    unsigned publish_calls;
    char operations[16];
    size_t operation_count;
} fixture_t;

static void append_operation(fixture_t *fixture, char operation)
{
    assert(fixture->operation_count + 1 < sizeof(fixture->operations));
    fixture->operations[fixture->operation_count++] = operation;
    fixture->operations[fixture->operation_count] = '\0';
}

static fixture_t fixture_defaults(void)
{
    return (fixture_t) {
        .snapshot_ok = 1,
        .new_offset_ok = 1,
        .old_offset_ok = 1,
        .write_rtc_ok = 1,
        .restore_rtc_ok = 1,
        .old_offset_known = 1,
        .old_offset_minutes = -420,
        .new_offset_minutes = -360,
    };
}

static int snapshot_rtc(void *context)
{
    fixture_t *fixture = context;
    append_operation(fixture, 'S');
    return fixture->snapshot_ok;
}

static int store_offset(void *context, int known, int offset_minutes)
{
    fixture_t *fixture = context;
    if (fixture->store_calls++ == 0) {
        append_operation(fixture, 'O');
        assert(known == 1);
        assert(offset_minutes == fixture->new_offset_minutes);
        return fixture->new_offset_ok;
    }
    append_operation(fixture, known ? 'K' : 'E');
    assert(known == fixture->old_offset_known);
    if (known) {
        assert(offset_minutes == fixture->old_offset_minutes);
    }
    return fixture->old_offset_ok;
}

static int write_rtc(void *context)
{
    fixture_t *fixture = context;
    append_operation(fixture, 'W');
    return fixture->write_rtc_ok;
}

static int restore_rtc(void *context)
{
    fixture_t *fixture = context;
    append_operation(fixture, 'R');
    return fixture->restore_rtc_ok;
}

static void publish(void *context)
{
    fixture_t *fixture = context;
    append_operation(fixture, 'P');
    fixture->publish_calls++;
}

static pj_time_transaction_result_t execute(fixture_t *fixture,
                                            int update_offset)
{
    pj_time_transaction_request_t request = {
        .update_offset = update_offset,
        .old_offset_known = fixture->old_offset_known,
        .old_offset_minutes = fixture->old_offset_minutes,
        .new_offset_minutes = fixture->new_offset_minutes,
    };
    pj_time_transaction_ops_t ops = {
        .context = fixture,
        .snapshot_rtc = snapshot_rtc,
        .store_offset = store_offset,
        .write_rtc = write_rtc,
        .restore_rtc = restore_rtc,
        .publish = publish,
    };
    return pj_time_transaction_execute(&request, &ops);
}

static void test_success_orders_publication_last(void)
{
    fixture_t without_offset = fixture_defaults();
    pj_time_transaction_result_t result = execute(&without_offset, 0);
    assert(result.status == PJ_TIME_TRANSACTION_OK);
    assert(result.partial_components == 0);
    assert(strcmp(without_offset.operations, "SWP") == 0);
    assert(without_offset.publish_calls == 1);

    fixture_t with_offset = fixture_defaults();
    result = execute(&with_offset, 1);
    assert(result.status == PJ_TIME_TRANSACTION_OK);
    assert(result.partial_components == 0);
    assert(strcmp(with_offset.operations, "SOWP") == 0);
    assert(with_offset.publish_calls == 1);
}

static void test_snapshot_failure_has_no_side_effects(void)
{
    fixture_t fixture = fixture_defaults();
    fixture.snapshot_ok = 0;
    pj_time_transaction_result_t result = execute(&fixture, 1);
    assert(result.status == PJ_TIME_TRANSACTION_RTC_SNAPSHOT_FAILED);
    assert(result.partial_components == 0);
    assert(strcmp(fixture.operations, "S") == 0);
    assert(fixture.publish_calls == 0);
}

static void test_offset_failure_restores_known_and_unknown_state(void)
{
    fixture_t known = fixture_defaults();
    known.new_offset_ok = 0;
    pj_time_transaction_result_t result = execute(&known, 1);
    assert(result.status == PJ_TIME_TRANSACTION_OFFSET_FAILED_ROLLED_BACK);
    assert(result.partial_components == 0);
    assert(strcmp(known.operations, "SOK") == 0);
    assert(known.publish_calls == 0);

    fixture_t unknown = fixture_defaults();
    unknown.old_offset_known = 0;
    unknown.new_offset_ok = 0;
    result = execute(&unknown, 1);
    assert(result.status == PJ_TIME_TRANSACTION_OFFSET_FAILED_ROLLED_BACK);
    assert(result.partial_components == 0);
    assert(strcmp(unknown.operations, "SOE") == 0);
    assert(unknown.publish_calls == 0);
}

static void test_offset_failure_reports_failed_rollback(void)
{
    fixture_t fixture = fixture_defaults();
    fixture.new_offset_ok = 0;
    fixture.old_offset_ok = 0;
    pj_time_transaction_result_t result = execute(&fixture, 1);
    assert(result.status == PJ_TIME_TRANSACTION_PARTIAL_COMMIT);
    assert(result.partial_components ==
           PJ_TIME_TRANSACTION_COMPONENT_UTC_OFFSET);
    assert(strcmp(fixture.operations, "SOK") == 0);
    assert(fixture.publish_calls == 0);
}

static void test_rtc_failure_without_offset_restores_raw_rtc(void)
{
    fixture_t fixture = fixture_defaults();
    fixture.write_rtc_ok = 0;
    pj_time_transaction_result_t result = execute(&fixture, 0);
    assert(result.status == PJ_TIME_TRANSACTION_RTC_FAILED_ROLLED_BACK);
    assert(result.partial_components == 0);
    assert(strcmp(fixture.operations, "SWR") == 0);
    assert(fixture.publish_calls == 0);

    fixture = fixture_defaults();
    fixture.write_rtc_ok = 0;
    fixture.restore_rtc_ok = 0;
    result = execute(&fixture, 0);
    assert(result.status == PJ_TIME_TRANSACTION_PARTIAL_COMMIT);
    assert(result.partial_components == PJ_TIME_TRANSACTION_COMPONENT_RTC);
    assert(strcmp(fixture.operations, "SWR") == 0);
    assert(fixture.publish_calls == 0);
}

static void test_rtc_failure_exhausts_rollback_combinations(void)
{
    for (unsigned failures = 0; failures < 4; failures++) {
        fixture_t fixture = fixture_defaults();
        fixture.write_rtc_ok = 0;
        fixture.restore_rtc_ok = (failures & 1u) == 0;
        fixture.old_offset_ok = (failures & 2u) == 0;
        pj_time_transaction_result_t result = execute(&fixture, 1);
        assert(strcmp(fixture.operations, "SOWRK") == 0);
        assert(fixture.publish_calls == 0);

        uint32_t expected = 0;
        if ((failures & 1u) != 0) {
            expected |= PJ_TIME_TRANSACTION_COMPONENT_RTC;
        }
        if ((failures & 2u) != 0) {
            expected |= PJ_TIME_TRANSACTION_COMPONENT_UTC_OFFSET;
        }
        assert(result.partial_components == expected);
        assert(result.status == (expected == 0
            ? PJ_TIME_TRANSACTION_RTC_FAILED_ROLLED_BACK
            : PJ_TIME_TRANSACTION_PARTIAL_COMMIT));
    }
}

static void test_invalid_contract_never_calls_callbacks(void)
{
    fixture_t fixture = fixture_defaults();
    pj_time_transaction_request_t request = {
        .update_offset = 1,
        .old_offset_known = 1,
    };
    pj_time_transaction_ops_t ops = {
        .context = &fixture,
        .snapshot_rtc = snapshot_rtc,
        .write_rtc = write_rtc,
        .restore_rtc = restore_rtc,
        .publish = publish,
    };
    pj_time_transaction_result_t result =
        pj_time_transaction_execute(&request, &ops);
    assert(result.status == PJ_TIME_TRANSACTION_INVALID);
    assert(strcmp(fixture.operations, "") == 0);
    assert(strcmp(pj_time_transaction_status_name(result.status), "invalid") == 0);
}

int main(void)
{
    test_success_orders_publication_last();
    test_snapshot_failure_has_no_side_effects();
    test_offset_failure_restores_known_and_unknown_state();
    test_offset_failure_reports_failed_rollback();
    test_rtc_failure_without_offset_restores_raw_rtc();
    test_rtc_failure_exhausts_rollback_combinations();
    test_invalid_contract_never_calls_callbacks();
    puts("time transaction tests passed");
    return 0;
}
