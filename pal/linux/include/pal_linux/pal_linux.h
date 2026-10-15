#ifndef PAL_LINUX_H
#define PAL_LINUX_H

#include "edge/pal.h"
#include "pal_os/os.h"

#include <stdbool.h>
#include <stdint.h>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Native Linux PAL.
 *
 * Uses clock_gettime(CLOCK_MONOTONIC) for nanosecond monotonic ticks,
 * recursive pthread mutex for critical sections, atomic fences for barriers,
 * and sched_yield / nanosleep for OS yield and sleep.
 */
typedef struct pal_linux_state {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    pthread_mutex_t mutex;
#else
    uint32_t dummy_mutex;
#endif
    volatile uint32_t depth;
    volatile bool in_isr;
    uint32_t yields;
    uint32_t idle_waits;
} pal_linux_state_t;

/* Initialize the Linux PAL state (caller-owned). */
void pal_linux_init(pal_linux_state_t *state);

/* Destroy any OS resources held by the state. */
void pal_linux_deinit(pal_linux_state_t *state);

/* Return the PAL port. If state is NULL, uses a static default instance. */
edge_pal_port_t pal_linux_port(pal_linux_state_t *state);

/* Return the OS port. If state is NULL, uses a static default instance. */
edge_os_port_t pal_linux_os_port(pal_linux_state_t *state);

/* Test controls & telemetry inspection. */
void pal_linux_set_in_isr(pal_linux_state_t *state, bool in_isr);
uint32_t pal_linux_critical_depth(pal_linux_state_t *state);
uint32_t pal_linux_yields(pal_linux_state_t *state);
uint32_t pal_linux_idle_waits(pal_linux_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* PAL_LINUX_H */
