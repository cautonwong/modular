#ifndef SYS_RUNTIME_H
#define SYS_RUNTIME_H

#include "edge/clock.h"
#include "edge/event.h"
#include "edge/module.h"
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
} edge_sys_stats_t;

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
} edge_sys_t;

edge_status_t edge_sys_init(edge_sys_t *sys, edge_module_t **apps, size_t count);
edge_status_t edge_sys_bind_event_queue(edge_sys_t *sys, edge_event_queue_t *queue,
                                        edge_sys_subscription_t *subscriptions, size_t capacity);
edge_status_t edge_sys_set_clock(edge_sys_t *sys, const edge_clock_port_t *clock);
edge_status_t edge_sys_set_event_budget(edge_sys_t *sys, uint32_t max_events_per_run);
edge_status_t edge_sys_set_required(edge_sys_t *sys, const uint32_t *ids, size_t count);
edge_status_t edge_sys_subscribe(edge_sys_t *sys, uint32_t event_id, edge_module_t *app);
edge_status_t edge_sys_validate_required(const edge_sys_t *sys);
edge_status_t edge_sys_start(edge_sys_t *sys);
edge_status_t edge_sys_dispatch_events(edge_sys_t *sys);
edge_status_t edge_sys_run_once(edge_sys_t *sys);
edge_status_t edge_sys_power_off(edge_sys_t *sys);
edge_status_t edge_sys_deinit(edge_sys_t *sys);
edge_status_t edge_sys_stats_get(const edge_sys_t *sys, edge_sys_stats_t *out);

#endif
