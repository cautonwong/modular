#define _GNU_SOURCE
#include "pal_linux/pal_linux.h"

#include <stddef.h>
#include <time.h>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <sched.h>
#include <unistd.h>

static pal_linux_state_t s_default_linux_state;
static bool s_default_linux_inited = false;

static void ensure_default_inited(void) {
    if (!s_default_linux_inited) {
        pal_linux_init(&s_default_linux_state);
        s_default_linux_inited = true;
    }
}

static pal_linux_state_t *resolve_state(void *self) {
    if (self != NULL) {
        return (pal_linux_state_t *)self;
    }
    ensure_default_inited();
    return &s_default_linux_state;
}

static void critical_enter(void *self) {
    pal_linux_state_t *state = resolve_state(self);
    pthread_mutex_lock(&state->mutex);
    ++state->depth;
}

static void critical_exit(void *self) {
    pal_linux_state_t *state = resolve_state(self);
    if (state->depth > 0u) {
        --state->depth;
    }
    pthread_mutex_unlock(&state->mutex);
}

static void memory_barrier(void *self) {
    (void)self;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static uint64_t monotonic_ticks(void *self) {
    (void)self;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

static bool in_isr(void *self) {
    pal_linux_state_t *state = resolve_state(self);
    return state->in_isr;
}

static void isr_enter(void *self) {
    pal_linux_state_t *state = resolve_state(self);
    state->in_isr = true;
}

static void isr_exit(void *self) {
    pal_linux_state_t *state = resolve_state(self);
    state->in_isr = false;
}

static void idle(void *self) {
    pal_linux_state_t *state = resolve_state(self);
    ++state->idle_waits;
    sched_yield();
}

static void os_yield(void *self) {
    pal_linux_state_t *state = resolve_state(self);
    ++state->yields;
    sched_yield();
}

static void os_sleep_ms(void *self, uint32_t ms) {
    (void)self;
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000u);
    req.tv_nsec = (long)((ms % 1000u) * 1000000uL);
    nanosleep(&req, NULL);
}

void pal_linux_init(pal_linux_state_t *state) {
    if (state == NULL) {
        return;
    }
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&state->mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    state->depth = 0u;
    state->in_isr = false;
    state->yields = 0u;
    state->idle_waits = 0u;
}

void pal_linux_deinit(pal_linux_state_t *state) {
    if (state != NULL) {
        pthread_mutex_destroy(&state->mutex);
    }
}

edge_pal_port_t pal_linux_port(pal_linux_state_t *state) {
    if (state == NULL) {
        ensure_default_inited();
    }
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

edge_os_port_t pal_linux_os_port(pal_linux_state_t *state) {
    if (state == NULL) {
        ensure_default_inited();
    }
    const edge_os_port_t port = {
        .yield = os_yield,
        .sleep_ms = os_sleep_ms,
        .self = state,
    };
    return port;
}

void pal_linux_set_in_isr(pal_linux_state_t *state, bool isr) {
    pal_linux_state_t *s = resolve_state(state);
    s->in_isr = isr;
}

uint32_t pal_linux_critical_depth(pal_linux_state_t *state) {
    pal_linux_state_t *s = resolve_state(state);
    return s->depth;
}

uint32_t pal_linux_yields(pal_linux_state_t *state) {
    pal_linux_state_t *s = resolve_state(state);
    return s->yields;
}

uint32_t pal_linux_idle_waits(pal_linux_state_t *state) {
    pal_linux_state_t *s = resolve_state(state);
    return s->idle_waits;
}

#else

/* Non-POSIX / non-Linux fallback */
static pal_linux_state_t s_default_fallback_state;

static pal_linux_state_t *resolve_state(void *self) {
    return self != NULL ? (pal_linux_state_t *)self : &s_default_fallback_state;
}

static void critical_enter(void *self) {
    pal_linux_state_t *state = resolve_state(self);
    ++state->depth;
}

static void critical_exit(void *self) {
    pal_linux_state_t *state = resolve_state(self);
    if (state->depth > 0u) {
        --state->depth;
    }
}

static void memory_barrier(void *self) {
    (void)self;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static uint64_t monotonic_ticks(void *self) {
    (void)self;
    return 0u;
}

static bool in_isr(void *self) {
    pal_linux_state_t *state = resolve_state(self);
    return state->in_isr;
}

static void isr_enter(void *self) {
    pal_linux_state_t *state = resolve_state(self);
    state->in_isr = true;
}

static void isr_exit(void *self) {
    pal_linux_state_t *state = resolve_state(self);
    state->in_isr = false;
}

static void idle(void *self) {
    pal_linux_state_t *state = resolve_state(self);
    ++state->idle_waits;
}

static void os_yield(void *self) {
    pal_linux_state_t *state = resolve_state(self);
    ++state->yields;
}

static void os_sleep_ms(void *self, uint32_t ms) {
    (void)self;
    (void)ms;
}

void pal_linux_init(pal_linux_state_t *state) {
    if (state != NULL) {
        state->depth = 0u;
        state->in_isr = false;
        state->yields = 0u;
        state->idle_waits = 0u;
    }
}

void pal_linux_deinit(pal_linux_state_t *state) {
    (void)state;
}

edge_pal_port_t pal_linux_port(pal_linux_state_t *state) {
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

edge_os_port_t pal_linux_os_port(pal_linux_state_t *state) {
    const edge_os_port_t port = {
        .yield = os_yield,
        .sleep_ms = os_sleep_ms,
        .self = state,
    };
    return port;
}

void pal_linux_set_in_isr(pal_linux_state_t *state, bool isr) {
    pal_linux_state_t *s = resolve_state(state);
    s->in_isr = isr;
}

uint32_t pal_linux_critical_depth(pal_linux_state_t *state) {
    pal_linux_state_t *s = resolve_state(state);
    return s->depth;
}

uint32_t pal_linux_yields(pal_linux_state_t *state) {
    pal_linux_state_t *s = resolve_state(state);
    return s->yields;
}

uint32_t pal_linux_idle_waits(pal_linux_state_t *state) {
    pal_linux_state_t *s = resolve_state(state);
    return s->idle_waits;
}

#endif
