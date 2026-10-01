#ifndef FOC_MATH_H
#define FOC_MATH_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef M_PI
/*
 * Double, as glibc's M_PI is and as the reference uses it. This was a float literal, which made
 * every angle wrap (`*angle += 2.0 * M_PI`) land about one float ULP away from the reference's;
 * the error is invisible in one call and accumulates over thousands - measured with the HFI
 * tracker's differential harness, where it reached the fifth digit after 36 x 400 steps. Strict
 * -std=c11 does not expose glibc's M_PI (the reference tree needs _DEFAULT_SOURCE to get it), so
 * this definition is what the port sees and it has to be the same value.
 */
#define M_PI 3.14159265358979323846
#endif

#define SQRT3_BY_2 0.8660254037844386f
#define ONE_BY_SQRT3 0.5773502691896257f
#define TWO_BY_SQRT3 1.1547005383792515f
#define SQ(x) ((x) * (x))

/* Reference: util/utils_math.h:65. */
#define NORM2_f(x, y) (sqrtf(SQ(x) + SQ(y)))

/* Fast trigonometric utilities */
void foc_fast_sincos(float angle_rad, float *sin_out, float *cos_out);

/*
 * Reference: util/utils_math.c utils_fast_atan2 - a polynomial approximation, not
 * atan2f. The observer feeds this angle straight into the Park transform, so the
 * approximation error (up to ~0.003 rad) is part of the reference behaviour.
 */
float foc_fast_atan2(float y, float x);

/*
 * Saturation compensation mode, same order and meaning as the reference's
 * SAT_COMP_MODE (datatypes.h).
 */
typedef enum {
    FOC_SAT_COMP_DISABLED = 0,
    FOC_SAT_COMP_FACTOR,
    FOC_SAT_COMP_LAMBDA,
    FOC_SAT_COMP_LAMBDA_AND_FACTOR,
} foc_sat_comp_mode_t;

/*
 * Phase-locked loop, reference util/... foc_math.c foc_pll_run (motor/foc_math.c:225).
 * The observer gives an angle; the PLL turns it into the electrical speed the
 * control path actually uses. This port previously differentiated the observer
 * phase instead, which is not what the reference does and is noisier.
 */
typedef struct foc_pll {
    float phase;
    float speed;
} foc_pll_t;

void foc_pll_run(foc_pll_t *pll, float phase, float dt, float kp, float ki);

/*
 * Speed PID, reference motor/foc_math.c:492 foc_run_pid_control_speed. State and
 * configuration are separated here so the loop can be driven and compared on its
 * own; the reference keeps both inside the motor struct and the configuration.
 *
 * `iq_set` is in/out: the reference writes motor->m_iq_set, and when the loop is not
 * in speed mode it returns leaving the setpoint untouched - which is why this
 * cannot simply return a value.
 */
typedef struct foc_speed_pid {
    float set_rpm;    /* reference: m_speed_pid_set_rpm, the ramped setpoint */
    float i_term;     /* m_speed_i_term */
    float prev_error; /* m_speed_prev_error */
    float d_filter;   /* m_speed_d_filter */
} foc_speed_pid_t;

typedef struct foc_speed_pid_params {
    float kp;
    float ki;
    float kd;
    float kd_filter;
    float ramp_erpms_s; /* s_pid_ramp_erpms_s */
    float min_erpm;     /* s_pid_min_erpm */
    float openloop_rpm; /* foc_openloop_rpm */
    float l_min_erpm;
    float l_max_erpm;
    float lo_current_max; /* the motor current limit the output is scaled to */
    float current_max_scale;
    bool allow_braking;
    bool invert_direction;
} foc_speed_pid_params_t;

void foc_run_pid_speed(foc_speed_pid_t *pid, const foc_speed_pid_params_t *params,
                       bool in_speed_mode, bool index_found, float rpm, float rpm_command, float dt,
                       float *iq_set);

/*
 * Observer selection, same order and names as the reference's mc_foc_observer_type
 * (datatypes.h). Each has a distinct convergence behaviour and a distinct set of
 * states it maintains, so this is a selector over real algorithms, not a hint.
 */
