#ifndef EDGE_SYS_H
#define EDGE_SYS_H

#include "event.h"
#include "module.h"
#include <stddef.h>
#include <stdint.h>

typedef struct edge_sys_subscription {
    uint32_t event_id;
    edge_module_t *app;
} edge_sys_subscription_t;

typedef struct edge_sys {
    edge_module_t **apps;
    size_t app_count;
    edge_sys_subscription_t *subscriptions;
    size_t subscription_count;
    size_t subscription_capacity;
    edge_event_queue_t *events;
    uint32_t tick;
} edge_sys_t;

int edge_sys_init(edge_sys_t *sys, edge_module_t **apps, size_t count);
int edge_sys_bind_event_queue(edge_sys_t *sys, edge_event_queue_t *queue,
                              edge_sys_subscription_t *subscriptions,
                              size_t capacity);
int edge_sys_subscribe(edge_sys_t *sys, uint32_t event_id, edge_module_t *app);
int edge_sys_dispatch_events(edge_sys_t *sys);
int edge_sys_validate_required(const edge_sys_t *sys, const uint32_t *required_ids, size_t required_count);
int edge_sys_run_once(edge_sys_t *sys);
int edge_sys_power_off(edge_sys_t *sys);

#endif
