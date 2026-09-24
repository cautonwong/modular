#ifndef PAL_RTOS_THREADX_H
#define PAL_RTOS_THREADX_H

#include "edge/event.h"
#include "edge/pal.h"
#include "pal_os/tick64.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ThreadX implementation of the architecture primitives (D46/D85, issue #134).
 *
 * ThreadX headers stay inside `pal/rtos/threadx` (D50), so this header exposes only
 * neutral `edge_*` types. The port is kernel-shaped - like the FreeRTOS one and
 * unlike Zephyr's - so it compiles for a Cortex-M target only and refuses to build
 * anywhere else.
 */

typedef struct edge_rtos_pal_state {
    edge_tick64_t tick;   /* kernel tick, extended to 64 bit (D72) */
    uint32_t posture;     /* interrupt posture saved by the outermost TX_DISABLE */
    uint32_t depth;       /* nesting count */
} edge_rtos_pal_state_t;

void edge_rtos_pal_init(edge_rtos_pal_state_t *state);

/*
 * Context rules, as on the other ports:
 *   critical_enter/exit  task context only; TX_DISABLE/TX_RESTORE around the
 *                        outermost enter, nestable. ISR code uses the guard below.
 *   in_isr               reads the hardware IPSR (no flag to keep in sync).
 *   monotonic_ticks      `tx_time_get()` extended through `edge_tick64_extend`,
 *                        masked because the event sink reads the clock from an ISR.
 *   idle                 DSB plus WFI; the D71 atomic sequence lives in
 *                        `edge_os_idle_wait()` and calls this inside its mask.
 */
edge_pal_port_t edge_rtos_pal_port(edge_rtos_pal_state_t *state);

/* ISR-side guard for the event sink: save/restore the interrupt posture. */
edge_irq_guard_t edge_rtos_irq_guard(void);

#ifdef __cplusplus
}
#endif

#endif
