#ifndef PAL_WINDOWS_H
#define PAL_WINDOWS_H

#include "edge/pal.h"
#include "pal_os/os.h"

#include <stdbool.h>
#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Native Windows PAL.
 *
 * Uses QueryPerformanceCounter for high-resolution monotonic ticks,
 * CRITICAL_SECTION for thread-safe nested critical sections, MemoryBarrier()
 * for hardware fences, and SwitchToThread / Sleep for OS yield and sleep.
 */
typedef struct pal_windows_state {
#if defined(_WIN32) || defined(__CYGWIN__)
    CRITICAL_SECTION cs;
    LARGE_INTEGER freq;
    bool freq_init;
#else
    uint32_t dummy_cs;
    uint64_t dummy_ticks;
#endif
    volatile uint32_t depth;
    volatile bool in_isr;
    uint32_t yields;
    uint32_t idle_waits;
} pal_windows_state_t;

/* Initialize Windows PAL state (caller-owned). */
void pal_windows_init(pal_windows_state_t *state);

/* Destroy any OS resources held by the state. */
void pal_windows_deinit(pal_windows_state_t *state);

/* Return the PAL port. If state is NULL, uses a static default instance. */
edge_pal_port_t pal_windows_port(pal_windows_state_t *state);

/* Return the OS port. If state is NULL, uses a static default instance. */
edge_os_port_t pal_windows_os_port(pal_windows_state_t *state);

/* Test controls & telemetry inspection. */
void pal_windows_set_in_isr(pal_windows_state_t *state, bool in_isr);
uint32_t pal_windows_critical_depth(pal_windows_state_t *state);
uint32_t pal_windows_yields(pal_windows_state_t *state);
uint32_t pal_windows_idle_waits(pal_windows_state_t *state);
void pal_windows_set_ticks(pal_windows_state_t *state, uint64_t ticks);

#ifdef __cplusplus
}
#endif

#endif /* PAL_WINDOWS_H */
