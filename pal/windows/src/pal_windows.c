#include "pal_windows/pal_windows.h"

#include <stddef.h>

#if defined(_WIN32) || defined(__CYGWIN__)

static pal_windows_state_t s_default_win_state;
static bool s_default_win_inited = false;

static void ensure_default_inited(void) {
    if (!s_default_win_inited) {
        pal_windows_init(&s_default_win_state);
        s_default_win_inited = true;
    }
}

static pal_windows_state_t *resolve_state(void *self) {
    if (self != NULL) {
        return (pal_windows_state_t *)self;
    }
    ensure_default_inited();
    return &s_default_win_state;
}

static void critical_enter(void *self) {
    pal_windows_state_t *state = resolve_state(self);
    EnterCriticalSection(&state->cs);
    ++state->depth;
}

static void critical_exit(void *self) {
    pal_windows_state_t *state = resolve_state(self);
    if (state->depth > 0u) {
        --state->depth;
    }
    LeaveCriticalSection(&state->cs);
}

static void memory_barrier(void *self) {
    (void)self;
    MemoryBarrier();
}

static uint64_t monotonic_ticks(void *self) {
    pal_windows_state_t *state = resolve_state(self);
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    if (!state->freq_init || state->freq.QuadPart == 0) {
        QueryPerformanceFrequency(&state->freq);
        state->freq_init = true;
    }
    if (state->freq.QuadPart == 0) {
        return (uint64_t)counter.QuadPart;
    }
    return (uint64_t)((counter.QuadPart * 1000000000LL) / state->freq.QuadPart);
}

static bool in_isr(void *self) {
    pal_windows_state_t *state = resolve_state(self);
    return state->in_isr;
}

static void isr_enter(void *self) {
    pal_windows_state_t *state = resolve_state(self);
    state->in_isr = true;
}

static void isr_exit(void *self) {
    pal_windows_state_t *state = resolve_state(self);
    state->in_isr = false;
}

static void idle(void *self) {
    pal_windows_state_t *state = resolve_state(self);
    ++state->idle_waits;
    Sleep(0);
}

static void os_yield(void *self) {
    pal_windows_state_t *state = resolve_state(self);
    ++state->yields;
    SwitchToThread();
}

static void os_sleep_ms(void *self, uint32_t ms) {
    (void)self;
    Sleep(ms);
}

void pal_windows_init(pal_windows_state_t *state) {
    if (state == NULL) {
        return;
    }
    InitializeCriticalSection(&state->cs);
    QueryPerformanceFrequency(&state->freq);
    state->freq_init = true;
    state->depth = 0u;
    state->in_isr = false;
    state->yields = 0u;
    state->idle_waits = 0u;
}

void pal_windows_deinit(pal_windows_state_t *state) {
    if (state != NULL) {
        DeleteCriticalSection(&state->cs);
    }
}

edge_pal_port_t pal_windows_port(pal_windows_state_t *state) {
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

edge_os_port_t pal_windows_os_port(pal_windows_state_t *state) {
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

void pal_windows_set_in_isr(pal_windows_state_t *state, bool isr) {
    pal_windows_state_t *s = resolve_state(state);
    s->in_isr = isr;
}

uint32_t pal_windows_critical_depth(pal_windows_state_t *state) {
    pal_windows_state_t *s = resolve_state(state);
    return s->depth;
}

uint32_t pal_windows_yields(pal_windows_state_t *state) {
    pal_windows_state_t *s = resolve_state(state);
    return s->yields;
}

uint32_t pal_windows_idle_waits(pal_windows_state_t *state) {
    pal_windows_state_t *s = resolve_state(state);
    return s->idle_waits;
}

void pal_windows_set_ticks(pal_windows_state_t *state, uint64_t ticks) {
    (void)state;
    (void)ticks;
}

#else

/* Host test fallback for non-Windows platforms (e.g. Linux CI/test environments) */
static pal_windows_state_t s_default_win_fallback_state;

static pal_windows_state_t *resolve_state(void *self) {
    return self != NULL ? (pal_windows_state_t *)self : &s_default_win_fallback_state;
}

static void critical_enter(void *self) {
    pal_windows_state_t *state = resolve_state(self);
    ++state->depth;
}

static void critical_exit(void *self) {
    pal_windows_state_t *state = resolve_state(self);
    if (state->depth > 0u) {
        --state->depth;
    }
}

static void memory_barrier(void *self) {
    (void)self;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static uint64_t monotonic_ticks(void *self) {
    /* cppcheck-suppress unreadVariable */
    pal_windows_state_t *state = resolve_state(self);
    state->dummy_ticks += 1u;
    return state->dummy_ticks;
}

static bool in_isr(void *self) {
    pal_windows_state_t *state = resolve_state(self);
    return state->in_isr;
}

static void isr_enter(void *self) {
    pal_windows_state_t *state = resolve_state(self);
    state->in_isr = true;
}

static void isr_exit(void *self) {
    pal_windows_state_t *state = resolve_state(self);
    state->in_isr = false;
}

static void idle(void *self) {
    pal_windows_state_t *state = resolve_state(self);
    ++state->idle_waits;
}

static void os_yield(void *self) {
    pal_windows_state_t *state = resolve_state(self);
    ++state->yields;
}

static void os_sleep_ms(void *self, uint32_t ms) {
    (void)self;
    (void)ms;
}

void pal_windows_init(pal_windows_state_t *state) {
    if (state != NULL) {
        state->dummy_cs = 0u;
        state->dummy_ticks = 0u;
        state->depth = 0u;
        state->in_isr = false;
        state->yields = 0u;
        state->idle_waits = 0u;
    }
}

void pal_windows_deinit(pal_windows_state_t *state) {
    (void)state;
}

edge_pal_port_t pal_windows_port(pal_windows_state_t *state) {
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

edge_os_port_t pal_windows_os_port(pal_windows_state_t *state) {
    const edge_os_port_t port = {
        .yield = os_yield,
        .sleep_ms = os_sleep_ms,
        .self = state,
    };
    return port;
}

void pal_windows_set_in_isr(pal_windows_state_t *state, bool isr) {
    pal_windows_state_t *s = resolve_state(state);
    s->in_isr = isr;
}

uint32_t pal_windows_critical_depth(pal_windows_state_t *state) {
    pal_windows_state_t *s = resolve_state(state);
    return s->depth;
}

uint32_t pal_windows_yields(pal_windows_state_t *state) {
    pal_windows_state_t *s = resolve_state(state);
    return s->yields;
}

uint32_t pal_windows_idle_waits(pal_windows_state_t *state) {
    pal_windows_state_t *s = resolve_state(state);
    return s->idle_waits;
}

void pal_windows_set_ticks(pal_windows_state_t *state, uint64_t ticks) {
    pal_windows_state_t *s = resolve_state(state);
    s->dummy_ticks = ticks;
}

#endif
