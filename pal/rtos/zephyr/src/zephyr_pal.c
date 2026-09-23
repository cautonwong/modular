#include "pal_rtos_zephyr/pal_rtos_zephyr.h"

#if !defined(__ZEPHYR__)
#error                                                                                             \
    "pal/rtos/zephyr is a Zephyr port: build it inside a Zephyr build (see zephyr/CMakeLists.txt). A host or bare-metal build gets no fake primitives - a port that pretends to work is worse than one that refuses to build."
#endif

#include <zephyr/arch/cpu.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>

#include <stddef.h>

/* --- architecture primitives (D46) -------------------------------------- */

static void critical_enter(void *self) {
    edge_rtos_pal_state_t *state = (edge_rtos_pal_state_t *)self;
    if (state == NULL) {
        return;
    }
    if (state->lock_depth == 0u) {
        state->lock_key = irq_lock();
    }
    ++state->lock_depth;
}

static void critical_exit(void *self) {
    edge_rtos_pal_state_t *state = (edge_rtos_pal_state_t *)self;
    if (state == NULL || state->lock_depth == 0u) {
        return;
    }
    --state->lock_depth;
    if (state->lock_depth == 0u) {
        irq_unlock(state->lock_key);
    }
}

static void memory_barrier(void *self) {
    (void)self;
    barrier_dmem_fence_full(); /* Zephyr's own full fence, not hand-rolled asm */
}

/*
 * The Zephyr kernel tick, extended to 64 bit.
 */
static uint64_t monotonic_ticks(void *self) {
    edge_rtos_pal_state_t *state = (edge_rtos_pal_state_t *)self;
    /* Masked: an ISR reads the clock too (the event sink stamps timestamps), and the
     * accumulator must not lose a wrap to an interleaved sample. */
    const unsigned int key = irq_lock();
    const int64_t ticks = k_uptime_ticks();
    const uint64_t extended =
        edge_tick64_extend(state != NULL ? &state->tick : NULL, (uint32_t)ticks);
    irq_unlock(key);
    return extended;
}

static bool in_isr(void *self) {
    (void)self;
    return k_is_in_isr() != 0;
}

static void isr_enter(void *self) {
    (void)self;
}

static void isr_exit(void *self) {
    (void)self;
}

static void idle(void *self) {
    (void)self;
    k_cpu_idle();
}

/* --- ISR-side guard ------------------------------------------------------ */

static unsigned int g_isr_key;
static uint32_t g_isr_depth;

static void irq_guard_enter(void *self) {
    (void)self;
    if (g_isr_depth == 0u) {
        g_isr_key = irq_lock();
    }
    ++g_isr_depth;
}

static void irq_guard_exit(void *self) {
    (void)self;
    if (g_isr_depth > 0u) {
        --g_isr_depth;
    }
    if (g_isr_depth != 0u) {
        return;
    }
    irq_unlock(g_isr_key);
}

edge_irq_guard_t edge_rtos_irq_guard(void) {
    const edge_irq_guard_t guard = {
        .enter = irq_guard_enter,
        .exit = irq_guard_exit,
        .self = NULL,
    };
    return guard;
}

void edge_rtos_pal_init(edge_rtos_pal_state_t *state) {
    if (state == NULL) {
        return;
    }
    edge_tick64_reset(&state->tick);
    state->lock_key = 0u;
    state->lock_depth = 0u;
}

edge_pal_port_t edge_rtos_pal_port(edge_rtos_pal_state_t *state) {
    const edge_pal_port_t port = {
        .critical_enter = critical_enter,
        .critical_exit = critical_exit,
        .memory_barrier = memory_barrier,
        .monotonic_ticks = monotonic_ticks,
        .in_isr = in_isr,
        .isr_enter = isr_enter,
        .isr_exit = isr_exit,
        .idle = idle,
        .self = state,
    };
    return port;
}
