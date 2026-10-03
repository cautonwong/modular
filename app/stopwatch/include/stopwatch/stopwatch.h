#ifndef APP_STOPWATCH_H
#define APP_STOPWATCH_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "stopwatch/ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STOPWATCH_HIST_SIZE 4u
#define STOPWATCH_LAP_BOUNDARY 1000u
#define STOPWATCH_MAX_HOURS_MS (3600000UL * 1000UL) /* 1000 hours in ms */

typedef enum stopwatch_state {
    STOPWATCH_CLEARED = 0,
    STOPWATCH_RUNNING = 1,
    STOPWATCH_PAUSED = 2,
} stopwatch_state_t;

typedef struct stopwatch_lap {
    uint16_t number;
    uint32_t time_since_start_ms;
} stopwatch_lap_t;

typedef struct stopwatch_app {
    edge_module_t module;
    const stopwatch_clock_if_t *clock;

    stopwatch_state_t state;
    uint32_t start_time_ms;
    uint32_t time_elapsed_previously_ms;

    stopwatch_lap_t history[STOPWATCH_HIST_SIZE];
    uint16_t max_lap_number;
    uint8_t lap_count;
} stopwatch_app_t;

void stopwatch_construct(stopwatch_app_t *self, uint32_t module_id, uint32_t priority,
                         const stopwatch_clock_if_t *clock);

edge_status_t stopwatch_init(stopwatch_app_t *self);
edge_status_t stopwatch_shutdown(stopwatch_app_t *self);

void stopwatch_start(stopwatch_app_t *self);
void stopwatch_pause(stopwatch_app_t *self);
void stopwatch_clear(stopwatch_app_t *self);

uint32_t stopwatch_get_elapsed_ms(stopwatch_app_t *self);
edge_status_t stopwatch_add_lap(stopwatch_app_t *self);
uint16_t stopwatch_get_max_lap_number(const stopwatch_app_t *self);
edge_status_t stopwatch_get_lap(const stopwatch_app_t *self, uint8_t index,
                                stopwatch_lap_t *out_lap);
uint32_t stopwatch_get_lap_duration_ms(const stopwatch_app_t *self, uint8_t index);

bool stopwatch_is_running(const stopwatch_app_t *self);
bool stopwatch_is_cleared(const stopwatch_app_t *self);
bool stopwatch_is_paused(const stopwatch_app_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_STOPWATCH_H */
