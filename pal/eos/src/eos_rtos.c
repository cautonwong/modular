#include "pal_eos/eos.h"
#include "pal_os/os.h"
#include "pal_rtos/rtos.h"

#include <stddef.h>

/*
 * EOS as a `pal_rtos` port (D46/D85): the same function names the FreeRTOS port
 * defines, because a product links exactly one port - and this is the one that makes
 * the neutral contract *executable on the host*. The other two need a kernel and a
 * target, so without EOS the contract could only be checked statically;
 * `tests/contract/rtos_contract.c` runs against it.
 *
 * The mapping is not one-to-one, and the differences are the point:
 *
 *   - EOS is a fixed-rate executive, so it has **no per-task stack**: `stack_words`
 *     is accepted and ignored, and `edge_rtos_task_stack_high_water()` returns 0,
 *     which the contract defines as "unavailable" rather than "plenty".
 *   - It has **no blocking**: the runner never parks, because a round is always
 *     work. So `wait_for_work()` reports work instead of waiting and the wake pair
 *     is a no-op. That is honest here and must not be read as power management -
 *     low power belongs to the target's PAL idle path (`docs/low-power.md`), not to
 *     this port.
 */
static edge_eos_t *g_eos;

edge_status_t edge_eos_bind_rtos(edge_eos_t *eos) {
    if (eos == NULL)
        return EDGE_EINVAL;
    g_eos = eos;
    return EDGE_OK;
}

edge_status_t edge_rtos_task_create(const char *name, edge_rtos_task_fn fn, void *arg,
                                    uint32_t stack_words, uint32_t priority) {
    if (g_eos == NULL)
        return EDGE_ESTATE; /* the composition root binds the executive first */
    (void)stack_words;      /* no per-task stack exists to size */
    return edge_eos_task_add(g_eos, name, fn, arg, priority, 0u);
}

void edge_rtos_start(void) {
    edge_eos_run(g_eos); /* returns only when a task stops the executive */
}

static void os_yield(void *self) {
    (void)self; /* the executive's next round is the yield */
}

edge_os_port_t edge_rtos_os_port(void) {
    /* `sleep_ms` is NULL on purpose: an executive has no sleep primitive to offer,
     * and a fabricated one would hide that from whoever reads the port. */
    const edge_os_port_t port = {.yield = os_yield, .sleep_ms = NULL, .self = NULL};
    return port;
}

uint32_t edge_rtos_task_stack_high_water(void) {
    return 0u; /* unavailable: EOS has no per-task stacks */
}

void edge_rtos_wake_target_set_self(void) {
    /* nothing to publish: no task can be parked */
}

void edge_rtos_wake_from_isr(void) {
    /* nothing to wake - and safe to call, so an ISR never has to know which port it
     * is running under */
}

bool edge_rtos_wait_for_work(uint32_t timeout_ticks) {
    (void)timeout_ticks;
    return true; /* the runner never parks; a round is always work */
}
