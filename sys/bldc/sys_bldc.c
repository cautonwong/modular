#include "bldc/sys.h"

edge_status_t sys_bldc_init(edge_sys_t *sys, edge_module_t **apps, size_t app_count,
                            edge_event_queue_t *events, edge_sys_subscription_t *subscriptions,
                            size_t subscription_capacity) {
    const edge_sys_config_t config = {
        .apps = apps,
        .app_count = app_count,
        .events = events,
        .subscriptions = subscriptions,
        .subscription_capacity = subscription_capacity,
    };
    return edge_sys_configure(sys, &config);
}
