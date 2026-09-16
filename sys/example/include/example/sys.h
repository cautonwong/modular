#ifndef SYS_EXAMPLE_H
#define SYS_EXAMPLE_H

/*
 * Family: example.
 *
 * The generic foreground runtime lives in `sys/runtime`; this family header
 * re-exports that contract and adds the family's composition entry point so
 * products keep a single `<family>/sys.h` include.
 */
#include "runtime/sys.h"

#include <stddef.h>

edge_status_t sys_example_init(edge_sys_t *sys, edge_module_t **apps, size_t app_count,
                               edge_event_queue_t *events, edge_sys_subscription_t *subscriptions,
                               size_t subscription_capacity);

#endif
