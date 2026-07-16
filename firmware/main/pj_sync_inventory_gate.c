#include "pj_sync_inventory_gate.h"

#include <stddef.h>

void pj_sync_inventory_gate_init(pj_sync_inventory_gate_t *gate)
{
    if (gate == NULL) {
        return;
    }
    *gate = (pj_sync_inventory_gate_t) {0};
}

int pj_sync_inventory_gate_reset(pj_sync_inventory_gate_t *gate)
{
    if (gate == NULL) {
        return 0;
    }
    int retained = gate->session != 0;
    pj_sync_inventory_gate_init(gate);
    return retained;
}

int pj_sync_inventory_gate_reconcile(pj_sync_inventory_gate_t *gate,
                                     uint32_t current_session)
{
    if (gate == NULL || gate->session == 0 ||
        gate->session == current_session) {
        return 0;
    }
    return pj_sync_inventory_gate_reset(gate);
}

int pj_sync_inventory_gate_bind(pj_sync_inventory_gate_t *gate,
                                uint32_t session, uint64_t now_ms)
{
    if (gate == NULL || gate->session != 0 || session == 0) {
        return 0;
    }
    gate->session = session;
    gate->retry_after_ms = now_ms;
    return 1;
}

int pj_sync_inventory_gate_poll_due(const pj_sync_inventory_gate_t *gate,
                                    uint32_t current_session,
                                    uint64_t now_ms)
{
    return gate != NULL && gate->session != 0 &&
        gate->session == current_session && now_ms >= gate->retry_after_ms;
}

void pj_sync_inventory_gate_defer(pj_sync_inventory_gate_t *gate,
                                  uint64_t now_ms, uint64_t delay_ms)
{
    if (gate == NULL || gate->session == 0) {
        return;
    }
    gate->retry_after_ms = UINT64_MAX - now_ms < delay_ms ?
        UINT64_MAX : now_ms + delay_ms;
}

uint32_t pj_sync_inventory_gate_take(pj_sync_inventory_gate_t *gate)
{
    if (gate == NULL) {
        return 0;
    }
    uint32_t session = gate->session;
    pj_sync_inventory_gate_init(gate);
    return session;
}
