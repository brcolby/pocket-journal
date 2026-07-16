#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "pj_sync_inventory_gate.h"

static void test_busy_retry_waits_within_same_session(void)
{
    pj_sync_inventory_gate_t gate;
    pj_sync_inventory_gate_init(&gate);

    assert(pj_sync_inventory_gate_bind(&gate, 10U, 100U));
    assert(pj_sync_inventory_gate_poll_due(&gate, 10U, 100U));
    pj_sync_inventory_gate_defer(&gate, 100U, 200U);
    assert(!pj_sync_inventory_gate_poll_due(&gate, 10U, 299U));
    assert(pj_sync_inventory_gate_poll_due(&gate, 10U, 300U));
    assert(pj_sync_inventory_gate_take(&gate) == 10U);
    assert(!pj_sync_inventory_gate_poll_due(&gate, 10U, 300U));
}

static void test_newer_session_discards_worker_binding_and_retry_deadline(void)
{
    pj_sync_inventory_gate_t gate;
    pj_sync_inventory_gate_init(&gate);

    assert(pj_sync_inventory_gate_bind(&gate, 20U, 1000U));
    pj_sync_inventory_gate_defer(&gate, 1000U, 200U);
    assert(!pj_sync_inventory_gate_poll_due(&gate, 20U, 1100U));

    /* The return value tells app_main to cancel the old worker result. */
    assert(pj_sync_inventory_gate_reconcile(&gate, 21U));
    assert(!pj_sync_inventory_gate_reconcile(&gate, 21U));
    assert(pj_sync_inventory_gate_bind(&gate, 21U, 1100U));
    assert(pj_sync_inventory_gate_poll_due(&gate, 21U, 1100U));
    assert(!pj_sync_inventory_gate_poll_due(&gate, 20U, 1300U));
}

static void test_retry_deadline_saturates(void)
{
    pj_sync_inventory_gate_t gate;
    pj_sync_inventory_gate_init(&gate);
    assert(pj_sync_inventory_gate_bind(&gate, 1U, UINT64_MAX - 10U));
    pj_sync_inventory_gate_defer(&gate, UINT64_MAX - 10U, 20U);
    assert(!pj_sync_inventory_gate_poll_due(&gate, 1U, UINT64_MAX - 1U));
    assert(pj_sync_inventory_gate_poll_due(&gate, 1U, UINT64_MAX));
    assert(pj_sync_inventory_gate_reset(&gate));
    assert(!pj_sync_inventory_gate_reset(&gate));
}

int main(void)
{
    test_busy_retry_waits_within_same_session();
    test_newer_session_discards_worker_binding_and_retry_deadline();
    test_retry_deadline_saturates();
    puts("sync inventory gate tests passed");
    return 0;
}
