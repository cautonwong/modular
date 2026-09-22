#include "pal_rtos_zephyr/pal_rtos_zephyr.h"

#if defined(__ZEPHYR__)
#include <zephyr/arch/cpu.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>
#endif

#include <stddef.h>

/* --- architecture primitives (D46) -------------------------------------- */

static void critical_enter(void *self) {
    edge_rtos_pal_state_t *state = (edge_rtos_pal_state_t *)self;
    if (state == NULL) {
        return;
    }
#if defined(__ZEPHYR__)
    if (state->lock_depth == 0u) {
        state->lock_key = irq_lock();
    }
    ++state->lock_depth;
#else
    ++state->lock_depth;
#endif
}

static void critical_exit(void *self) {
    edge_rtos_pal_state_t *state = (edge_rtos_pal_state_t *)self;
    if (state == NULL || state->lock_depth == 0u) {
        return;
    }
    --state->lock_depth;
#if defined(__ZEPHYR__)
    if (state->lock_depth == 0u) {
        irq_unlock(state->lock_key);
    }
#endif
}

static void memory_barrier(void *self) {
    (void)self;
#if defined(__ZEPHYR__)
    barrier_dmem_fence_full();
#else
    __asm volatile("" ::: "memory");
#endif
}

/*
 * The Zephyr kernel tick, extended to 64 bit.
 */
static uint64_t monotonic_ticks(void *self) {
    edge_rtos_pal_state_t *state = (edge_rtos_pal_state_t *)self;
#if defined(__ZEPHYR__)
    const unsigned int key = irq_lock();
    const int64_t ticks = k_uptime_ticks();
    const uint64_t extended =
        edge_tick64_extend(state != NULL ? &state->tick : NULL, (uint32_t)ticks);
    irq_unlock(key);
    return extended;
#else
    static uint32_t fake_ticks;
    return edge_tick64_extend(state != NULL ? &state->tick : NULL, ++fake_ticks);
#endif
}

static bool in_isr(void *self) {
    (void)self;
#if defined(__ZEPHYR__)
    return k_is_in_isr() != 0;
#else
    return false;
#endif
}

static void isr_enter(void *self) {
    (void)self;
}

static void isr_exit(void *self) {
    (void)self;
}

static void idle(void *self) {
    (void)self;
#if defined(__ZEPHYR__)
    k_cpu_idle();
#else
    __asm volatile("" ::: "memory");
#endif
}

/* --- ISR-side guard ------------------------------------------------------ */

#if defined(__ZEPHYR__)
static unsigned int g_isr_key;
#endif
static uint32_t g_isr_depth;

static void irq_guard_enter(void *self) {
    (void)self;
#if defined(__ZEPHYR__)
    if (g_isr_depth == 0u) {
        g_isr_key = irq_lock();
    }
#endif
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
#if defined(__ZEPHYR__)
    irq_unlock(g_isr_key);
#endif
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
