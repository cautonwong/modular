#ifndef MOTOR_ID_H
#define MOTOR_ID_H

#include <stdbool.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"
#include "edge/modules.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum motor_id_state {
    MOTOR_ID_STATE_IDLE = 0,
    MOTOR_ID_STATE_MEASURING_R,
    MOTOR_ID_STATE_MEASURING_L,
    MOTOR_ID_STATE_SPIN_FLUX,
    MOTOR_ID_STATE_DETECT_HALL,
    MOTOR_ID_STATE_COMPLETE,
    MOTOR_ID_STATE_FAILED
} motor_id_state_t;

typedef struct motor_id_result {
    float r_ohm;
    float l_henry;
    float flux_linkage_wb;
    uint8_t hall_table[8];
    bool hall_inverted;
    bool valid;
} motor_id_result_t;

typedef struct motor_id_measure_port {
    void *self;
    edge_status_t (*get_currents)(void *self, float *i_alpha, float *i_beta);
    edge_status_t (*get_vbus)(void *self, float *v_bus);
    uint8_t (*get_hall)(void *self);
    float (*get_rotor_angle)(void *self);
} motor_id_measure_port_t;

typedef struct motor_id_control_port {
    void *self;
    edge_status_t (*set_voltage_alpha_beta)(void *self, float v_alpha, float v_beta);
    edge_status_t (*set_pwm_duty)(void *self, float duty_a, float duty_b, float duty_c);
    edge_status_t (*set_openloop_angle)(void *self, float angle_rad, float current_target);
    edge_status_t (*stop_inverter)(void *self);
} motor_id_control_port_t;

typedef struct motor_id_config {
    float max_current;
    float target_erpm;
    uint32_t samples_r;
    uint32_t samples_l;
} motor_id_config_t;

typedef struct motor_id_app {
    edge_module_t module;
    motor_id_state_t state;
    motor_id_config_t config;
    motor_id_measure_port_t measure_port;
    motor_id_control_port_t control_port;
    motor_id_result_t result;
    uint32_t step_count;
    float accum_v;
    float accum_i;
    float current_applied_v;
    float openloop_angle;
} motor_id_app_t;

void motor_id_construct(motor_id_app_t *app, uint32_t module_id, uint32_t priority,
                        const motor_id_config_t *config,
                        const motor_id_measure_port_t *measure_port,
                        const motor_id_control_port_t *control_port);
edge_status_t motor_id_init(motor_id_app_t *app);

edge_status_t motor_id_start_r_l(motor_id_app_t *app);
edge_status_t motor_id_start_flux(motor_id_app_t *app);
edge_status_t motor_id_start_hall(motor_id_app_t *app);

edge_status_t motor_id_step(motor_id_app_t *app, float dt);
const motor_id_result_t *motor_id_get_result(const motor_id_app_t *app);
edge_module_t *motor_id_module(motor_id_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_ID_H */
