#ifndef APP_STOPWATCH_PORTS_H
#define APP_STOPWATCH_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Consumer-defined port for reading high-resolution system millisecond timestamp */
typedef struct stopwatch_clock_if {
    void *self;
    uint32_t (*get_tick_ms)(void *self);
} stopwatch_clock_if_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_STOPWATCH_PORTS_H */
