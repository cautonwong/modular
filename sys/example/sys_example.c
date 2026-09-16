#include "example/sys.h"

edge_status_t sys_example_init(edge_sys_t *sys, edge_module_t **apps,
                               size_t app_count, edge_event_queue_t *events,
                               edge_sys_subscription_t *subscriptions,
                               size_t subscription_capacity)
{
    edge_status_t rc = edge_sys_init(sys, apps, app_count);
    if (rc < 0) return rc;
    return edge_sys_bind_event_queue(sys, events, subscriptions, subscription_capacity);
}
