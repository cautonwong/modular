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

/* Vector transformations */
void foc_clarke_transform(float ia, float ib, float ic, float *i_alpha, float *i_beta);
void foc_park_transform(float i_alpha, float i_beta, float sin_th, float cos_th, float *id,
                        float *iq);
void foc_inv_park_transform(float vd, float vq, float sin_th, float cos_th, float *v_alpha,
                            float *v_beta);

/* Space Vector PWM (SVPWM) */
void foc_svpwm(float v_alpha, float v_beta, float v_bus, float *duty_a, float *duty_b,
               float *duty_c, uint32_t *sector_out);

/* Ortega flux observer state */
typedef struct foc_observer {
    float x1;
    float x2;
    float lambda_est;
    float phase;
    float speed_rad_s;
} foc_observer_t;

void foc_observer_init(foc_observer_t *obs, float initial_lambda);
void foc_observer_update(foc_observer_t *obs, float v_alpha, float v_beta, float i_alpha,
                         float i_beta, float dt, float r_ohm, float l_henry, float lambda_wb,
                         float gamma);

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
