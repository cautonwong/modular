#ifndef APP_STEP_COUNTER_PORTS_H
#define APP_STEP_COUNTER_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct imu_sensor_if {
    edge_status_t (*read_accel)(void *self, int16_t *out_x, int16_t *out_y, int16_t *out_z);
    edge_status_t (*read_steps)(void *self, uint32_t *out_steps);
    edge_status_t (*reset_steps)(void *self);
    void *self;
} imu_sensor_if_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_STEP_COUNTER_PORTS_H */
