#ifndef APP_TIMER_PORTS_H
#define APP_TIMER_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct timer_clock_if {
    void *self;
    uint32_t (*get_tick_ms)(void *self);
} timer_clock_if_t;

typedef struct timer_alert_if {
    void *self;
    edge_status_t (*start_alert)(void *self);
    edge_status_t (*stop_alert)(void *self);
} timer_alert_if_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_TIMER_PORTS_H */
