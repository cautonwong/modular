#include "meter/sys.h"

edge_status_t sys_meter_init(edge_sys_t *sys, edge_module_t **apps, size_t app_count,
                             const uint32_t *required_ids, size_t required_count,
                             edge_event_queue_t *events, edge_sys_subscription_t *subscriptions,
                             size_t subscription_capacity) {
    const edge_sys_config_t config = {
        .apps = apps,
        .app_count = app_count,
        .required_ids = required_ids,
        .required_count = required_count,
        .events = events,
        .subscriptions = subscriptions,
        .subscription_capacity = subscription_capacity,
        .event_budget = SYS_METER_EVENT_BUDGET,
    };
    return edge_sys_configure(sys, &config);
}
