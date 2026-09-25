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

    /*
     * Motor speed information. The reference has no separate pole-pair field: the
     * FOC, the virtual motor and the speed conversion all derive it from
     * si_motor_poles / 2 (motor/virtual_motor.c:126, motor/mc_interface.c:1626), so
     * this is the single source here too.
     */
    uint8_t si_motor_poles;
    float si_gear_ratio;
    float si_wheel_diameter;

    float current_max_a;
    float current_min_a;
    float duty_max;

    float current_kp;
    float current_ki;

    float vbus_ov_threshold;
    float vbus_uv_threshold;
    float temp_fet_max_c;

    /* Reference: mcconf foc_current_filter_const, default 0.1
     * (motor/mcconf_default.h MCCONF_FOC_CURRENT_FILTER_CONST). */
    float current_filter_const;

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

    /*
     * Energy counters are cumulative, not averages, and the reference never reads
     * them with its reset flag set (no caller in the tree passes true), so a plain
     * snapshot is the whole contract. Units are hours, as the protocol sends them
     * (the accumulators are in amp-seconds / watt-seconds and divided by 3600).
     */
    int32_t tachometer;
    int32_t tachometer_abs;
    float current_in;
    float amp_hours;
    float amp_hours_charged;
    float watt_hours;
    float watt_hours_charged;
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

    /*
     * Low-passed currents. The reference filters id/iq right after the Park
     * transform (mcpwm_foc.c:4628) and is explicit that these are for "less time
     * critical parts, not for the feedback" - the current controller keeps using
     * the raw values. They exist because the energy counters are gated on the
     * filtered current magnitude, not the instantaneous one.
     */
    float id_filter;
    float iq_filter;
    float i_abs_filter;

    /* Input (bus) current: the reference has no DC-current sensor on this path and
     * estimates it by power balance, mcpwm_foc.c:4713 with the modulation
     * normalised as 1.5/v_bus, i.e. i_bus = 1.5 * (vd*id + vq*iq) / v_bus. */
    float i_bus;

    /* Total motor current magnitude, unfiltered. Reference: state_m->i_abs
     * (mcpwm_foc.c:4717). The energy counters gate on the FILTERED magnitude, the
     * statistics below accumulate this raw one - both, as the reference does. */
    float i_abs;

    /*
     * Tachometer. The reference does NOT need a hall sensor or an encoder for this:
     * it quantises the phase the FOC already has into six 60-degree sectors and
     * counts the sector deltas, with the wrap correction below
     * (mcpwm_foc.c:3866-3881, "resolution = 60 deg as for BLDC").
     */
    int32_t tacho_step_last;
    int32_t tachometer;
    int32_t tachometer_abs;

    /* Vehicle speed in m/s, reference mc_interface_get_speed(). */
    float speed_m_s;

    /* Statistics, see foc_stats_t. */
    float stat_speed_sum;
    float stat_max_speed;
    float stat_samples;
    float stat_power_sum;
    float stat_max_power;
    float stat_current_sum;
    float stat_max_current;
    float stat_temp_mos_sum;
    float stat_max_temp_mos;
    float stat_temp_motor_sum;
    float stat_max_temp_motor;

    /* Energy counters, amp-seconds / watt-seconds before the /3600. */
    float amp_seconds;
    float amp_seconds_charged;
    float watt_seconds;
    float watt_seconds_charged;

    /*
     * Running sums for the read-and-reset averages the reference serves over the
     * protocol. The reference keeps these in mc_interface.c and feeds them from a
     * periodic sampler (m_motor_id_sum += mcpwm_foc_get_id(), ...), then
     * mc_interface_read_reset_avg_id() divides by the iteration count and zeroes
     * it. Same contract here: average since the previous read, 0/0 included.
     */
    float avg_id_sum;
    float avg_iq_sum;
    float avg_vd_sum;
    float avg_vq_sum;
    float avg_motor_current_sum;
    float avg_input_current_sum;
    float avg_id_iterations;
    float avg_iq_iterations;
    float avg_vd_iterations;
    float avg_vq_iterations;
    float avg_motor_current_iterations;
    float avg_input_current_iterations;

    /* Metrics & Diagnostics */
    uint32_t fast_loop_count;
    uint32_t step_count;
} foc_core_t;

/*
 * Running statistics (reference: setup_stats / mc_interface.c update_stats(),
 * sampled by a dedicated thread; COMM_GET_STATS serves them). Averages are
 * sum/samples, maxima are running maxima since the last reset, and the two
 * temperature maxima start at -300 as the reference's stat_reset() does.
 *
 * Not carried here, because the inputs do not exist yet: the speed statistics
 * need mc_configuration's si_motor_poles / si_wheel_diameter / si_gear_ratio
 * (reference mc_interface_get_speed() converts ERPM to m/s with them), and
 * count_time needs a clock this module is not given.
 */
typedef struct foc_stats {
    float speed_avg;
    float speed_max;
    float power_avg;
    float power_max;
    float current_avg;
    float current_max;
    float temp_mos_avg;
    float temp_mos_max;
    float temp_motor_avg;
    float temp_motor_max;
} foc_stats_t;

/* One bit per read-and-reset average; only the masked channels are read and reset. */
typedef enum {
    FOC_AVG_MOTOR_CURRENT = (1u << 0),
    FOC_AVG_INPUT_CURRENT = (1u << 1),
    FOC_AVG_ID = (1u << 2),
    FOC_AVG_IQ = (1u << 3),
    FOC_AVG_VD = (1u << 4),
    FOC_AVG_VQ = (1u << 5),
} foc_avg_channel_t;

typedef struct foc_averages {
    float motor_current;
    float input_current;
    float id;
    float iq;
    float vd;
    float vq;
} foc_averages_t;

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

/*
 * Read the averages selected by channel_mask and reset exactly those accumulators.
 * Channels not in the mask are left alone, so a peer polling one field does not
 * shorten every other field's averaging window (reference: COMM_GET_VALUES_SELECTIVE
 * only calls mc_interface_read_reset_* for the bits its mask selects).
 */
void foc_core_read_reset_averages(foc_core_t *self, uint32_t channel_mask, foc_averages_t *out);
void foc_core_set_temperature(foc_core_t *self, float fet_temp_c);

void foc_core_get_stats(const foc_core_t *self, foc_stats_t *out_stats);
void foc_core_stats_reset(foc_core_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_FOC_CORE_H */
