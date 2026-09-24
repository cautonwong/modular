#include "foc_core/foc_math.h"
#include <math.h>

/*
 * Reference firmware: util/utils_math.c `utils_fast_sincos_better`. The FOC ISR
 * uses this variant, not the one-pass `utils_fast_sincos`: the parabola fit alone
 * is off by up to 0.056, and the 0.225 refinement pass brings that down to 0.0011.
 * The residual approximation error is therefore part of the reference behaviour -
 * replacing this with sinf/cosf changes the Park transform, and the Park transform
 * feeds the current loop.
 * Kept bit-compatible with the reference on purpose - the Park transform feeds
 * the current loop, so a different sine is a different loop gain.
 */
void foc_fast_sincos(float angle_rad, float *sin_out, float *cos_out) {
    /* Always wrap input angle to -PI..PI */
    while (angle_rad < -(float)M_PI) {
        angle_rad += 2.0f * (float)M_PI;
    }
    while (angle_rad > (float)M_PI) {
        angle_rad -= 2.0f * (float)M_PI;
    }

    /* Compute sine */
    if (angle_rad < 0.0f) {
        *sin_out = 1.27323954f * angle_rad + 0.405284735f * angle_rad * angle_rad;
        *sin_out = 0.225f * (*sin_out * -*sin_out - *sin_out) + *sin_out;
    } else {
        *sin_out = 1.27323954f * angle_rad - 0.405284735f * angle_rad * angle_rad;

        if (*sin_out < 0.0f) {
            *sin_out = 0.225f * (*sin_out * -*sin_out - *sin_out) + *sin_out;
        } else {
            *sin_out = 0.225f * (*sin_out * *sin_out - *sin_out) + *sin_out;
        }
    }

    /* Compute cosine: sin(x + PI/2) = cos(x) */
    angle_rad += 0.5f * (float)M_PI;
    if (angle_rad > (float)M_PI) {
        angle_rad -= 2.0f * (float)M_PI;
    }

    if (angle_rad < 0.0f) {
        *cos_out = 1.27323954f * angle_rad + 0.405284735f * angle_rad * angle_rad;

        if (*cos_out < 0.0f) {
            *cos_out = 0.225f * (*cos_out * -*cos_out - *cos_out) + *cos_out;
        } else {
            *cos_out = 0.225f * (*cos_out * *cos_out - *cos_out) + *cos_out;
        }
    } else {
        *cos_out = 1.27323954f * angle_rad - 0.405284735f * angle_rad * angle_rad;

        if (*cos_out < 0.0f) {
            *cos_out = 0.225f * (*cos_out * -*cos_out - *cos_out) + *cos_out;
        } else {
            *cos_out = 0.225f * (*cos_out * *cos_out - *cos_out) + *cos_out;
        }
    }
}

void foc_clarke_transform(float ia, float ib, float ic, float *i_alpha, float *i_beta) {
    (void)ic; /* Satisfies ia + ib + ic = 0 constraint */
    *i_alpha = ia;
    *i_beta = (ia + 2.0f * ib) * ONE_BY_SQRT3;
}

void foc_park_transform(float i_alpha, float i_beta, float sin_th, float cos_th, float *id,
                        float *iq) {
    *id = i_alpha * cos_th + i_beta * sin_th;
    *iq = -i_alpha * sin_th + i_beta * cos_th;
}

void foc_inv_park_transform(float vd, float vq, float sin_th, float cos_th, float *v_alpha,
                            float *v_beta) {
    *v_alpha = vd * cos_th - vq * sin_th;
    *v_beta = vd * sin_th + vq * cos_th;
}

void foc_deadtime_comp(float ia, float ib, float ic, float dt_comp_v, float *va_comp,
                       float *vb_comp, float *vc_comp) {
    if (va_comp) {
        *va_comp = (ia > 0.1f) ? dt_comp_v : ((ia < -0.1f) ? -dt_comp_v : 0.0f);
    }
    if (vb_comp) {
        *vb_comp = (ib > 0.1f) ? dt_comp_v : ((ib < -0.1f) ? -dt_comp_v : 0.0f);
    }
    if (vc_comp) {
        *vc_comp = (ic > 0.1f) ? dt_comp_v : ((ic < -0.1f) ? -dt_comp_v : 0.0f);
    }
}

float foc_calc_mtpa_id(float iq, float ld_h, float lq_h, float lambda_wb) {
    float ld_lq_diff = ld_h - lq_h;
    if (fabsf(ld_lq_diff) < 1e-9f || fabsf(iq) < 1e-4f) {
        return 0.0f;
    }
    float num = -lambda_wb + sqrtf(SQ(lambda_wb) + 8.0f * SQ(ld_lq_diff) * SQ(iq));
    float den = 4.0f * ld_lq_diff;
    return num / den;
}

