#include "pal_rtos_riot/pal_rtos_riot.h"

#if defined(RIOT_VERSION) || defined(__RIOT__)
#include "irq.h"
#include "ztimer.h"
#endif

#include <stddef.h>
#include <stdint.h>

/* --- architecture primitives (D46) -------------------------------------- */

#if defined(RIOT_VERSION) || defined(__RIOT__)
static unsigned g_pal_isr_level;
#endif

static void critical_enter(void *self) {
    edge_rtos_pal_state_t *state = (edge_rtos_pal_state_t *)self;
    if (state == NULL) {
        return;
    }
#if defined(RIOT_VERSION) || defined(__RIOT__)
    if (state->lock_depth == 0u) {
        g_pal_isr_level = irq_disable();
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
#if defined(RIOT_VERSION) || defined(__RIOT__)
    if (state->lock_depth == 0u) {
        irq_restore(g_pal_isr_level);
    }
#endif
}

static void memory_barrier(void *self) {
    (void)self;
    __asm volatile("" ::: "memory");
}

/*
 * The RIOT kernel tick, extended to 64 bit.
 */
static uint64_t monotonic_ticks(void *self) {
    edge_rtos_pal_state_t *state = (edge_rtos_pal_state_t *)self;
#if defined(RIOT_VERSION) || defined(__RIOT__)
    const unsigned level = irq_disable();
    const uint32_t now = ztimer_now(ZTIMER_MSEC);
    const uint64_t extended = edge_tick64_extend(state != NULL ? &state->tick : NULL, now);
    irq_restore(level);
    return extended;
#else
    static uint32_t fake_ticks;
    return edge_tick64_extend(state != NULL ? &state->tick : NULL, ++fake_ticks);
#endif
}

static bool in_isr(void *self) {
    (void)self;
#if defined(RIOT_VERSION) || defined(__RIOT__)
    return irq_is_in();
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
#if defined(RIOT_VERSION) || defined(__RIOT__)
    __asm volatile("wfi" ::: "memory");
#else
    __asm volatile("" ::: "memory");
#endif
}

/* --- ISR-side guard ------------------------------------------------------ */

#if defined(RIOT_VERSION) || defined(__RIOT__)
static unsigned g_guard_level;
#endif
static uint32_t g_guard_depth;

static void irq_guard_enter(void *self) {
    (void)self;
#if defined(RIOT_VERSION) || defined(__RIOT__)
    if (g_guard_depth == 0u) {
        g_guard_level = irq_disable();
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
#if defined(RIOT_VERSION) || defined(__RIOT__)
    irq_restore(g_guard_level);
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
