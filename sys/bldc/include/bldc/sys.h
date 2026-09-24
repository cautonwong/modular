#ifndef SYS_BLDC_H
#define SYS_BLDC_H

#include "runtime/sys.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

edge_status_t sys_bldc_init(edge_sys_t *sys, edge_module_t **apps, size_t app_count,
                            edge_event_queue_t *events, edge_sys_subscription_t *subscriptions,
                            size_t subscription_capacity);

#ifdef __cplusplus
}
#endif

#endif /* SYS_BLDC_H */
