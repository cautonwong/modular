#ifndef ZMK_SENSORS_H
#define ZMK_SENSORS_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "zmk_sensors/ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZMK_MAX_SENSORS 8u

enum zmk_sensor_direction {
    ZMK_SENSOR_DIR_CW = 1,
    ZMK_SENSOR_DIR_CCW = -1,
};

typedef struct zmk_sensor_binding {
    uint16_t behavior_id;
    uint32_t cw_param1;
    uint32_t ccw_param1;
} zmk_sensor_binding_t;

typedef struct zmk_encoder_state {
    uint8_t pin_a_state;
    uint8_t pin_b_state;
    int8_t pulse_accumulator;
    uint8_t pulses_per_detent; /* Default 4 or 2 */
    zmk_sensor_binding_t binding;
} zmk_encoder_state_t;

typedef struct zmk_sensors_app {
    edge_module_t module;
    const zmk_sensors_behavior_if_t *behavior_port;

    zmk_encoder_state_t sensors[ZMK_MAX_SENSORS];
    uint8_t sensor_count;
} zmk_sensors_app_t;

void zmk_sensors_construct(zmk_sensors_app_t *self, uint32_t module_id, uint32_t priority,
                           const zmk_sensors_behavior_if_t *behavior_port);

edge_status_t zmk_sensors_init(zmk_sensors_app_t *self);
edge_status_t zmk_sensors_shutdown(zmk_sensors_app_t *self);

edge_status_t zmk_sensors_add_encoder(zmk_sensors_app_t *self, uint8_t pulses_per_detent,
                                      zmk_sensor_binding_t binding, uint8_t *out_sensor_id);

edge_status_t zmk_sensors_process_encoder_pulse(zmk_sensors_app_t *self, uint8_t sensor_id,
                                                uint8_t pin_a, uint8_t pin_b,
                                                uint32_t timestamp_ms);

edge_status_t zmk_sensors_process_step(zmk_sensors_app_t *self, uint8_t sensor_id, int8_t steps,
                                       uint32_t timestamp_ms);

#ifdef __cplusplus
}
#endif

#endif /* ZMK_SENSORS_H */