void foc_svpwm(float v_alpha, float v_beta, float v_bus, float *duty_a, float *duty_b,
               float *duty_c, uint32_t *sector_out) {
    if (v_bus <= 0.001f) {
        *duty_a = 0.5f;
        *duty_b = 0.5f;
        *duty_c = 0.5f;
        if (sector_out != (void *)0) {
            *sector_out = 1u;
        }
        return;
    }

    /* Normalize modulation vector */
    float mod_alpha = v_alpha / (v_bus * SQRT3_BY_2);
    float mod_beta = v_beta / (v_bus * SQRT3_BY_2);

    /* Clamp modulation index to 1.0 (overmodulation prevention) */
    float mod_mag_sq = SQ(mod_alpha) + SQ(mod_beta);
    if (mod_mag_sq > 1.0f) {
        float inv_mag = 1.0f / sqrtf(mod_mag_sq);
        mod_alpha *= inv_mag;
        mod_beta *= inv_mag;
    }

    uint32_t sector = 1u;
    if (mod_beta >= 0.0f) {
        if (mod_alpha >= 0.0f) {
            if (ONE_BY_SQRT3 * mod_beta > mod_alpha) {
                sector = 2u;
            } else {
                sector = 1u;
            }
        } else {
            if (-ONE_BY_SQRT3 * mod_beta > mod_alpha) {
                sector = 3u;
            } else {
                sector = 2u;
            }
        }
    } else {
        if (mod_alpha >= 0.0f) {
            if (-ONE_BY_SQRT3 * mod_beta > mod_alpha) {
                sector = 5u;
            } else {
                sector = 6u;
            }
        } else {
            if (ONE_BY_SQRT3 * mod_beta > mod_alpha) {
                sector = 4u;
            } else {
                sector = 5u;
            }
        }
    }

    if (sector_out != (void *)0) {
        *sector_out = sector;
    }

    float t1 = 0.0f;
    float t2 = 0.0f;
    float da = 0.5f;
    float db = 0.5f;
    float dc = 0.5f;

    switch (sector) {
    case 1u:
        t1 = mod_alpha - ONE_BY_SQRT3 * mod_beta;
        t2 = TWO_BY_SQRT3 * mod_beta;
        da = (1.0f + t1 + t2) * 0.5f;
        db = da - t1;
        dc = db - t2;
        break;
    case 2u:
        t1 = mod_alpha + ONE_BY_SQRT3 * mod_beta;
        t2 = -mod_alpha + ONE_BY_SQRT3 * mod_beta;
        db = (1.0f + t1 + t2) * 0.5f;
        da = db - t2;
        dc = da - t1;
        break;
    case 3u:
        t1 = TWO_BY_SQRT3 * mod_beta;
        t2 = -mod_alpha - ONE_BY_SQRT3 * mod_beta;
        db = (1.0f + t1 + t2) * 0.5f;
        dc = db - t1;
        da = dc - t2;
        break;
    case 4u:
        t1 = -mod_alpha + ONE_BY_SQRT3 * mod_beta;
        t2 = -TWO_BY_SQRT3 * mod_beta;
        dc = (1.0f + t1 + t2) * 0.5f;
        db = dc - t2;
        da = db - t1;
        break;
    case 5u:
        t1 = -mod_alpha - ONE_BY_SQRT3 * mod_beta;
        t2 = mod_alpha - ONE_BY_SQRT3 * mod_beta;
        dc = (1.0f + t1 + t2) * 0.5f;
        da = dc - t1;
        db = da - t2;
        break;
    case 6u:
    default:
        t1 = -TWO_BY_SQRT3 * mod_beta;
        t2 = mod_alpha + ONE_BY_SQRT3 * mod_beta;
        da = (1.0f + t1 + t2) * 0.5f;
        dc = da - t2;
        db = dc - t1;
        break;
    }

    /* Clamp duty cycles to [0.0, 1.0] */
    if (da < 0.0f)
        da = 0.0f;
    else if (da > 1.0f)
        da = 1.0f;
    if (db < 0.0f)
        db = 0.0f;
    else if (db > 1.0f)
        db = 1.0f;
    if (dc < 0.0f)
        dc = 0.0f;
    else if (dc > 1.0f)
        dc = 1.0f;

    *duty_a = da;
    *duty_b = db;
    *duty_c = dc;
}

void foc_observer_init(foc_observer_t *obs, float initial_lambda) {
    obs->x1 = initial_lambda;
    obs->x2 = 0.0f;
    obs->lambda_est = initial_lambda;
    obs->phase = 0.0f;
    obs->speed_rad_s = 0.0f;
}

