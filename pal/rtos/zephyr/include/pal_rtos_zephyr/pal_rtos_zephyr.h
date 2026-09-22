#ifndef PAL_RTOS_ZEPHYR_H
#define PAL_RTOS_ZEPHYR_H

#include "edge/event.h"
#include "edge/pal.h"
#include "pal_os/tick64.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Zephyr implementation of the architecture primitives (D46/D85).
 *
 * Zephyr headers stay inside `pal/rtos/zephyr` (D50), so this header exposes
 * only neutral `edge_*` types.
 */

typedef struct edge_rtos_pal_state {
    edge_tick64_t tick;
    unsigned int lock_key;
    uint32_t lock_depth;
} edge_rtos_pal_state_t;

void edge_rtos_pal_init(edge_rtos_pal_state_t *state);

/*
 * The architecture PAL for a product whose scheduler is Zephyr RTOS.
 *
 * Context rules:
 *   critical_enter/exit  task context critical sections (irq_lock/unlock).
 *   in_isr               k_is_in_isr(): reads hardware interrupt context.
 *   isr_enter/isr_exit   context markers for contract compliance.
 *   monotonic_ticks      the Zephyr kernel uptime tick, extended to 64 bit (D72).
 *   idle                 k_cpu_idle() / WFI.
 */
edge_pal_port_t edge_rtos_pal_port(edge_rtos_pal_state_t *state);

/*
 * ISR-side guard for the event sink, installed as `edge_event_sink_t.guard` by
 * the composition root. Wraps the push in an interrupt lock save/restore.
 */
edge_irq_guard_t edge_rtos_irq_guard(void);

#ifdef __cplusplus
}
#endif

#endif /* PAL_RTOS_ZEPHYR_H */
