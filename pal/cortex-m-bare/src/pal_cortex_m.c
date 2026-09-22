#include "pal_cortex_m/pal_cortex_m.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Free-running SysTick period, counted in processor clocks. */
#define SYSTICK_LOAD_DEFAULT 0x00FFFFFFu

#if defined(EDGE_PAL_CORTEX_M_HOST_TEST) && !defined(__arm__)

/*
 * Host test build: deterministic fallbacks so the contract can be exercised off
 * target. This branch is opt-in by macro on purpose - see the header's
 * architecture binding.
 */

static void critical_enter(void *self) {
    edge_pal_cortex_m_state_t *state = (edge_pal_cortex_m_state_t *)self;
    if (state != NULL)
        ++state->depth;
}

static void critical_exit(void *self) {
    edge_pal_cortex_m_state_t *state = (edge_pal_cortex_m_state_t *)self;
    if (state != NULL && state->depth > 0u)
        --state->depth;
}

static void memory_barrier(void *self) {
    (void)self;
}

static uint64_t monotonic_ticks(void *self) {
    edge_pal_cortex_m_state_t *state = (edge_pal_cortex_m_state_t *)self;
    if (state == NULL)
        return 0u;
    return ++state->host_ticks;
}

static bool in_isr(void *self) {
    (void)self;
    return false;
}

static void isr_enter(void *self) {
    (void)self;
}

static void isr_exit(void *self) {
    (void)self;
}

/* Raw wait primitive; see the ARM branch for the D71 ownership note. The host
 * fallback has nothing to wait for. */
static void idle(void *self) {
    (void)self;
}

#elif defined(__arm__)

#define SYSTICK_CTRL (*(volatile uint32_t *)0xe000e010u)
#define SYSTICK_LOAD (*(volatile uint32_t *)0xe000e014u)
#define SYSTICK_VAL (*(volatile uint32_t *)0xe000e018u)

static uint32_t primask_read(void) {
    uint32_t primask = 0u;
    __asm volatile("mrs %0, primask" : "=r"(primask));
    return primask;
}

/*
 * The saved slot is written only on the outermost enter, which is safe because a
 * critical section masks every maskable interrupt: no masking context can run
 * inside one, so nothing can interleave between the read and the mask (or inside
 * the section) and corrupt it. NMI/HardFault are not masked and are excluded by
 * the header - see it for why one slot is sufficient and what breaks otherwise.
 */
static void critical_enter(void *self) {
    edge_pal_cortex_m_state_t *state = (edge_pal_cortex_m_state_t *)self;
    if (state == NULL)
        return;
    if (state->depth == 0u)
        state->primask = primask_read();
    __asm volatile("cpsid i" ::: "memory");
    ++state->depth;
}

static void critical_exit(void *self) {
    edge_pal_cortex_m_state_t *state = (edge_pal_cortex_m_state_t *)self;
    if (state == NULL)
        return;
    if (state->depth > 0u)
        --state->depth;
    if (state->depth == 0u)
        __asm volatile("msr primask, %0" ::"r"(state->primask) : "memory");
}

static void memory_barrier(void *self) {
    (void)self;
    __asm volatile("dsb 0xF" ::: "memory");
}

/*
 * One VAL read, then the pure extension decides what it means. COUNTFLAG is
 * deliberately not consulted: reading CTRL clears it, so a wrap landing between
 * the VAL and CTRL reads is precisely the case where it is missed - and the
 * MPS2 model under QEMU does not report it as documented, which made the clock
 * step backwards by a whole period. See the header.
 */
static uint64_t monotonic_ticks(void *self) {
    edge_pal_cortex_m_state_t *state = (edge_pal_cortex_m_state_t *)self;
    if (state == NULL)
        return 0u;
    /*
     * The sample and the accumulator update are one critical section.
     *
     * The clock is read from an ISR as well - the event sink stamps timestamps from
     * it - so an ISR can preempt between the SYSTICK_VAL read and the update. It
     * would then advance `state->last`, and the interrupted read would be evaluated
     * against that newer sample, read as a wrap, and increment `wrap` for a wrap
     * that never happened. The mask costs a PRIMASK save/restore and restores the
     * previous value rather than clearing it.
     */
    const uint32_t primask = primask_read();
    __asm volatile("cpsid i" ::: "memory");
    const uint64_t now = edge_pal_cortex_m_extend(state, SYSTICK_VAL);
    __asm volatile("msr primask, %0" ::"r"(primask) : "memory");
    return now;
}

static bool in_isr(void *self) {
    (void)self;
    uint32_t ipsr = 0u;
    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    // cppcheck-suppress knownConditionTrueFalse ; `ipsr` is written by the asm above
    return ipsr != 0u;
}

static void isr_enter(void *self) {
    (void)self;
}

static void isr_exit(void *self) {
    (void)self;
}

/* Raw wait primitive. The D71 atomic sequence (mask -> re-check -> wait -> unmask)
 * is edge_os_idle_wait() in pal/os, which calls this inside its mask; do not move
 * the sequence here or it double-masks and re-checks outside its own protection. */
static void idle(void *self) {
    (void)self;
    __asm volatile("wfi" ::: "memory");
}

#else
#error                                                                                             \
    "pal/cortex-m-bare is a Cortex-M port: build for __arm__, or define EDGE_PAL_CORTEX_M_HOST_TEST for host unit tests only"
#endif

uint64_t edge_pal_cortex_m_extend(edge_pal_cortex_m_state_t *state, uint32_t val) {
    if (state == NULL)
        return 0u;
    if (val > state->load) {
        /* VAL above the cached period means SYSTICK_LOAD changed after init (see
         * the header). The sample cannot be interpreted, so freeze at the last
         * accepted value and report it: a contract violation must never be
         * absorbed, and it must never make the clock run backwards either. */
        ++state->anomalies;
        return (state->wrap * ((uint64_t)state->load + 1u)) + (state->load - state->last);
    }
    if (val > state->last)
        ++state->wrap; /* the down-counter reloaded: a wrap was crossed */
    state->last = val;
    return (state->wrap * ((uint64_t)state->load + 1u)) + (state->load - val);
}

void edge_pal_cortex_m_bare_init(edge_pal_cortex_m_state_t *state) {
    if (state == NULL)
        return;
    state->primask = 0u;
    state->depth = 0u;
    state->wrap = 0u;
    state->host_ticks = 0u;
    state->anomalies = 0u;
    state->load = SYSTICK_LOAD_DEFAULT;
    state->last = SYSTICK_LOAD_DEFAULT; /* the first sample cannot look like a wrap */
#if defined(__arm__)
    /* Takes over SysTick. Excluded with any RTOS that owns the tick - see the
     * header's ownership note. */
    SYSTICK_LOAD = SYSTICK_LOAD_DEFAULT;
    SYSTICK_VAL = 0u;
    SYSTICK_CTRL = 0x5u; /* processor clock, no tick interrupt, enable */
#endif
}

edge_pal_port_t edge_pal_cortex_m_bare_port(edge_pal_cortex_m_state_t *state) {
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
