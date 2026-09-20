#include "pal_cortex_m/pal_cortex_m.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__arm__)

#define SYSTICK_CTRL (*(volatile uint32_t *)0xe000e010u)
#define SYSTICK_LOAD (*(volatile uint32_t *)0xe000e014u)
#define SYSTICK_VAL (*(volatile uint32_t *)0xe000e018u)
#define SYSTICK_COUNTFLAG (1u << 16)

static uint32_t primask_read(void) {
    uint32_t primask = 0u;
    __asm volatile("mrs %0, primask" : "=r"(primask));
    return primask;
}

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

static uint64_t monotonic_ticks(void *self) {
    edge_pal_cortex_m_state_t *state = (edge_pal_cortex_m_state_t *)self;
    if (state == NULL)
        return 0u;
    if ((SYSTICK_CTRL & SYSTICK_COUNTFLAG) != 0u)
        ++state->wrap;
    const uint32_t load = SYSTICK_LOAD;
    const uint32_t elapsed = load - SYSTICK_VAL;
    return (state->wrap * ((uint64_t)load + 1u)) + elapsed;
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

static void idle(void *self) {
    (void)self;
    __asm volatile("wfi" ::: "memory");
}

#else /* host fallback: keep the target analyzable and host-testable */

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

static void idle(void *self) {
    (void)self;
}

#endif

void edge_pal_cortex_m_bare_init(edge_pal_cortex_m_state_t *state) {
    if (state == NULL)
        return;
    state->primask = 0u;
    state->depth = 0u;
    state->wrap = 0u;
    state->host_ticks = 0u;
#if defined(__arm__)
    SYSTICK_LOAD = 0x00FFFFFFu;
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
