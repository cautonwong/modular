#ifndef APP_FOC_CORE_H
#define APP_FOC_CORE_H

#include "edge/module.h"
#include "foc_core/foc_math.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Consumer-Defined Ports (Rule: must have void *self; callbacks take void *self)
 */
typedef struct foc_inverter_port {
    edge_status_t (*set_duty)(void *self, float duty_a, float duty_b, float duty_c);
    edge_status_t (*set_phase_state)(void *self, bool enable);
    void *self;
} foc_inverter_port_t;

typedef struct foc_current_port {
    edge_status_t (*read_currents)(void *self, float *ia, float *ib, float *ic);
    edge_status_t (*read_vbus)(void *self, float *v_bus);
    void *self;
} foc_current_port_t;

typedef struct foc_rotor_port {
    edge_status_t (*read_angle)(void *self, float *angle_rad, float *rpm);
    void *self;
} foc_rotor_port_t;

typedef enum {
    FOC_STATE_UNINITIALIZED = 0,
    FOC_STATE_FAULT,
    FOC_STATE_IDLE,
    FOC_STATE_RUNNING_CURRENT,
    FOC_STATE_RUNNING_DUTY,
    FOC_STATE_RUNNING_RPM,
    FOC_STATE_RUNNING_POS
} foc_state_t;

typedef enum {
    FOC_FAULT_NONE = 0,
    FOC_FAULT_OVER_CURRENT = (1u << 0),
    FOC_FAULT_OVER_VOLTAGE = (1u << 1),
    FOC_FAULT_UNDER_VOLTAGE = (1u << 2),
    FOC_FAULT_OVER_TEMP = (1u << 3),
    FOC_FAULT_SENSOR_LOST = (1u << 4),
    FOC_FAULT_INVALID_CONFIG = (1u << 5)
} foc_fault_t;

typedef struct foc_config {
    float r_ohm;
    float l_henry;
    float lambda_wb;
    int pole_pairs;

    float current_max_a;
    float current_min_a;
    float duty_max;

    float current_kp;
    float current_ki;

    float vbus_ov_threshold;
    float vbus_uv_threshold;
    float temp_fet_max_c;

    bool sensorless_mode;
    float observer_gamma;
} foc_config_t;

typedef struct foc_telemetry {
    foc_state_t state;
    uint32_t faults;
    float v_bus;
    float current_d;
    float current_q;
    float current_abs;
    float duty_now;
    float rotor_angle_rad;
    float speed_rpm;
    float fet_temp_c;
} foc_telemetry_t;

typedef struct foc_core {
    edge_module_t module;

    /* Injected Consumer Ports */
    const foc_inverter_port_t *inverter;
    const foc_current_port_t *current_sensor;
    const foc_rotor_port_t *rotor_sensor;

    /* Parameters & Configuration */
    foc_config_t config;

    /* Domain State Machine & Aggregation Invariants */
    foc_state_t state;
    uint32_t faults;

    /* Target Setpoints */
    float target_id;
    float target_iq;
    float target_duty;
    float target_rpm;

    /* Fast Control Loop Internal State */
    float id_integral;
    float iq_integral;
    float v_d;
    float v_q;
    float v_alpha;
    float v_beta;
    float duty_a;
    float duty_b;
    float duty_c;
    uint32_t svm_sector;

    /* Observer & Feedback */
    foc_observer_t observer;
    float last_v_bus;
    float last_ia;
    float last_ib;
    float last_ic;
    float last_id;
    float last_iq;
    float last_angle_rad;
    float last_rpm;
    float fet_temp_c;

    /* Metrics & Diagnostics */
    uint32_t fast_loop_count;
    uint32_t step_count;
} foc_core_t;

/* Construction & Lifecycle API (D51: init called by composition root) */
void foc_core_construct(foc_core_t *self, uint32_t module_id, uint32_t priority,
                        const foc_config_t *config, const foc_inverter_port_t *inverter,
                        const foc_current_port_t *current_sensor,
                        const foc_rotor_port_t *rotor_sensor);

edge_status_t foc_core_init(foc_core_t *self);
edge_status_t foc_core_deinit(foc_core_t *self);
edge_module_t *foc_core_module(foc_core_t *self);

/* Fast Real-Time ISR Path (20kHz - 40kHz) */
edge_status_t foc_core_fast_loop(foc_core_t *self, float dt);

/* Domain Commands & Setpoints */
edge_status_t foc_core_set_current(foc_core_t *self, float iq_target, float id_target);
edge_status_t foc_core_set_duty(foc_core_t *self, float duty_target);
edge_status_t foc_core_set_rpm(foc_core_t *self, float rpm_target);
edge_status_t foc_core_set_pos(foc_core_t *self, float pos_target_deg);
edge_status_t foc_core_set_handbrake(foc_core_t *self, float brake_current_a);
edge_status_t foc_core_stop(foc_core_t *self);
edge_status_t foc_core_clear_faults(foc_core_t *self);

/* Telemetry & Queries */
foc_state_t foc_core_get_state(const foc_core_t *self);
uint32_t foc_core_get_faults(const foc_core_t *self);
void foc_core_get_telemetry(const foc_core_t *self, foc_telemetry_t *out_telem);
void foc_core_set_temperature(foc_core_t *self, float fet_temp_c);

#ifdef __cplusplus
}
#endif

#endif /* APP_FOC_CORE_H */
