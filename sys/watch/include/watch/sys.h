#ifndef SYS_WATCH_H
#define SYS_WATCH_H

#include "runtime/sys.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Family: watch.
 *
 * Smartwatch products reconcile touch, sensor, BLE, and power state machines.
 * The generic runtime lives in `sys/runtime`; this header re-exports that contract
 * and adds the family's composition entry point.
 */
#define SYS_WATCH_EVENT_BUDGET 8u

edge_status_t sys_watch_init(edge_sys_t *sys, edge_module_t **apps, size_t app_count,
                             const uint32_t *required_ids, size_t required_count,
                             edge_event_queue_t *events, edge_sys_subscription_t *subscriptions,
                             size_t subscription_capacity);

#ifdef __cplusplus
}
#endif

#endif /* SYS_WATCH_H */
