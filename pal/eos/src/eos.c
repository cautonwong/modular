#include "pal_eos/eos.h"

#include <stddef.h>

static uint64_t now_ticks(const edge_eos_t *eos) {
    if (eos->clock != NULL && eos->clock->monotonic_ticks != NULL)
        return eos->clock->monotonic_ticks(eos->clock->self);
    return 0u;
}

/*
 * D72's modular compare, for the same reason sys uses it: a deadline is compared
 * with subtraction so a counter wrap does not read as "not due yet".
 */
static bool due(uint64_t now, const edge_eos_task_t *task) {
    if (task->period_ticks == 0u)
        return true;
    return (int64_t)(now - task->next_due) >= 0;
}

edge_status_t edge_eos_init(edge_eos_t *eos, const edge_clock_port_t *clock) {
    if (eos == NULL)
        return EDGE_EINVAL;
    for (uint32_t i = 0u; i < EDGE_EOS_MAX_TASKS; ++i) {
        eos->tasks[i].fn = NULL;
        eos->tasks[i].arg = NULL;
        eos->tasks[i].name = NULL;
        eos->tasks[i].priority = 0u;
        eos->tasks[i].period_ticks = 0u;
        eos->tasks[i].next_due = 0u;
        eos->tasks[i].runs = 0u;
    }
    eos->count = 0u;
    eos->clock = clock;
    eos->running = false;
    eos->rounds = 0u;
    eos->idle_rounds = 0u;
    eos->dispatches = 0u;
    return EDGE_OK;
}

edge_status_t edge_eos_task_add(edge_eos_t *eos, const char *name, edge_rtos_task_fn fn, void *arg,
                                uint32_t priority, uint32_t period_ticks) {
    if (eos == NULL || fn == NULL)
        return EDGE_EINVAL;
    if (priority >= EDGE_EOS_MAX_PRIORITIES)
        return EDGE_EINVAL;
    if (eos->count >= EDGE_EOS_MAX_TASKS)
        return EDGE_ENOSPC;
    edge_eos_task_t *task = &eos->tasks[eos->count];
    task->fn = fn;
    task->arg = arg;
    task->name = name;
    task->priority = priority;
    task->period_ticks = period_ticks;
    /* First deadline is the next tick, so a periodic task does not run in the
     * round that registered it. */
    task->next_due = now_ticks(eos) + (period_ticks != 0u ? period_ticks : 1u);
    task->runs = 0u;
    ++eos->count;
    return EDGE_OK;
}

uint32_t edge_eos_run_once(edge_eos_t *eos) {
    if (eos == NULL)
        return 0u;
    const uint64_t now = now_ticks(eos);
    ++eos->rounds;

    uint32_t ran = 0u;
    for (uint32_t prio = 0u; prio < EDGE_EOS_MAX_PRIORITIES; ++prio) {
        for (uint32_t i = 0u; i < eos->count; ++i) {
            edge_eos_task_t *task = &eos->tasks[i];
            if (task->fn == NULL || task->priority != prio)
                continue;
            if (!due(now, task))
                continue;
            /*
             * One round per due task, then the deadline moves to `now + period`:
             * a task that missed several periods runs once, not once per missed
             * period. Work per round is therefore bounded by the task count, and a
             * high-priority task cannot consume more than its own round - which is
             * why a lower-priority task is delayed but never starved
             * (tests/test_eos.c asserts it).
             */
            task->next_due = now + (task->period_ticks != 0u ? task->period_ticks : 1u);
            ++task->runs;
            ++eos->dispatches;
            ++ran;
            task->fn(task->arg);
        }
    }
    if (ran == 0u)
        ++eos->idle_rounds;
    return ran;
}

void edge_eos_run(edge_eos_t *eos) {
    if (eos == NULL)
        return;
    eos->running = true;
    while (eos->running)
        (void)edge_eos_run_once(eos);
}

void edge_eos_stop(edge_eos_t *eos) {
    if (eos != NULL)
        eos->running = false;
}

bool edge_eos_running(const edge_eos_t *eos) {
    return eos != NULL && eos->running;
}
