#ifndef PAL_OS_POWER_H
#define PAL_OS_POWER_H

#include "edge/errors.h"
#include "edge/pal.h"
#include "pal_os/idle.h"
#include "pal_os/os.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Power modes (D28/D47/D52/D71/Tickless):
 * 0: ACTIVE - CPU running full speed.
 * 1: IDLE   - CPU stopped via WFI; peripheral clocks and main oscillator remain running. Zero wake
 * latency. 2: STOP   - Deep sleep; high-frequency oscillators stopped, SRAM and registers retained.
 *             Wake by external GPIO/Tamper interrupt or low-power timer / RTC.
 * 3: STANDBY- Ultra low power; RAM lost (or backup only), longest wake latency.
 */
typedef enum edge_pm_mode {
    EDGE_PM_ACTIVE = 0,
    EDGE_PM_IDLE = 1,
    EDGE_PM_STOP = 2,
    EDGE_PM_STANDBY = 3,
    EDGE_PM_MODE_COUNT = 4
} edge_pm_mode_t;

/*
 * Power management state with reference-counted mode locks (Wake Locks)
 * and threshold configurations.
 */
typedef struct edge_pm_state {
    uint32_t lock_counts[EDGE_PM_MODE_COUNT];
    uint64_t stop_threshold_ticks;
    uint64_t standby_threshold_ticks;
} edge_pm_state_t;

/* Initialize power management state with default thresholds. */
edge_status_t edge_pm_init(edge_pm_state_t *pm, uint64_t stop_threshold_ticks,
                           uint64_t standby_threshold_ticks);

/*
 * Acquire a power lock (constrain sleep mode).
 * e.g., locking EDGE_PM_IDLE prevents the system from entering EDGE_PM_STOP or STANDBY.
 */
edge_status_t edge_pm_lock(edge_pm_state_t *pm, edge_pm_mode_t mode);

/* Release a previously acquired power lock. */
edge_status_t edge_pm_unlock(edge_pm_state_t *pm, edge_pm_mode_t mode);

/* Compute the deepest allowable power mode given expected idle duration and active locks. */
edge_pm_mode_t edge_pm_target_mode(const edge_pm_state_t *pm, uint64_t idle_ticks);

/* Board low-power entry hook signature. */
typedef edge_status_t (*edge_board_pm_enter_fn)(edge_pm_mode_t mode, uint64_t idle_ticks,
                                                void *ctx);

/*
 * Atomic Power Management & Tickless Execution:
 * Evaluates target mode, enters critical section, checks `pending` predicate,
 * calls `board_pm` hook (or fallback WFI), and exits critical section.
 */
edge_status_t edge_pm_execute(const edge_pm_state_t *pm, uint64_t idle_ticks,
                              const edge_pal_port_t *pal, edge_board_pm_enter_fn board_pm,
                              void *board_ctx, edge_os_pending_fn pending, void *pending_ctx);

#ifdef __cplusplus
}
#endif

#endif
