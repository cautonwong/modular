#ifndef SYS_METER_H
#define SYS_METER_H

/*
 * Family: meter.
 *
 * Meter products reconcile metering modules on a fixed cooperative cadence and
 * bound event draining so periodic work cannot be starved by a busy bus. The
 * generic runtime lives in `sys/runtime`; this header re-exports that contract
 * and adds the family's composition entry point.
 */
#include "runtime/sys.h"

#include <stddef.h>
#include <stdint.h>

#define SYS_METER_EVENT_BUDGET 4u

edge_status_t sys_meter_init(edge_sys_t *sys, edge_module_t **apps, size_t app_count,
                             const uint32_t *required_ids, size_t required_count,
                             edge_event_queue_t *events, edge_sys_subscription_t *subscriptions,
                             size_t subscription_capacity);

#endif
