#include "pal_rtos_rtems/pal_rtos_rtems.h"

#if defined(__rtems__)
#include <rtems.h>
#include <rtems/fatal.h>
#endif

#include <stddef.h>
#include <stdint.h>

/* --- architecture primitives (D46) -------------------------------------- */

#if defined(__rtems__)
static rtems_interrupt_level g_pal_isr_level;
#endif

static void critical_enter(void *self) {
    edge_rtos_pal_state_t *state = (edge_rtos_pal_state_t *)self;
    if (state == NULL) {
        return;
    }
#if defined(__rtems__)
    if (state->lock_depth == 0u) {
        rtems_interrupt_local_disable(g_pal_isr_level);
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
#if defined(__rtems__)
    if (state->lock_depth == 0u) {
        rtems_interrupt_local_enable(g_pal_isr_level);
    }
#endif
}

static void memory_barrier(void *self) {
    (void)self;
    __asm volatile("" ::: "memory");
}

/*
 * The RTEMS kernel tick, extended to 64 bit.
 */
static uint64_t monotonic_ticks(void *self) {
    edge_rtos_pal_state_t *state = (edge_rtos_pal_state_t *)self;
#if defined(__rtems__)
    rtems_interrupt_level level;
    rtems_interrupt_local_disable(level);
    const rtems_interval now = rtems_clock_get_ticks_since_boot();
    const uint64_t extended =
        edge_tick64_extend(state != NULL ? &state->tick : NULL, (uint32_t)now);
    rtems_interrupt_local_enable(level);
    return extended;
#else
    static uint32_t fake_ticks;
    return edge_tick64_extend(state != NULL ? &state->tick : NULL, ++fake_ticks);
#endif
}

static bool in_isr(void *self) {
    (void)self;
#if defined(__rtems__)
    return rtems_interrupt_is_in_progress() != 0;
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
#if defined(__rtems__)
    __asm volatile("wfi" ::: "memory");
#else
    __asm volatile("" ::: "memory");
#endif
}

/* --- ISR-side guard ------------------------------------------------------ */

#if defined(__rtems__)
static rtems_interrupt_level g_guard_level;
#endif
static uint32_t g_guard_depth;

static void irq_guard_enter(void *self) {
    (void)self;
#if defined(__rtems__)
    if (g_guard_depth == 0u) {
        rtems_interrupt_local_disable(g_guard_level);
    }
#endif
    ++g_guard_depth;
}

static void irq_guard_exit(void *self) {
    (void)self;
    if (g_guard_depth > 0u) {
        --g_guard_depth;
    }
    if (g_guard_depth != 0u) {
        return;
    }
#if defined(__rtems__)
    rtems_interrupt_local_enable(g_guard_level);
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