typedef enum {
    FOC_OBSERVER_ORTEGA_ORIGINAL = 0,
    FOC_OBSERVER_MXLEMMING,
    FOC_OBSERVER_ORTEGA_LAMBDA_COMP,
    FOC_OBSERVER_MXLEMMING_LAMBDA_COMP,
    FOC_OBSERVER_MXV,
    FOC_OBSERVER_MXV_LAMBDA_COMP,
    FOC_OBSERVER_MXV_LAMBDA_COMP_LIN,
} foc_observer_type_t;

/* Vector transformations */
void foc_clarke_transform(float ia, float ib, float ic, float *i_alpha, float *i_beta);
void foc_park_transform(float i_alpha, float i_beta, float sin_th, float cos_th, float *id,
                        float *iq);
void foc_inv_park_transform(float vd, float vq, float sin_th, float cos_th, float *v_alpha,
                            float *v_beta);

/* Space Vector PWM (SVPWM) */
void foc_svpwm(float v_alpha, float v_beta, float v_bus, float duty_max, float *duty_a,
               float *duty_b, float *duty_c, uint32_t *sector_out);

/* Reference: util/utils_math.h:206 - the reference's own map, not a re-derivation. */
#define FOC_MAP(x, in_min, in_max, out_min, out_max)                                               \
    (((x) - (in_min)) * ((out_max) - (out_min)) / ((in_max) - (in_min)) + (out_min))

float foc_sign(float x);
void foc_step_towards(float *value, float goal, float step);
void foc_truncate_number(float *number, float min, float max);
void foc_truncate_number_abs(float *number, float max);
float foc_min_abs(float va, float vb);
float foc_max_abs(float va, float vb);

/*
 * Field weakening, reference motor/foc_math.c:708-762. The reference reads this out of
 * motor_all_state_t and mc_configuration; here it is a pure function over exactly the fields
 * it touches, so its output can be compared with the reference's own. One side effect is
 * deliberately absent: the reference also sets m_current_off_delay = 1.0, whose only reader
 * is the modulation-extension block (mcpwm_foc.c:3953-3990). That block is not ported yet, so
 * the field would be dead state; it arrives with the block.
 */
typedef struct foc_fw_params {
    float current_max; /* foc_fw_current_max */
    float duty_start;  /* foc_fw_duty_start */
    float backoff;     /* foc_fw_backoff */
    float ramp_time;   /* foc_fw_ramp_time */
    float l_max_duty;  /* l_max_duty */
    float cc_min_current;
} foc_fw_params_t;

typedef struct foc_fw_state {
    float duty_abs_filtered; /* m_duty_abs_filtered */
    float iq;                /* motor state iq */
    float iq_target;         /* motor state iq_target */
    float speed_erpm;        /* m_speed_est_fast; only its sign is used */
    float i_fw_set;          /* in and out */
} foc_fw_state_t;

void foc_run_fw(foc_fw_state_t *state, const foc_fw_params_t *params, bool mode_allows, float dt);

/*
 * MTPA, reference mcpwm_foc.c:3627-3640, where it is an inline block rather than a function:
 * maximum-torque-per-ampere reprojection of the q setpoint onto both axes. The mode values
 * are the reference's MTPA_MODE enum (datatypes.h:372-375).
 */
#define FOC_MTPA_MODE_OFF 0u
#define FOC_MTPA_MODE_IQ_TARGET 1u
#define FOC_MTPA_MODE_IQ_MEASURED 2u

void foc_apply_mtpa(uint8_t mtpa_mode, float ld_lq_diff, float lambda, float iq_filter,
                    float *iq_set, float *id_set);

/*
 * Battery state of charge and remaining watt-hours, reference mc_interface_get_battery_level()
 * (motor/mc_interface.c) and utils_batt_liion_norm_v_to_capacity (util/utils_math.c). The
 * battery types are the reference's enum order (datatypes.h BATTERY_TYPE); they live here
 * rather than in a consumer's header because app/foc_core does not depend on other apps.
 */
#define FOC_BATTERY_TYPE_LIION_3_0__4_2 0u
#define FOC_BATTERY_TYPE_LIIRON_2_6__3_6 1u
#define FOC_BATTERY_TYPE_LEAD_ACID 2u

