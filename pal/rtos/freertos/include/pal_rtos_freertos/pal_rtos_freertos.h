#ifndef PAL_RTOS_FREERTOS_H
#define PAL_RTOS_FREERTOS_H

#include "edge/event.h"
#include "edge/pal.h"
#include "pal_os/tick64.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FreeRTOS implementation of the architecture primitives (D46/D85).
 *
 * FreeRTOS headers stay inside `pal/rtos/freertos` (D50), so this header exposes
 * only neutral `edge_*` types.
 */

typedef struct edge_rtos_pal_state {
    edge_tick64_t tick;
} edge_rtos_pal_state_t;

void edge_rtos_pal_init(edge_rtos_pal_state_t *state);

/*
 * The architecture PAL for a product whose scheduler is FreeRTOS.
 *
 * Context rules:
 *   critical_enter/exit  task context only: taskENTER_CRITICAL/EXIT, nestable,
 *                        mask-based (BASEPRI), never a global interrupt disable.
 *                        ISR code must not call these - it brackets with
 *                        edge_rtos_irq_guard() instead.
 *   in_isr               xPortIsInsideInterrupt(): reads the hardware IPSR, so
 *                        there is no flag to keep in sync.
 *   isr_enter/isr_exit   context markers that have nothing to do on Cortex-M;
 *                        they stay in the port because the contract has them.
 *   monotonic_ticks      the kernel tick, extended to 64 bit (D72).
 *   idle                 WFI. Tickless sleep is the scheduler idle task's job
 *                        (configUSE_TICKLESS_IDLE); see docs/low-power.md.
 */
edge_pal_port_t edge_rtos_pal_port(edge_rtos_pal_state_t *state);

/*
 * ISR-side guard for the event sink, installed as `edge_event_sink_t.guard` by
 * the composition root. Wraps the push in a BASEPRI mask save/restore, which is
 * what makes an ISR-delivered event safe against a task-context reader of the
 * same queue.
 *
 * Interrupt context only: this is the ISR half of the contract. See the source
 * for why it does not yield on exit.
 */
edge_irq_guard_t edge_rtos_irq_guard(void);

#ifdef __cplusplus
}
#endif

#endif
