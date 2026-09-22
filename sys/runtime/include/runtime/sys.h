#ifndef SYS_RUNTIME_H
#define SYS_RUNTIME_H

#include "edge/clock.h"
#include "edge/event.h"
#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum edge_sys_state {
    EDGE_SYS_CONSTRUCTED = 0,
    EDGE_SYS_RUNNING = 1,
    EDGE_SYS_STOPPED = 2,
    EDGE_SYS_FAILED = 3
} edge_sys_state_t;

typedef struct edge_sys_subscription {
    uint32_t event_id;
    edge_module_t *app;
} edge_sys_subscription_t;

typedef struct edge_sys_stats {
    uint32_t polls;
    uint32_t events;
    uint32_t drops;
    uint32_t errors;
    uint32_t budget_hits;
    uint32_t isolated;
    uint32_t idle_calls;
    uint32_t pending_high_water;
} edge_sys_stats_t;

/* Idle hook: called from the runner when a step had no event and no poll (D52). */
typedef void (*edge_sys_idle_fn)(void *ctx);

typedef struct edge_sys {
    edge_module_t **apps;
    size_t app_count;
    edge_sys_subscription_t *subscriptions;
    size_t subscription_count;
    size_t subscription_capacity;
    const uint32_t *required_ids;
    size_t required_count;
    edge_event_queue_t *events;
    const edge_clock_port_t *clock;
    uint32_t max_events_per_run;
    uint64_t tick;
    edge_sys_stats_t stats;
    edge_sys_state_t state;
    edge_event_t *pending;
    uint32_t pending_capacity;
    uint32_t pending_head;
    uint32_t pending_tail;
    uint32_t pending_count;
    edge_sys_idle_fn idle_hook;
    void *idle_ctx;
} edge_sys_t;

edge_status_t edge_sys_init(edge_sys_t *sys, edge_module_t **apps, size_t count);
edge_status_t edge_sys_bind_event_queue(edge_sys_t *sys, edge_event_queue_t *queue,
                                        edge_sys_subscription_t *subscriptions, size_t capacity);
/* Runner-owned deferred queue for `edge_sys_publish()` (D70). */
edge_status_t edge_sys_bind_pending_queue(edge_sys_t *sys, edge_event_t *storage,
                                          uint32_t capacity);
edge_status_t edge_sys_set_clock(edge_sys_t *sys, const edge_clock_port_t *clock);
edge_status_t edge_sys_set_event_budget(edge_sys_t *sys, uint32_t max_events_per_run);
/*
 * The list is **borrowed, not copied**: `ids` must outlive the sys - a `static
 * const` table, not a caller's stack frame - because `edge_sys_validate_required()`
 * dereferences it on every run. A product that passes a local array is relying on
 * its stack frame surviving the handover, which no contract here promises.
 */
edge_status_t edge_sys_set_required(edge_sys_t *sys, const uint32_t *ids, size_t count);
edge_status_t edge_sys_set_idle(edge_sys_t *sys, edge_sys_idle_fn hook, void *ctx);
edge_status_t edge_sys_subscribe(edge_sys_t *sys, uint32_t event_id, edge_module_t *app);
edge_status_t edge_sys_unsubscribe(edge_sys_t *sys, uint32_t event_id, const edge_module_t *app);
/* Publish a fact from the runner context; deferred into the pending queue (D70). */
edge_status_t edge_sys_publish(edge_sys_t *sys, const edge_event_t *event);
edge_status_t edge_sys_validate_required(const edge_sys_t *sys);
edge_status_t edge_sys_start(edge_sys_t *sys);
edge_status_t edge_sys_dispatch_events(edge_sys_t *sys);
edge_status_t edge_sys_run_once(edge_sys_t *sys);
/* Decomposable runner step (D47); `run_once` is kept as the historical name. */
edge_status_t edge_sys_step(edge_sys_t *sys);
/* Bare-metal loop: `while (state == RUNNING) step()` (D47/D52). */
edge_status_t edge_sys_run(edge_sys_t *sys);
edge_status_t edge_sys_idle(edge_sys_t *sys);
edge_status_t edge_sys_suspend_all(edge_sys_t *sys);
edge_status_t edge_sys_resume_all(edge_sys_t *sys);
edge_status_t edge_sys_power_off(edge_sys_t *sys);
edge_status_t edge_sys_deinit(edge_sys_t *sys);
edge_status_t edge_sys_stats_get(const edge_sys_t *sys, edge_sys_stats_t *out);
edge_status_t edge_sys_stats_reset(edge_sys_t *sys);

/* Watchdog policy input (D9/D71): false once any module has failed. */
bool edge_sys_healthy(const edge_sys_t *sys);
/* True when an event or a due poll is waiting; the atomic idle re-check (D71). */
bool edge_sys_pending(const edge_sys_t *sys);

#endif
