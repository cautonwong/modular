#ifndef APP_METRONOME_PORTS_H
#define APP_METRONOME_PORTS_H

#include "edge/errors.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct metronome_motor_if {
    void *self;
    edge_status_t (*run_duration_ms)(void *self, uint32_t duration_ms);
} metronome_motor_if_t;

typedef struct metronome_clock_if {
    void *self;
    uint32_t (*get_tick_ms)(void *self);
} metronome_clock_if_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_METRONOME_PORTS_H */
