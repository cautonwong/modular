#ifndef APP_WATCH_TIME_PORTS_H
#define APP_WATCH_TIME_PORTS_H

#include "edge/errors.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rtc_clock_if {
    edge_status_t (*get_counter)(void *self, uint32_t *out_ticks);
    uint32_t (*get_tick_frequency)(void *self);
    void *self;
} rtc_clock_if_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_WATCH_TIME_PORTS_H */
