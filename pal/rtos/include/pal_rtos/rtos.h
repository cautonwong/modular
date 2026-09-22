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
 *
 * The four semantics below are pinned here because the kernels disagree about all
 * of them, and a contract that silently inherits one kernel's convention is not
 * neutral - it is that kernel's shape wearing a neutral name. Each `pal/rtos/<os>`
 * port translates; the caller states intent and never learns the convention.
 * See `docs/rtos-ports.md` for what each kernel provides and what its port must
 * therefore emulate.
 *
 * 1. PRIORITY DIRECTION AND RANGE: `0` is the HIGHEST priority, and numerically
 *    larger is lower. An implementation has a maximum it can represent, and a
 *    request above it must be **rejected with EDGE_EINVAL rather than clamped**:
 *    clamping turns a caller's mistake into a scheduling surprise. (FreeRTOS is the opposite,
 * ThreadX agrees with this, Zephyr's cooperative priorities are negative numbers.) A port must
 * invert rather than forward: forwarding means a caller asking for "the priority above" gets the
 *    one below.
 *
 * 2. TASK STORAGE: the *implementation* owns the task's control block and stack.
 *    A kernel that allocates (FreeRTOS with dynamic allocation) does so directly;
 *    a kernel that cannot (ThreadX, Zephyr with static threads) must supply a
 *    static pool sized by its own configuration macro and return EDGE_ENOSPC when
 *    it is exhausted. `stack_words` is in *words* as a consequence of this
 *    signature; a port whose kernel counts bytes converts, and one whose kernel
 *    has a documented minimum rejects a too-small request with EDGE_EINVAL.
 *
 * 3. START SEMANTICS: creation before the scheduler runs must work, because that is
 *    what a composition root does. A kernel whose entry call never returns
 *    (`tx_kernel_enter`) buffers the requests and creates them from its entry
 *    callback; a port may not require the caller to move assembly into that
 *    callback.
 *
 * 4. CAPABILITIES THAT MAY BE ABSENT: `edge_rtos_task_stack_high_water()` returns 0
 *    when the kernel cannot report it (ThreadX exposes only an approximation and
 *    only with stack checking enabled). 0 means "unavailable", never "plenty" -
 *    a product must not read it as a safety margin. The assert contract lives in
 *    `pal_rtos/assert.h`; a kernel with no assert macro (ThreadX) wires its fault
 *    handlers and stack-error notification to `edge_rtos_assert_failed()`.
 */
typedef void (*edge_rtos_task_fn)(void *arg);

/* `priority`: 0 is highest. `stack_words`: words, implementation-owned storage. */
edge_status_t edge_rtos_task_create(const char *name, edge_rtos_task_fn fn, void *arg,
                                    uint32_t stack_words, uint32_t priority);

/* Starts the RTOS scheduler. Does not return. Creation before this call works. */
void edge_rtos_start(void);

/* yield/sleep backed by the RTOS, for the sys idle hook. */
edge_os_port_t edge_rtos_os_port(void);

/* Bytes of stack still unused by the calling task, or 0 when the kernel cannot
 * report it (unavailable, not "plenty"). */
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
