#include "pal_rtos_threadx/pal_rtos_threadx.h"

#include "tx_api.h"

#include <stddef.h>

/*
 * The macros of a critical section insist on a save slot declared by name, and the name
 * is the port's (`interrupt_save` on the Cortex-M GNU port, `tx_saved_posture` on the
 * Linux one). Capture and restore it through one alias so both ports work; a rename
 * upstream breaks the build rather than silently changing what gets restored.
 */
#if defined(__arm__)
#define EDGE_THREADX_SAVE_SLOT interrupt_save
#else
#define EDGE_THREADX_SAVE_SLOT tx_saved_posture
#endif

/* --- architecture primitives (D46) -------------------------------------- */

/*
 * Task context only, and nestable. `TX_INTERRUPT_SAVE_AREA` declares the save slot the
 * macros insist on by name, and only the outermost enter takes one: an interrupted
 * critical section cannot run here because the outermost call masked every interrupt
 * the chosen mode covers (see EDGE_THREADX_MASK_MODE in edge_threadx_config.h).
 */
static void critical_enter(void *self) {
    edge_rtos_pal_state_t *state = (edge_rtos_pal_state_t *)self;
    if (state == NULL)
        return;
    if (state->depth == 0u) {
        TX_INTERRUPT_SAVE_AREA
        TX_DISABLE
        state->posture = (uint32_t)EDGE_THREADX_SAVE_SLOT;
    }
    ++state->depth;
}

static void critical_exit(void *self) {
    edge_rtos_pal_state_t *state = (edge_rtos_pal_state_t *)self;
    if (state == NULL || state->depth == 0u)
        return;
    --state->depth;
    if (state->depth == 0u) {
        TX_INTERRUPT_SAVE_AREA
        EDGE_THREADX_SAVE_SLOT = (UINT)state->posture;
        TX_RESTORE
    }
}

static void memory_barrier(void *self) {
    (void)self;
#if defined(__arm__)
    __asm volatile("dsb 0xF" ::: "memory");
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

/*
 * The kernel tick, extended to 64 bit. Masked because the clock is read from an ISR
 * as well (the event sink stamps timestamps): an interleaved sample would advance the
 * accumulator's last value, and the interrupted read would then look like a wrap that
 * never happened.
 */
static uint64_t monotonic_ticks(void *self) {
    edge_rtos_pal_state_t *state = (edge_rtos_pal_state_t *)self;
#if defined(__arm__)
    TX_INTERRUPT_SAVE_AREA
    TX_DISABLE
    const ULONG ticks = tx_time_get();
    const uint64_t extended =
        edge_tick64_extend(state != NULL ? &state->tick : NULL, (uint32_t)ticks);
    TX_RESTORE
    return extended;
#else
    /* The host port has no interrupt that samples this clock, so an interleaved read
     * cannot happen and the critical section is pure cost - and on that port a critical
     * section is a non-recursive mutex, which the sink's own guard would be holding. */
    const ULONG ticks = tx_time_get();
    return edge_tick64_extend(state != NULL ? &state->tick : NULL, (uint32_t)ticks);
#endif
}

static bool in_isr(void *self) {
    (void)self;
#if defined(__arm__)
    uint32_t ipsr = 0u;
    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    // cppcheck-suppress knownConditionTrueFalse ; `ipsr` is written by the asm above
    return ipsr != 0u;
#else
    /* The host port schedules threads and has no interrupt context to be inside of. */
    return false;
#endif
}

static void isr_enter(void *self) {
    (void)self;
}

static void isr_exit(void *self) {
    (void)self;
}

/* Raw wait primitive: the D71 sequence lives in edge_os_idle_wait(). */
static void idle(void *self) {
    (void)self;
#if defined(__arm__)
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("wfi" ::: "memory");
#else
    /* There is no halt instruction to give up the CPU to on the host port. A no-op is
     * the truth here; inventing a yield would be a different contract. */
#endif
}

/* --- ISR-side guard ------------------------------------------------------ */

static uint32_t g_isr_posture;
static uint32_t g_isr_depth;

static void irq_guard_enter(void *self) {
    (void)self;
    if (g_isr_depth == 0u) {
        TX_INTERRUPT_SAVE_AREA
        TX_DISABLE
        g_isr_posture = (uint32_t)EDGE_THREADX_SAVE_SLOT;
    }
    ++g_isr_depth;
}

static void irq_guard_exit(void *self) {
    (void)self;
    if (g_isr_depth > 0u)
        --g_isr_depth;
    if (g_isr_depth != 0u)
        return;
    TX_INTERRUPT_SAVE_AREA
    EDGE_THREADX_SAVE_SLOT = (UINT)g_isr_posture;
    TX_RESTORE
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
    if (state == NULL)
        return;
    edge_tick64_reset(&state->tick);
    state->posture = 0u;
    state->depth = 0u;
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
