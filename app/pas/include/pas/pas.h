#ifndef PAS_H
#define PAS_H

#include <stdbool.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"
#include "edge/modules.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pas_sensor_type {
    PAS_SENSOR_CADENCE_ONLY = 0,
    PAS_SENSOR_TORQUE_AND_CADENCE
} pas_sensor_type_t;

typedef struct pas_port {
    void *self;
    edge_status_t (*read_cadence_rpm)(void *self, float *cadence_rpm);
    edge_status_t (*read_torque_nm)(void *self, float *torque_nm);
} pas_port_t;

typedef struct pas_config {
    pas_sensor_type_t type;
    float assist_ratio;
    float min_cadence_rpm;
    float max_cadence_rpm;
    float max_motor_current_a;
    float ramp_time_s;
} pas_config_t;

typedef struct pas_app {
    edge_module_t module;
    pas_config_t config;
    pas_port_t port;
    float current_demand_a;
    float measured_cadence_rpm;
    float measured_torque_nm;
    bool active;
} pas_app_t;

void pas_construct(pas_app_t *app, uint32_t module_id, uint32_t priority,
                   const pas_config_t *config, const pas_port_t *port);
edge_status_t pas_init(pas_app_t *app);
edge_status_t pas_update(pas_app_t *app, float dt);
float pas_get_current_demand(const pas_app_t *app);
bool pas_is_active(const pas_app_t *app);
edge_module_t *pas_module(pas_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* PAS_H */
