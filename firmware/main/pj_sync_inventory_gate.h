#ifndef PJ_SYNC_INVENTORY_GATE_H
#define PJ_SYNC_INVENTORY_GATE_H

#include <stdint.h>

typedef struct {
    uint32_t session;
    uint64_t retry_after_ms;
} pj_sync_inventory_gate_t;

void pj_sync_inventory_gate_init(pj_sync_inventory_gate_t *gate);
/* Returns 1 when a retained request from an older session was discarded. */
int pj_sync_inventory_gate_reconcile(pj_sync_inventory_gate_t *gate,
                                     uint32_t current_session);
int pj_sync_inventory_gate_bind(pj_sync_inventory_gate_t *gate,
                                uint32_t session, uint64_t now_ms);
int pj_sync_inventory_gate_poll_due(const pj_sync_inventory_gate_t *gate,
                                    uint32_t current_session,
                                    uint64_t now_ms);
void pj_sync_inventory_gate_defer(pj_sync_inventory_gate_t *gate,
                                  uint64_t now_ms, uint64_t delay_ms);
uint32_t pj_sync_inventory_gate_take(pj_sync_inventory_gate_t *gate);
/* Returns 1 when a retained request was discarded. */
int pj_sync_inventory_gate_reset(pj_sync_inventory_gate_t *gate);

#endif