float foc_batt_liion_norm_v_to_capacity(float norm_v);

/* Returns ampere-hours left over ampere-hours total, and writes the remaining watt-hours
 * through wh_left when it is not NULL (the reference's own output pair). */
float foc_battery_level(uint8_t battery_type, int cells, float battery_ah, float v_in,
                        float *wh_left);

/* Ortega flux observer state */
typedef struct foc_observer {
    float x1;
    float x2;
    float lambda_est;
    float phase;
    /* Last currents, for the observers that integrate the voltage minus the
     * resistive drop (reference: observer_state.i_alpha_last). */
    float i_alpha_last;
    float i_beta_last;
} foc_observer_t;

void foc_observer_init(foc_observer_t *obs, float initial_lambda);
/*
 * The electrical-parameter compensation the reference performs at the top of
 * foc_observer_update (motor/foc_math.c:34-76), extracted here as a pure function:
 * the observer keeps taking explicit R/L/lambda, and whoever calls it owns the
 * decision about how the machine's parameters change with load and heat.
 *
 * Inputs mirror the reference's sources: `ld_lq_diff`/`l_current_max`/`sat_comp`
 * come from the configuration, `i_abs_filter`/`id`/`iq` from the measured state,
 * `lambda_est` from the observer's own flux estimate, `r_temp_comp` from the
 * temperature model. `type` matters because the lambda-scaled branch only applies
 * to the observers that track a flux estimate.
 */
void foc_observer_adjust_params(float r_ohm, float l_henry, float lambda_wb, float ld_lq_diff,
                                float id, float iq, float i_abs_filter, float l_current_max,
                                float lambda_est, float sat_comp, foc_sat_comp_mode_t sat_mode,
                                float r_temp_comp, bool temp_comp, foc_observer_type_t type,
                                float *r_out, float *l_out, float *lambda_out);

/*
 * The temperature model's factor for one cycle: 1.0 + 0.00386 * (motor_temp - base_temp), the
 * expression from the reference's timer_update (mcpwm_foc.c:3942). The caller applies it to the
 * motor resistance and to the current loop's ki, and owns the reference's -30 degC floor.
 */
float foc_temp_comp_factor(float motor_temp_c, float base_temp_c);

/*
 * HFI's angle state. The reference's hfi_state_t carries much more - the excitation table, the
 * sample buffers, the flip counters - and only what the ported pieces touch is here; the rest
 * arrives with the excitation and sampling that own them.
 */
typedef struct foc_hfi_state {
    float angle;
    float double_integrator;
    bool ready;
    /* The sample table the configuration selects: how many samples the transform runs over and how
     * many steps of the reference's table one of them advances, which the state machine reads. */
    int samples;
    int table_fact;
    /* The reference's hfi_state_t, the fields its six-vector path and tracking half read. The
     * V2/V3 and V4/V5 fields (sign_last_sample, cos_last, sin_last, prev_sample_d) are absent with
     * those modes: they sample at an instant this port has no contract for, so it refuses them
     * rather than approximating them, and a field nothing reads would be dead state. */
    float buffer[32];
    float buffer_current[32];
    int ind;
    bool is_samp_n;
    float prev_sample;
    int est_done_cnt;
    float observer_zero_time;
    int flip_cnt;
} foc_hfi_state_t;

/*
 * Reference util/utils_math.h:229 utils_saturate_vector_2d: scale a 2D vector down to a maximum
 * magnitude, leaving it alone when it is already inside. The magnitude has a floor so a vector at
 * the origin cannot divide by zero, and max is taken as an absolute value. True when it scaled.
 */
bool foc_saturate_vector_2d(float *x, float *y, float max);

/*
 * What the excitation reads. These are the reference's own inputs at mcpwm_foc.c:4788-4994 - the
 * configuration's HFI voltages and frequency, the measured currents and iq, the bus and duty - so
 * the excitation itself stays a pure function of them and the state it owns.
 */
typedef struct foc_hfi_excite_in {
    float f_zv;
    float hfi_voltage_start;
    float hfi_voltage_run;
    float hfi_voltage_max;
    float current_max;
    float iq;
    float v_bus;
    float duty_now;
    float i_alpha;
    float i_beta;
    int start_samples;
} foc_hfi_excite_in_t;