void foc_observer_update(foc_observer_t *obs, float v_alpha, float v_beta, float i_alpha,
                         float i_beta, float dt, float r_ohm, float l_henry, float lambda_wb,
                         float gamma) {
    float l_ia = l_henry * i_alpha;
    float l_ib = l_henry * i_beta;
    float r_ia = r_ohm * i_alpha;
    float r_ib = r_ohm * i_beta;

    float err = SQ(lambda_wb) - (SQ(obs->x1 - l_ia) + SQ(obs->x2 - l_ib));
    if (err > 0.0f) {
        err = 0.0f;
    }

    float gamma_half = gamma * 0.5f;
    float x1_dot = v_alpha - r_ia + gamma_half * (obs->x1 - l_ia) * err;
    float x2_dot = v_beta - r_ib + gamma_half * (obs->x2 - l_ib) * err;

    obs->x1 += x1_dot * dt;
    obs->x2 += x2_dot * dt;

    /*
     * Reference firmware: mcpwm_foc.c foc_observer_update(), FOC_OBSERVER_ORTEGA_ORIGINAL.
     * Both guards are missing in a naive port and both matter: once x1/x2 is NaN it
     * stays NaN for the lifetime of the observer, and a flux vector that collapses
     * towards zero makes atan2 jump by half a turn on noise alone.
     */
    obs->x1 = (obs->x1 != obs->x1) ? 0.0f : obs->x1;
    obs->x2 = (obs->x2 != obs->x2) ? 0.0f : obs->x2;

    /* Prevent the magnitude from getting too low, as that makes the angle very unstable. */
    float flux_mag = sqrtf(SQ(obs->x1) + SQ(obs->x2));
    if (flux_mag < (lambda_wb * 0.5f)) {
        obs->x1 *= 1.1f;
        obs->x2 *= 1.1f;
    }

    float psi_alpha = obs->x1 - l_ia;
    float psi_beta = obs->x2 - l_ib;

    float last_phase = obs->phase;
    obs->phase = atan2f(psi_beta, psi_alpha);

    float d_phase = obs->phase - last_phase;
    while (d_phase > (float)M_PI)
        d_phase -= 2.0f * (float)M_PI;
    while (d_phase < -(float)M_PI)
        d_phase += 2.0f * (float)M_PI;

    if (dt > 0.000001f) {
        obs->speed_rad_s = d_phase / dt;
    }
}

void foc_virtual_motor_init(foc_virtual_motor_t *vm, float r_ohm, float l_henry, float lambda_wb,
                            int pole_pairs, float inertia) {
    vm->r_ohm = r_ohm;
    vm->l_henry = l_henry;
    vm->lambda_wb = lambda_wb;
    vm->pole_pairs = pole_pairs;
    vm->inertia = (inertia > 0.000001f) ? inertia : 0.0005f;
    vm->friction = 0.0001f;

    vm->ia = 0.0f;
    vm->ib = 0.0f;
    vm->ic = 0.0f;
    vm->i_alpha = 0.0f;
    vm->i_beta = 0.0f;
    vm->id = 0.0f;
    vm->iq = 0.0f;

    vm->rotor_angle_rad = 0.0f;
    vm->rotor_speed_rad_s = 0.0f;
}

void foc_virtual_motor_step(foc_virtual_motor_t *vm, float v_alpha, float v_beta, float unused,
                            float dt, float load_torque) {
    (void)unused;
    float sin_phi = sinf(vm->rotor_angle_rad);
    float cos_phi = cosf(vm->rotor_angle_rad);

    /* 1. Park transform on voltages */
    float vd = cos_phi * v_alpha + sin_phi * v_beta;
    float vq = cos_phi * v_beta - sin_phi * v_alpha;

    /* 2. Electrical differential equations */
    float we = vm->rotor_speed_rad_s * (float)vm->pole_pairs;

    float did_dt = (vd + we * vm->l_henry * vm->iq - vm->r_ohm * vm->id) / vm->l_henry;
    float diq_dt =
        (vq - we * (vm->l_henry * vm->id + vm->lambda_wb) - vm->r_ohm * vm->iq) / vm->l_henry;

    vm->id += did_dt * dt;
    vm->iq += diq_dt * dt;

    /* 3. Electromagnetic torque */
    float t_elec = 1.5f * (float)vm->pole_pairs * vm->lambda_wb * vm->iq;

    /* 4. Mechanical acceleration */
    float d_omega_dt = (t_elec - load_torque - vm->friction * vm->rotor_speed_rad_s) / vm->inertia;
    vm->rotor_speed_rad_s += d_omega_dt * dt;

    /* 5. Rotor angle update */
    vm->rotor_angle_rad += we * dt;
    while (vm->rotor_angle_rad > (float)M_PI)
        vm->rotor_angle_rad -= 2.0f * (float)M_PI;
    while (vm->rotor_angle_rad < -(float)M_PI)
        vm->rotor_angle_rad += 2.0f * (float)M_PI;

    /* 6. Inverse Park & Clarke for phase current feedback */
    vm->i_alpha = cos_phi * vm->id - sin_phi * vm->iq;
    vm->i_beta = cos_phi * vm->iq + sin_phi * vm->id;

    vm->ia = vm->i_alpha;
    vm->ib = -0.5f * vm->i_alpha + SQRT3_BY_2 * vm->i_beta;
    vm->ic = -vm->ia - vm->ib;
}
