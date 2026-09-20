#ifndef PAL_OS_TICK64_H
#define PAL_OS_TICK64_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 32-bit tick -> 64-bit monotonic extension.
 *
 * A 32-bit kernel tick wraps. At 100 Hz that is every 497 days; a water meter or
 * a watch runs for years, so the scheduler clock must not jump backwards. This
 * accumulator extends it without ever doing so.
 *
 * Two different wrap problems, deliberately solved in two different places:
 *   - comparing a *deadline* to now  -> D72's modular compare,
 *     `(int32_t)(now - due) >= 0` (see sys/runtime `tick_due`).
 *   - extending a *counter* across a wrap -> the unsigned `now < last` test
 *     here, which is the standard once-per-half-period wrap detection.
 * Mixing them up is the classic way to get a clock that runs backwards.
 *
 * Ceiling (by construction): the caller must sample at least once per 2^31
 * ticks, otherwise a wrap cannot be told from a large forward step. At 100 Hz
 * that is a 248-day budget between samples; every sys iteration samples, so the
 * real ceiling is a tickless sleep longer than that (see docs/low-power.md).
 *
 * Pure arithmetic over caller-owned state: no globals, so the caller decides the
 * locking. A caller reachable from both task and interrupt context must mask.
 */
typedef struct edge_tick64 {
    uint32_t last;
    uint32_t wraps;
} edge_tick64_t;

void edge_tick64_reset(edge_tick64_t *state);

/* Extends `now` into the monotonic 64-bit timeline. Never returns a value lower
 * than a previous call's (given the sampling ceiling above). */
uint64_t edge_tick64_extend(edge_tick64_t *state, uint32_t now);

#ifdef __cplusplus
}
#endif

#endif