/*
 * Reference mcpwm_foc.c:4808-4815: the excitation voltage, either the start-up voltage while the
 * estimate is still warming up or a ramp from foc_hfi_voltage_run to foc_hfi_voltage_max across
 * the measured |iq|, truncated by how much of the bus the current duty leaves free.
 */
float foc_hfi_voltage(const foc_hfi_state_t *hfi, const foc_hfi_excite_in_t *in);

/*
 * Reference mcpwm_foc.c:4939-4970, the six-vector branch of the injected-ADC HFI block: every
 * cycle either samples the current along the injected frame's table angle and accumulates it, or
 * stores the previous sample and drives the opposite voltage - and the two halves alternate
 * through is_samp_n, which the reference toggles once after the branch, here too. The voltage is
 * added to the caller's alpha/beta in volts, which is the same place the reference's
 * `mod_alpha_v7 += hfi_voltage * c * voltage_normalize` puts it: its modulation space is this
 * port's voltage space times 1.5/v_bus.
 *
 * This is the branch FOC_AMB_MODE_SIX_VECTOR (the default) and FOC_SENSOR_MODE_HFI_START take. The
 * V2/V3 and V4/V5 branches sample at another instant than this port's current contract offers, so
 * they are not ported.
 */
void foc_hfi_excite_six_vector(foc_hfi_state_t *hfi, const foc_hfi_excite_in_t *in, float *v_alpha,
                               float *v_beta);

/*
 * What the tracking half distinguishes among the reference's six HFI sensor modes. It is not the
 * configuration's enum: three of those values mean the same thing here as each other (the modes
 * whose update happens in the interrupt with a current this port has no contract for are refused),
 * and a caller that had to hand over the raw value would be handing over a numbering this app does
 * not own. The glue maps mc_foc_sensor_mode onto these.
 */
typedef enum {
    FOC_HFI_MODE_TRACK = 0, /* FOC_SENSOR_MODE_HFI: track the angle, do not touch the observer */
    FOC_HFI_MODE_START,   /* FOC_SENSOR_MODE_HFI_START: seeds the observer on the first estimate */
    FOC_HFI_MODE_REFUSED, /* HFI_V2..HFI_V5: their branch is not ported */
} foc_hfi_mode_t;

/*
 * What the tracking half reads: the configuration's HFI speed and sample thresholds, the two angles
 * it hands over between, the voltage frequency its compensation needs, and the flux linkage the
 * HFI_START seed writes into the observer.
 */
typedef struct foc_hfi_update_in {
    foc_hfi_mode_t mode;
    bool amb_mode_six_vector;
    bool control_sample_mode_v0_v7;
    int start_samples;
    float sl_erpm_hfi;
    float speed_est_fast;
    float phase_now_observer;
    float pll_speed;
    float f_zv;
    float flux_linkage;
} foc_hfi_update_in_t;

/*
 * Reference mcpwm_foc.c:4218-4310 hfi_update, the thread half: it turns the sample buffer the
 * injected interrupt filled into an angle. Above foc_sl_erpm_hfi the angle is the observer's and
 * the tracker's integrator is tied to the fast speed estimate, so HFI hands over as the motor
 * speeds up; below it the buffer's second harmonic is halved into an angle, compensated for the
 * half buffer of lag, disambiguated from its 180-degree twin against the angle already held, and
 * the flip tally decides whether that twin is the right one while the estimate is still warming up.
 * In HFI_START the first estimate also seeds the observer with the flux linkage it is tracking.
 *
 * The reference's dt is unused (its thread ignores it) and its plotting hooks are not ported.
 */
void foc_hfi_update(foc_hfi_state_t *hfi, const foc_hfi_update_in_t *in, foc_observer_t *observer);

/*
 * The sample-table selection, reference mcpwm_foc.c:133-166 update_hfi_samples. The reference
 * memsets the whole state and then sets the two fields, so a run of this resets the tracker with
 * them - which is what the reference does when the configuration changes under it. A value the
 * three cases do not cover leaves both at zero, exactly as that switch without a default does; the
 * consumer of the two numbers is the state machine, which is the next piece of this port.
 */
