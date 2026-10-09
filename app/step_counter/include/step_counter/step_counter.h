#ifndef APP_STEP_COUNTER_H
#define APP_STEP_COUNTER_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "step_counter/ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STEP_HISTORY_SIZE 16u
#define ACCEL_STATS_HISTORY 8u

typedef struct accel_stats {
    int32_t x_mean;
    int32_t y_mean;
    int32_t z_mean;
    int32_t prev_x_mean;
    int32_t prev_y_mean;
    int32_t prev_z_mean;
    int32_t x_variance;
    int32_t y_variance;
    int32_t z_variance;
} accel_stats_t;

typedef struct step_counter {
    edge_module_t module;
    const imu_sensor_if_t *sensor;

    /* Step Counts */
    uint32_t today_steps;
    uint32_t yesterday_steps;
    uint32_t trip_steps;
    uint32_t last_raw_steps;

    /* Acceleration History Ring Buffer (16 samples) */
    int16_t x_history[STEP_HISTORY_SIZE];
    int16_t y_history[STEP_HISTORY_SIZE];
    int16_t z_history[STEP_HISTORY_SIZE];
    uint8_t hist_head;

    /* Motion Speed & Stats */
    int32_t accumulated_speed;
    uint32_t last_tick;
    accel_stats_t stats;

    bool is_running;
    uint32_t poll_count;
} step_counter_t;

void step_counter_construct(step_counter_t *self, uint32_t module_id, uint32_t priority,
                            const imu_sensor_if_t *sensor);

edge_status_t step_counter_init(step_counter_t *self);
edge_status_t step_counter_shutdown(step_counter_t *self);

void step_counter_update_motion(step_counter_t *self, int16_t x, int16_t y, int16_t z,
                                uint32_t raw_steps, uint32_t tick);

bool step_counter_should_raise_wake(const step_counter_t *self);
bool step_counter_should_lower_sleep(const step_counter_t *self);

void step_counter_advance_day(step_counter_t *self);
void step_counter_reset_trip(step_counter_t *self);

uint32_t step_counter_get_steps(const step_counter_t *self);
uint32_t step_counter_get_trip_steps(const step_counter_t *self);

int16_t step_counter_fast_asin(int16_t arg);

#ifdef __cplusplus
}
#endif

#endif /* APP_STEP_COUNTER_H */
