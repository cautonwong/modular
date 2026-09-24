#ifndef BALANCE_H
#define BALANCE_H

#include <stdbool.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"
#include "edge/modules.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct balance_port {
    void *self;
    edge_status_t (*read_attitude)(void *self, float *pitch_deg, float *roll_deg,
                                   float *gyro_pitch_dps, float *gyro_roll_dps, bool *foot_switch1,
                                   bool *foot_switch2);
} balance_port_t;

typedef struct balance_config {
    float kp;
    float ki;
    float kd;
    float max_current_a;
    float deadband_deg;
    float fault_pitch_deg;
    float fault_roll_deg;
    float tiltback_speed_rpm;
    float tiltback_angle_deg;
} balance_config_t;

typedef struct balance_app {
    edge_module_t module;
    balance_config_t config;
    balance_port_t port;
    float current_demand_a;
    float integral;
    float last_error;
    float target_pitch_deg;
    bool foot_engaged;
    bool faulted;
} balance_app_t;

void balance_construct(balance_app_t *app, uint32_t module_id, uint32_t priority,
                       const balance_config_t *config, const balance_port_t *port);
edge_status_t balance_init(balance_app_t *app);
edge_status_t balance_update(balance_app_t *app, float motor_rpm, float dt);
float balance_get_current_demand(const balance_app_t *app);
bool balance_is_engaged(const balance_app_t *app);
edge_module_t *balance_module(balance_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* BALANCE_H */
