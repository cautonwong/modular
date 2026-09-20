#ifndef PAL_RTOS_H
#define PAL_RTOS_H

#include "edge/module.h"
#include "pal_os/os.h"
#include "pal_rtos/assert.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Neutral RTOS host contract.
 *
 * A product runs its superloop as a single RTOS task and injects events from a
 * sibling task. Products depend on this header only, so RTOS headers stay
 * confined to `pal/rtos/<os>` (D44/D47).
 */
typedef void (*edge_rtos_task_fn)(void *arg);

edge_status_t edge_rtos_task_create(const char *name, edge_rtos_task_fn fn, void *arg,
                                    uint32_t stack_words, uint32_t priority);

/* Starts the RTOS scheduler. Does not return. */
void edge_rtos_start(void);

/* yield/sleep backed by the RTOS, for the sys idle hook. */
edge_os_port_t edge_rtos_os_port(void);

/* Bytes of stack still unused by the calling task (high-water mark). */
uint32_t edge_rtos_task_stack_high_water(void);

/*
 * Wake/block primitive (D47/D71).
 *
 * A runner that polls cannot be a battery product: at a 1 ms poll the fixed cost
 * per wake is already ~1800x over a ten-year budget (docs/low-power.md section 1).
 * The task must be able to park until work actually arrives, and the ISR that
 * produces the work must be able to wake it without a PendSV on every interrupt
 * that has nothing to do. Three deliberately tiny functions:
 *
 *   wake_target_set_self()  the calling task becomes the wake target
 *   wake_from_isr()         wake it; ISR-safe, no-op when no target is set, and
 *                           yields only if a task was actually made ready
 *   wait_for_work(ticks)    park the caller; true when woken by work, false on
 *                           timeout. A timeout is how periodic work still runs:
 *                           pass the next deadline, not a fixed tick.
 */
void edge_rtos_wake_target_set_self(void);
void edge_rtos_wake_from_isr(void);
bool edge_rtos_wait_for_work(uint32_t timeout_ticks);

/* Assert contract (D87) lives in pal_rtos/assert.h so the product-owned
 * FreeRTOSConfig.h can use it without pulling in this header. */

#ifdef __cplusplus
}
#endif

#endif
