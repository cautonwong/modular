#ifndef FOC_MATH_H
#define FOC_MATH_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846f
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

void foc_observer_update(foc_observer_t *obs, float v_alpha, float v_beta, float i_alpha,
                         float i_beta, float dt, float r_ohm, float l_henry, float lambda_wb,
                         float gamma, foc_observer_type_t type);

/* Virtual motor physical simulation state */
typedef struct foc_virtual_motor {
    float r_ohm;
    float l_henry;
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
void foc_virtual_motor_step(foc_virtual_motor_t *vm, float va, float vb, float vc, float dt,
                            float load_torque);

#ifdef __cplusplus
}
#endif

#endif /* FOC_MATH_H */