void foc_hfi_configure(foc_hfi_state_t *hfi, uint8_t foc_hfi_samples);

/*
 * Reference util/utils_math.h:273 utils_angle_difference_rad, literally: the difference wrapped
 * into
 * [-pi, pi] by the two while loops rather than by a modulo, with the reference's double M_PI. HFI's
 * angle tracker chooses between the two bins with it, and uses it again for the flip test.
 */
float foc_angle_difference(float angle1, float angle2);

/*
 * Reference foc_math.c:766 foc_hfi_adjust_angle: HFI's angle tracker. A proportional term and a
 * double integrator pull the injected frame's angle towards the error the current measurement
 * reports, and the double integrator is bounded by the fast speed estimate - which is what keeps
 * the tracker from running away as the motor speeds up. max_err and gain are the configuration's
 * foc_hfi_max_err and foc_hfi_gain; speed_est_fast is the caller's m_speed_est_fast.
 */
void foc_hfi_adjust_angle(float ang_err, float max_err, float gain, float speed_est_fast, float dt,
                          foc_hfi_state_t *state);

/*
 * Reference util/utils_math.c:509-597, the DFT bins HFI computes its angle error from: bin 0 is
 * the mean, bins 1 and 2 the fundamental and second harmonic of the sample buffer, each over the
 * 8, 16 or 32 samples the configuration's foc_hfi_samples selects. They are copied rather than
 * replaced with a real FFT because they are exactly what the reference runs and they feed a
 * control loop.
 *
 * The tables they index are generated from the reference by tools/gen_hfi_from_reference.py: they
 * are rounded six-decimal literals, so cosf/sinf would not produce the same numbers.
 */
void foc_fft32_bin0(const float *real_in, float *real, float *imag);
void foc_fft32_bin1(const float *real_in, float *real, float *imag);
void foc_fft32_bin2(const float *real_in, float *real, float *imag);
void foc_fft16_bin0(const float *real_in, float *real, float *imag);
void foc_fft16_bin1(const float *real_in, float *real, float *imag);
void foc_fft16_bin2(const float *real_in, float *real, float *imag);
void foc_fft8_bin0(const float *real_in, float *real, float *imag);
void foc_fft8_bin1(const float *real_in, float *real, float *imag);
void foc_fft8_bin2(const float *real_in, float *real, float *imag);

void foc_observer_update(foc_observer_t *obs, float v_alpha, float v_beta, float i_alpha,
                         float i_beta, float dt, float r_ohm, float l_henry, float lambda_wb,
                         float gamma, foc_observer_type_t type);

/* Virtual motor physical simulation state */
typedef struct foc_virtual_motor {
    float r_ohm;
    float l_henry;
    /* Reference motor/virtual_motor.c:36-37 and :138-143: the d and q axis inductances and the
     * difference they are built from. With no difference both are l_henry and every equation below
     * reduces to the single-inductance form it had before, which is what keeps the existing
     * measurements' goldens exactly where they were. */
    float ld_lq_diff;
    float ld;
    float lq;
    float lambda_wb;
    int pole_pairs;
    float inertia;
    float friction;

    float ia;
    float ib;
    float ic;
    float i_alpha;
    float i_beta;
    float id;
    float iq;

    float rotor_angle_rad;
    float rotor_speed_rad_s;
} foc_virtual_motor_t;

void foc_virtual_motor_init(foc_virtual_motor_t *vm, float r_ohm, float l_henry, float lambda_wb,
                            int pole_pairs, float inertia);

/*
 * Reference motor/virtual_motor.c:138-143: the saliency is the configuration's
 * foc_motor_ld_lq_diff, and the two axis inductances are l +/- half of it. HFI needs a salient
 * machine to produce the second harmonic it tracks, so the closed-loop test asks for one here.
 */
void foc_virtual_motor_set_saliency(foc_virtual_motor_t *vm, float ld_lq_diff);
void foc_virtual_motor_step(foc_virtual_motor_t *vm, float va, float vb, float vc, float dt,
                            float load_torque);

#ifdef __cplusplus
}
#endif

#endif /* FOC_MATH_H */
