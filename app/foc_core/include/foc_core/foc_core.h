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

/*
 * Where a product keeps the backup block. The reference writes it from
 * conf_general_store_backup_data (conf_general.c:162-193), which it calls from its shutdown path
 * only
 * - its own comment says so, and says why: a page swap during a power loss could take the
 * configuration with it, so the store happens when the hardware's power switch says the power is
 * going away. The module's power_off is that path here, and where the words live is the product's,
 * so this is one callback rather than a format.
 *
 * The port is optional for the reason the reference's own store is conditional: hardware without a
 * power switch never has its shutdown path called.
 */
typedef struct foc_storage_port {
    void *self;
    edge_status_t (*store_backup)(void *self, const uint8_t *data, size_t len);
} foc_storage_port_t;

typedef enum {
    FOC_STATE_UNINITIALIZED = 0,
    FOC_STATE_FAULT,
    FOC_STATE_IDLE,
    FOC_STATE_RUNNING_CURRENT,
    FOC_STATE_RUNNING_DUTY,
    FOC_STATE_RUNNING_RPM,
    FOC_STATE_RUNNING_POS,

    /*
     * Reference CONTROL_MODE_HANDBRAKE. It is a mode of its own rather than a current
     * command: the loop forces the electrical phase to zero so the current locks the
     * rotor instead of driving it (mcpwm_foc.c:3602). Appended last so that no existing
     * value is renumbered.
     */
    FOC_STATE_HANDBRAKE,

    /*
     * Reference CONTROL_MODE_OPENLOOP, the mode mcpwm_foc_set_openloop_current selects
     * (mcpwm_foc.c:882). A mode rather than a command: the loop integrates the electrical angle
     * from the commanded speed and drives the current at that angle (mcpwm_foc.c:3607-3609),
     * which is how a motor is turned without knowing where the rotor is. Appended last, for the
     * same reason as the value above.
     */
    FOC_STATE_RUNNING_OPENLOOP,

    /*
     * Reference CONTROL_MODE_CURRENT_BRAKE, the mode mcpwm_foc_set_brake_current selects
     * (mcpwm_foc.c:832). A mode of its own rather than a current command: while it is entered the
     * loop shorts all three phases whenever the direction or the modulation changes sign, and holds
     * them shorted for at least ten cycles, until the braking current has been reached
     * (mcpwm_foc.c:3345-3366). Appended last, so no existing value is renumbered.
     */
    FOC_STATE_CURRENT_BRAKE
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

/*
 * The angle source the configuration selects, in the order the reference's mc_foc_sensor_mode
 * declares them (motor_config's vesc_enums.h). This app owns the name it answers to rather than
 * reading the generated enum, which lives in another app (D30: an app may not depend on one).
 */
typedef enum {
    FOC_ANGLE_SOURCE_SENSORLESS = 0,
    FOC_ANGLE_SOURCE_ENCODER,
    FOC_ANGLE_SOURCE_HALL,
    FOC_ANGLE_SOURCE_HFI,
    FOC_ANGLE_SOURCE_HFI_START,
    FOC_ANGLE_SOURCE_HFI_V2,
    FOC_ANGLE_SOURCE_HFI_V3,
    FOC_ANGLE_SOURCE_HFI_V4,
    FOC_ANGLE_SOURCE_HFI_V5,
} foc_sensor_mode_t;

/*
 * What the override derivation winds the current limits down by (foc_core_update_limits). The
 * ceiling is not here: foc_config_t's current_max_a / current_min_a are it, already carrying
 * whatever the product's own scaling decided - l_current_max * l_current_max_scale in the
 * reference's terms.
 */
typedef struct foc_limit_params {
    float l_temp_motor_start;     /* mcconf l_temp_motor_start, the motor temperature's knees */
    float l_temp_motor_end;       /* mcconf l_temp_motor_end */
    float l_erpm_start;           /* mcconf l_erpm_start, where both ERPM cuts begin */
    float l_max_erpm;             /* mcconf l_max_erpm */
    float l_min_erpm;             /* mcconf l_min_erpm */
    float foc_start_curr_dec;     /* mcconf foc_start_curr_dec, below foc_start_curr_dec_rpm */
    float foc_start_curr_dec_rpm; /* mcconf foc_start_curr_dec_rpm */
    float l_duty_start;           /* mcconf l_duty_start against l_max_duty, the duty de-rating */
    float l_max_duty;             /* mcconf l_max_duty */
    float cc_min_current;         /* mcconf cc_min_current, the floor both limits keep */
    float l_watt_max;             /* mcconf l_watt_max, over the input voltage */
    float l_watt_min;             /* mcconf l_watt_min */
    float l_in_current_max;       /* mcconf l_in_current_max */
    float l_in_current_min;       /* mcconf l_in_current_min */
    float
        l_in_current_map_start; /* mcconf l_in_current_map_start, the i_in fraction it starts at */
    float l_battery_cut_start;  /* mcconf l_battery_cut_start */
    float l_battery_cut_end;    /* mcconf l_battery_cut_end */
    float l_battery_regen_cut_start; /* mcconf l_battery_regen_cut_start */
    float l_battery_regen_cut_end;   /* mcconf l_battery_regen_cut_end */
} foc_limit_params_t;

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
    /* Reference: mcconf foc_observer_type (datatypes.h). */
    foc_observer_type_t observer_type;

    /* Reference: mcconf foc_sat_comp_mode / foc_sat_comp / foc_motor_ld_lq_diff. */
    uint8_t sat_comp_mode;
    float sat_comp;
    float ld_lq_diff;

    /*
     * Reference: mcconf foc_temp_comp / foc_temp_comp_base_temp. Two consumers, both gated on
     * the flag: the observer's resistance (foc_math.c:70-72) and the current loop's ki
     * (mcpwm_foc.c:4634-4637). The motor temperature itself arrives through
     * foc_core_set_motor_temperature().
     */
    bool temp_comp;
    float temp_comp_base_temp;

    /* Reference mcconf m_invert_direction. It has two consumers: the speed PID reads
     * it inside foc_run_pid_control_speed (foc_math.c:511), and the command layer uses
     * it as DIR_MULT (mc_interface.c:52). The PID gets its own copy through speed_pid
     * because that is what its signature takes; this is the motor-level view. */
    bool m_invert_direction;

    /*
     * Field weakening and MTPA, from mcconf foc_fw_* / foc_mtpa_mode / cc_min_current
     * (defaults: current_max 0.0, duty_start 0.8, backoff 2.0, ramp_time 0.5,
     * q_current_factor 0.05). foc_run_fw and foc_apply_mtpa take exactly these.
     */
    float fw_current_max;
    float fw_duty_start;
    float fw_backoff;
    float fw_ramp_time;
    float fw_q_current_factor;
    uint8_t mtpa_mode; /* FOC_MTPA_MODE_OFF / _IQ_TARGET / _IQ_MEASURED */
    float cc_min_current;

    /*
     * mcconf l_abs_current_max: the reference's absolute current ceiling, which is what its
     * relative-current paths gate their modulation-off-delay arming on (mc_interface.c:748). It is
     * not a limit this layer enforces - nothing clamps to it - it is the scale that gate is written
     * in.
     */
    float l_abs_current_max;

    /* Battery description, for the setup-values battery level (mc_interface_get_battery_level).
     * The type values are the reference's BATTERY_TYPE order. */
    uint8_t si_battery_type;
    int si_battery_cells;
    float si_battery_ah;

    /* Speed-loop parameters, handed to foc_run_pid_speed verbatim. */
    foc_speed_pid_params_t speed_pid;

    /* Position-loop parameters, handed to foc_run_pid_pos verbatim. */
    foc_pos_pid_params_t pos_pid;

    /*
     * What the override derivation winds the current limits down by (foc_core_update_limits). The
     * ceiling itself is not here: current_max_a / current_min_a above are it, already carrying
     * whatever the product's own scaling decided.
     */
    foc_limit_params_t limits;

    /* Reference: mcconf foc_pll_kp / foc_pll_ki, defaults 2000 / 30000
     * (motor/mcconf_default.h:284-288). */
    float pll_kp;
    float pll_ki;

    /*
     * HFI, from mcconf foc_hfi_* plus the two things the ported half needs to know about the loop
     * it runs in: f_zv is the switching frequency its lag compensation and its sampling estimate
     * are written in, and control_sample_mode says whether the interrupt samples in the two zero
     * vectors, which halves the period that compensation works with. sensor_mode is which angle
     * source runs, and the two bools stand for the reference's amb-mode and control-sample-mode
     * enumerations, each of which is a single question here.
     */
    foc_sensor_mode_t sensor_mode;
    bool hfi_amb_mode_six_vector;
    bool hfi_control_sample_mode_v0_v7;
    uint8_t hfi_samples;
    float hfi_voltage_start;
    float hfi_voltage_run;
    float hfi_voltage_max;
    float hfi_gain;
    float hfi_max_err;
    float sl_erpm_hfi;
    int hfi_start_samples;
    float hfi_obs_ovr_sec;
    float f_zv;
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
    float motor_temp_c;

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
    /*
     * Reference state_m->mod_alpha_raw / mod_beta_raw: the vector the SVM consumes, which is the
     * control output above plus HFI's excitation when it is running. The reference keeps the two
     * apart because its observer reads v_alpha/v_beta, which are the control's own output and do
     * not carry the injection.
     */
    float mod_alpha_raw;
    float mod_beta_raw;
    float duty_a;
    float duty_b;
    float duty_c;

    /*
     * Reference mcpwm_foc.c:3818-3820. This is a signed modulation magnitude
     * (SIGN(vq) * |mod| * p_duty_norm), NOT the phase-A duty in duty_a above; the
     * two are unrelated quantities that only look alike. The wire's duty field
     * (COMM_GET_VALUES) and the current-command branch in foc_core_set_current_rel
     * both read this one.
     */
    float duty_now;
    uint32_t svm_sector;

    /*
     * The two filtered quantities field weakening and MTPA read: |duty_now| low-passed at 0.01
     * and mod_q at 0.2, both clamped to magnitude 1 (reference mcpwm_foc.c:3332-3333 and
     * :3812-3814). They are per-cycle state, not per-command, so they live here.
     */
    float duty_abs_filtered;
    float mod_q_filter;
    /*
     * Reference m_duty_filtered (mcpwm_foc.c:3336-3337): |duty_now| is filtered above and the
     * signed duty here, both at the same coefficient. The brake's short-circuit test is the one
     * that reads the signed one, so it needs its own. The three fields below it are that test's own
     * state
     * (:3350-3367): the direction and the modulation the previous cycle ended with, how many cycles
     * the phases have been shorted for, and whether the last cycle was driven from duty rather than
     * from current. */
    float duty_filtered;
    float br_speed_before;
    float br_vq_before;
    uint32_t br_no_duty_samples;
    bool was_control_duty;

    /*
     * The backup data's two counters, reference mc_interface.c:2570-2578: the odometer in metres
     * and the runtime, which the reference keeps in g_backup and the protocol reads back through
     * COMM_GET_VALUES_SETUP. The odometer accumulates the *difference* of the absolute distance
     * because the reference truncates that distance to whole metres first
     * (mc_interface.c:1650-1656), so what is added is the metres the tachometer has newly covered.
     *
     * Persistence is the product's: this aggregate owns the live counter and the reference stores
     * the backup block only from its shutdown path, which is what the module's own power_off is
     * here.
     */
    uint64_t backup_odometer_m;
    uint64_t backup_distance_last_m;
    /* Microseconds, not seconds: summing the loop's dt in a float loses a millisecond every ten of
     * them, because 200 additions of 5e-5 are not exactly 0.01. The reference reads a wall clock
     * and has no such drift; this port counts, so it counts in whole microseconds. */
    uint64_t backup_uptime_us;

    /* Where a product keeps that block. Optional, as the reference's own store is: hardware without
     * a power switch never has its shutdown path called. */
    const foc_storage_port_t *storage;

    /* Field-weakening setpoint, the reference's m_i_fw_set. */
    float i_fw_set;

    /* Observer & Feedback */
    foc_observer_t observer;
    foc_pll_t pll;
    foc_speed_pid_t speed_pid;
    foc_pos_pid_t pos_pid;

    /*
     * The limits the control loop actually runs on, the reference's lo_current_max / lo_current_min
     * (mc_interface.c:2540-2541). foc_core_update_limits recomputes them from the configuration and
     * what the machine is doing; everything that turns a fraction or a speed into a current reads
     * these rather than the configuration's ceiling.
     */
    float lo_current_max;
    float lo_current_min;
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
     * The motor NTC, already low-passed by whoever samples it: the reference filters it in its
     * ADC interrupt handler (mc_interface.c:2331) with the board's MOTOR_TEMP_LPF, and the FOC
     * only reads the cached value. It starts at zero, as the reference's own static does.
     */
    float motor_temp_c;

    /* Reference timer_update (mcpwm_foc.c:3939-3948) recomputes these every cycle; they are
     * only *used* when foc_temp_comp is set, which is why they exist even with the flag off. */
    float res_temp_comp;
    float current_ki_temp_comp;

    /*
     * Forced-angle mode: the reference's m_phase_override / m_phase_now_override. While it is set
     * the control loop runs against this angle and the sensor and observer branches are not
     * taken at all, which is how the reference guards them (mcpwm_foc.c:3486, :3524, :3532).
     * Motor detection is its only user, and it needs the rotor held at a known electrical angle.
     */
    bool phase_override;
    float phase_override_rad;

    /*
     * Open-loop drive: the reference's m_openloop_angle and m_openloop_speed (mcpwm_foc.c:884,
     * :3607). The angle is integrated by the control loop at the commanded electrical speed and
     * normalised, and it is the angle the current is applied at. Both start at zero, as the
     * reference's own state does.
     */
    float openloop_angle;
    float openloop_speed;

    /*
     * Reference m_motor_released and m_current_off_delay (mcpwm_foc.c:823, :3972). Releasing is a
     * request: it zeroes the setpoints and sets the flag, and the control loop then decides - the
     * delay counts down and, with every setpoint below the minimum, it stops the modulation.
     */
    bool motor_released;
    float current_off_delay;

    /*
     * Reference mcpwm_foc.c:4139-4146: the detection's sample accumulator, which the control
     * loop adds to on every cycle it ran - the current and voltage vector magnitudes that cycle
     * produced. The resistance measurement reads and clears it; nothing else reads it.
     */
    float detect_i_sum;
    float detect_v_sum;
    uint32_t detect_samples;

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

    /*
     * HFI's own state (the reference's m_hfi) and the two things that pace it. The reference runs
     * its tracking half on a thread that sleeps 500 microseconds - 2 kHz - while the excitation
     * runs in the interrupt at the switching frequency, so the accumulator steps the tracking half
     * at that slower rate rather than once per control cycle. hfi_using_hfi is the reference's
     * m_using_encoder as this mode uses it: the hysteresis that decides whether the angle in use is
     * HFI's or the observer's.
     */
    foc_hfi_state_t hfi;
    float hfi_step_accum;
    bool hfi_using_hfi;
    /* Reference m_cc_was_hfi (:4618): last cycle's excitation decision, which widens the speed gate
     * that decision is made under from 1.5 to 1.8 times foc_sl_erpm_hfi (:4594). */
    bool hfi_was_hfi;
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

/*
 * Reference mc_interface_set_current_rel: the relative setpoint is scaled by a limit
 * chosen from the duty's sign, so this needs the last cycle's duty and cannot be
 * resolved in the codec. The result goes through the same path as set_current.
 */
edge_status_t foc_core_set_current_rel(foc_core_t *self, float rel);

/*
 * The reference's mcpwm_foc_set_current_off_delay (mcpwm_foc.c:1114): keep the current controller
 * from switching its modulation off for a target below cc_min_current for this long. It takes the
 * larger of what is armed and what is asked - never a smaller one - which is why a relative current
 * command can only postpone that switching-off and not bring it forward. The field and its reader
 * are already here; this is the arming side of it.
 */
void foc_core_arm_current_off_delay(foc_core_t *self, float delay_sec);

/*
 * The reference's update_override_limits (mc_interface.c:2245): the limits the control loop runs
 * on, which are foc_config_t's current_max_a / current_min_a wound down by what the machine is
 * doing - the duty it is at, both ERPM cuts, the start-current decrease, the input current against
 * the wattage and the battery cutoffs, and the motor's own temperature. Called once per control
 * cycle, from where the reference calls it out of its timer task (:2607). What it does not derive,
 * and why, is written down where the terms live.
 */
void foc_core_update_limits(foc_core_t *self);
edge_status_t foc_core_set_duty(foc_core_t *self, float duty_target);
edge_status_t foc_core_set_rpm(foc_core_t *self, float rpm_target);
edge_status_t foc_core_set_pos(foc_core_t *self, float pos_target_deg);
edge_status_t foc_core_set_handbrake(foc_core_t *self, float brake_current_a);

/*
 * Reference mcpwm_foc_set_brake_current (mcpwm_foc.c:832-849): brake with a desired current, where
 * positive and negative values have the same effect because the mode's own short-circuit test looks
 * at magnitudes. A magnitude below cc_min_current leaves the mode set and the setpoint written but
 * does nothing else - the reference's own early return - which is what makes the command a no-op
 * rather than a release. DIR_MULT is the command layer's, as it is for the other current commands.
 */
edge_status_t foc_core_set_brake_current(foc_core_t *self, float current_a);
/* Reference mcpwm_foc_release_motor (mcpwm_foc.c:819): zeroes both current setpoints, asks for the
 * release, and leaves the control loop to carry it out. */
edge_status_t foc_core_release_motor(foc_core_t *self);
/* Reference mcpwm_foc_set_openloop_current (mcpwm_foc.c:878): an electrical speed and the q-axis
 * current to drive at it. Unlike the current command it truncates, and a current below
 * cc_min_current selects the mode without starting the motor. */
edge_status_t foc_core_set_openloop_current(foc_core_t *self, float current_a, float rpm);
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
void foc_core_set_fet_temperature(foc_core_t *self, float fet_temp_c);

/*
 * The filtered motor NTC reading. The reference keeps it separately from the FET temperature
 * (m_temp_motor vs m_temp_fet) because the two have their own sensors and their own filters, so
 * this is a second input rather than a second argument to the one above.
 */
void foc_core_set_motor_temperature(foc_core_t *self, float motor_temp_c);

/*
 * Hold the rotor at a known electrical angle, which is what the reference's m_phase_override does
 * for motor detection (mcpwm_foc.c:1803). While it is set the loop uses this angle and does not
 * read the sensor or run the observer; clearing it returns the loop to its normal angle source.
 */
void foc_core_set_phase_override(foc_core_t *self, float angle_rad, bool enable);

/*
 * The detection sample accumulator: the running sums of the current and voltage vector
 * magnitudes and how many cycles produced them, which is what the reference's measurements read
 * (mcpwm_foc.c:1846 checks the count while sampling, :1869-1870 divides the totals) and what it
 * clears before sampling (:1841-1843). Read and clear are separate calls because the reference
 * uses them at different moments. Any output may be null.
 */
void foc_core_read_detect_samples(const foc_core_t *self, float *i_sum, float *v_sum,
                                  uint32_t *count);
void foc_core_reset_detect_samples(foc_core_t *self);

/*
 * The HFI transform's own outputs, which the inductance measurement reads (mcpwm_foc.c:2004-2007):
 * bin 0 of the sample buffer - the mean of the inverse inductance - bin 2 of it - the saliency's
 * second harmonic, whose magnitude the caller doubles - and bin 0 of the current-step buffer, the
 * mean measured step. The table the bins run over is the one the configuration selected, so a port
 * that has not selected one reports zeros.
 */
void foc_core_read_hfi_bins(const foc_core_t *self, float *offset, float *real_bin2,
                            float *imag_bin2, float *current_mean);

void foc_core_get_stats(const foc_core_t *self, foc_stats_t *out_stats);

/*
 * The two backup counters, as the protocol reads them (`COMM_GET_VALUES_SETUP`). The runtime is
 * milliseconds, which is the unit that reply carries; the reference derives it from its own wall
 * clock, while this port sums the loop's dt - the same difference the energy counters carry.
 */
void foc_core_get_backup(const foc_core_t *self, uint64_t *odometer_m, uint32_t *uptime_ms);

/* The restore side, for a product that read the block back at boot. */
void foc_core_set_backup(foc_core_t *self, uint64_t odometer_m, uint32_t uptime_ms);

void foc_core_set_storage_port(foc_core_t *self, const foc_storage_port_t *port);

/*
 * The backup block itself, which is the subset of the reference's packed backup_data that this port
 * has sources for: an init flag and a value for the odometer in metres, and the same pair for the
 * runtime in seconds - the reference's own units and its own validity rule, where each field is
 * recovered from RAM only if its flag still carries BACKUP_VAR_INIT_CODE. What the reference also
 * carries (the hardware configuration, the encoder correction and the CAN identity) has no source
 * here and is left out rather than invented, which makes this block shorter than its own.
 *
 * serialize fills `len` bytes and reports how many it wrote, or zero if `len` is too small; restore
 * takes the same bytes and returns EDGE_OK only when every flag was valid, having filled the values
 * it could either way.
 */
#define FOC_BACKUP_BLOCK_BYTES 24u
/* Reference BACKUP_VAR_INIT_CODE (datatypes.h): the value each field's flag carries once it holds
 * something real, and the value the recovery rule tests. */
#define FOC_BACKUP_INIT_CODE 92891934u
size_t foc_core_backup_serialize(const foc_core_t *self, uint8_t *out, size_t len);
edge_status_t foc_core_backup_restore(foc_core_t *self, const uint8_t *in, size_t len);
void foc_core_stats_reset(foc_core_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_FOC_CORE_H */
