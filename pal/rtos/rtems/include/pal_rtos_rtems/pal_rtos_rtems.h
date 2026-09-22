#ifndef PAL_RTOS_RTEMS_H
#define PAL_RTOS_RTEMS_H

#include "edge/event.h"
#include "edge/pal.h"
#include "pal_os/tick64.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct edge_rtos_pal_state {
    edge_tick64_t tick;
    uint32_t lock_key;
    uint32_t lock_depth;
} edge_rtos_pal_state_t;

void edge_rtos_pal_init(edge_rtos_pal_state_t *state);
edge_pal_port_t edge_rtos_pal_port(edge_rtos_pal_state_t *state);
edge_irq_guard_t edge_rtos_irq_guard(void);

#ifdef __cplusplus
}
#endif

#endif /* PAL_RTOS_RTEMS_H */
