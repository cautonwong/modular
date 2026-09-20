#include "pal_host/host.h"

#include <stddef.h>

static uint64_t g_ticks;
static uint32_t g_critical_depth;
static bool g_in_isr;
static uint32_t g_yields;
static uint64_t g_slept_ms;
static uint32_t g_idle_waits;

static void critical_enter(void *self) {
    (void)self;
    ++g_critical_depth;
}

static void critical_exit(void *self) {
    (void)self;
    if (g_critical_depth > 0u)
        --g_critical_depth;
}

static void memory_barrier(void *self) {
    (void)self;
}

static uint64_t monotonic_ticks(void *self) {
    (void)self;
    return ++g_ticks;
}

static bool in_isr(void *self) {
    (void)self;
    return g_in_isr;
}

static void isr_enter(void *self) {
    (void)self;
    g_in_isr = true;
}

static void isr_exit(void *self) {
    (void)self;
    g_in_isr = false;
}

static void idle(void *self) {
    (void)self;
    ++g_idle_waits;
}

edge_pal_port_t pal_host_port(void) {
    const edge_pal_port_t port = {
        .critical_enter = critical_enter,
        .critical_exit = critical_exit,
        .memory_barrier = memory_barrier,
        .monotonic_ticks = monotonic_ticks,
        .in_isr = in_isr,
        .isr_enter = isr_enter,
        .isr_exit = isr_exit,
        .idle = idle,
        .self = NULL,
    };
    return port;
}

void pal_host_set_ticks(uint64_t ticks) {
    g_ticks = ticks;
}

uint32_t pal_host_critical_depth(void) {
    return g_critical_depth;
}

void pal_host_set_in_isr(bool in_isr) {
    g_in_isr = in_isr;
}

static void os_yield(void *self) {
    (void)self;
    ++g_yields;
}

static void os_sleep_ms(void *self, uint32_t ms) {
    (void)self;
    g_slept_ms += ms;
}

edge_os_port_t pal_host_os_port(void) {
    const edge_os_port_t port = {
        .yield = os_yield,
        .sleep_ms = os_sleep_ms,
        .self = NULL,
    };
    return port;
}

uint32_t pal_host_yields(void) {
    return g_yields;
}

uint64_t pal_host_slept_ms(void) {
    return g_slept_ms;
}

uint32_t pal_host_idle_waits(void) {
    return g_idle_waits;
}
