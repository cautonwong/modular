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

/* Fast trigonometric utilities */
void foc_fast_sincos(float angle_rad, float *sin_out, float *cos_out);

/*
 * Reference: util/utils_math.c utils_fast_atan2 - a polynomial approximation, not
 * atan2f. The observer feeds this angle straight into the Park transform, so the
 * approximation error (up to ~0.003 rad) is part of the reference behaviour.
 */
float foc_fast_atan2(float y, float x);

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
