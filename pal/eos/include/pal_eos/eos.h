#ifndef PAL_EOS_H
#define PAL_EOS_H

#include "edge/clock.h"
#include "edge/errors.h"
#include "pal_rtos/rtos.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * EOS: the first-party runtime (D46/D85), a sibling of `pal/rtos/freertos` rather
 * than a wrapper around it. Both implement the same neutral contract, which is
 * what makes the contract a contract instead of a description of one kernel.
 *
 * This core is a **static, zero-allocation, fixed-rate priority executive**: every
 * task runs one bounded round and returns, and the executive decides what runs
 * next. That is the shape products already run in here (`edge_sys_run_once()`
 * returns, the product loops), so it needs no assembly, no per-task stack and no
 * context switch - and it is therefore fully host-testable, which a priority
 * kernel written in assembly is not.
 *
 * Priority direction is pinned HERE, because the neutral contract deliberately
 * does not pin it (a gap the ThreadX review found independently): **0 is the
 * highest priority**. Lower numbers run first within a round.
 *
 * Deliberately absent:
 *   - no preemption: no PendSV context switch and no per-task stacks, so there is
 *     no context-switch latency to report either;
 *   - no MPU, no privilege separation, no mutual exclusion primitives;
 *   - no certification, and nothing here may be described as certified. A
 *     certificate is issued to a product and its evidence chain, not to a design
 *     (see docs/eos.md, "non-claims").
 */

#define EDGE_EOS_MAX_TASKS 8u
#define EDGE_EOS_MAX_PRIORITIES 4u

typedef struct edge_eos_task {
    edge_rtos_task_fn fn; /* one bounded round, then returns */
    void *arg;
    const char *name;
    uint32_t priority;     /* 0 = highest */
    uint32_t period_ticks; /* 0 = every round */
    uint64_t next_due;
    uint32_t runs;
} edge_eos_task_t;

typedef struct edge_eos {
    edge_eos_task_t tasks[EDGE_EOS_MAX_TASKS];
    uint32_t count;
    const edge_clock_port_t *clock;
    bool running;
    /*
     * Metrics, so an EOS baseline can be re-measured rather than described.
     * `dispatches` is task dispatches, NOT context switches: this core has none,
     * and naming the counter after a mechanism it does not have is how a metric
     * turns into a false claim.
     */
    uint32_t rounds;
    uint32_t idle_rounds;
    uint32_t dispatches;
} edge_eos_t;

/* Caller-owned storage (D21): the executive lives in the caller's struct. */
edge_status_t edge_eos_init(edge_eos_t *eos, const edge_clock_port_t *clock);

/* `priority` is 0..EDGE_EOS_MAX_PRIORITIES-1 (0 highest); a full table is
 * EDGE_ENOSPC and an out-of-range priority is EDGE_EINVAL. */
edge_status_t edge_eos_task_add(edge_eos_t *eos, const char *name, edge_rtos_task_fn fn, void *arg,
                                uint32_t priority, uint32_t period_ticks);

/* Runs every due task exactly once, in priority order. Returns how many ran;
 * 0 means the round was idle. */
uint32_t edge_eos_run_once(edge_eos_t *eos);

/* The edge_rtos_start() analogue. Does not return until edge_eos_stop(). */
void edge_eos_run(edge_eos_t *eos);
void edge_eos_stop(edge_eos_t *eos);
bool edge_eos_running(const edge_eos_t *eos);

#ifdef __cplusplus
}
#endif

#endif
