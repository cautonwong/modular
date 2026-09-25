#include "foc_core/foc_math.h"
#include <math.h>

/*
 * Reference firmware: util/utils_math.c `utils_fast_sincos_better`. The FOC ISR
 * uses this variant, not the one-pass `utils_fast_sincos`: the parabola fit alone
 * is off by up to 0.056, and the 0.225 refinement pass brings that down to 0.0011.
 * That residual error is part of the reference behaviour and is kept: this feeds
 * the Park transform, so a different sine is a different loop gain.
 */
void foc_fast_sincos(float angle_rad, float *sin_out, float *cos_out) {
    /* Always wrap input angle to -PI..PI, with the reference's double literals. */
    while (angle_rad < -M_PI) {
        angle_rad += 2.0 * M_PI;
    }
    while (angle_rad > M_PI) {
        angle_rad -= 2.0 * M_PI;
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
    angle_rad += 0.5 * M_PI;
    if (angle_rad > M_PI) {
        angle_rad -= 2.0 * M_PI;
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

static float obs_truncate(float v, float min, float max) {
    if (v > max) {
        return max;
    }
    if (v < min) {
        return min;
    }
    return v;
}

static float obs_truncate_abs(float v, float max) {
    if (v > max) {
        return max;
    }
    if (v < -max) {
        return -max;
    }
    return v;
}

void foc_pll_run(foc_pll_t *pll, float phase, float dt, float kp, float ki) {
    pll->phase = (pll->phase != pll->phase) ? 0.0f : pll->phase; /* UTILS_NAN_ZERO */

    float delta_theta = phase - pll->phase;
    /* utils_norm_angle_rad, literally: the reference's macro uses double literals,
     * so the wraps are computed in double and rounded back to float. Using float
     * constants here shifts the speed integrator by ~1e-4 over a few thousand
     * steps (measured against the reference). */
    while (delta_theta < -M_PI) {
        delta_theta += 2.0 * M_PI;
    }
    while (delta_theta >= M_PI) {
        delta_theta -= 2.0 * M_PI;
    }

    pll->speed = (pll->speed != pll->speed) ? 0.0f : pll->speed;

    pll->phase += (pll->speed + kp * delta_theta) * dt;
    while (pll->phase < -M_PI) {
        pll->phase += 2.0 * M_PI;
    }
    while (pll->phase >= M_PI) {
        pll->phase -= 2.0 * M_PI;
    }

    pll->speed += ki * delta_theta * dt;
}

float foc_fast_atan2(float y, float x) {
    /* Constants and their types are the reference's: it evaluates this polynomial
     * in double (util/utils_math.c utils_fast_atan2), and narrowing them to float
     * changes the result in the last few bits. */
    float abs_y = fabsf(y) + 1e-20; /* kludge to prevent 0/0 condition */

    float angle;
    if (x >= 0.0f) {
        float r = (x - abs_y) / (x + abs_y);
        float rsq = r * r;
        angle = ((0.1963 * rsq) - 0.9817) * r + (M_PI / 4.0);
    } else {
        float r = (x + abs_y) / (abs_y - x);
        float rsq = r * r;
        angle = ((0.1963 * rsq) - 0.9817) * r + (3.0 * M_PI / 4.0);
    }

    angle = (angle != angle) ? 0.0f : angle; /* UTILS_NAN_ZERO */

    return (y < 0.0f) ? -angle : angle;
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

void foc_svpwm(float v_alpha, float v_beta, float v_bus, float duty_max, float *duty_a,
               float *duty_b, float *duty_c, uint32_t *sector_out) {
    if (v_bus <= 0.001f) {
        *duty_a = 0.5f;
        *duty_b = 0.5f;
        *duty_c = 0.5f;
        if (sector_out != (void *)0) {
            *sector_out = 1u;
        }
        return;
    }

    /*
     * Normalise the voltage vector the way the reference does: mod = 1.5 * v / v_bus,
     * i.e. 1.0 is the largest vector the inverter can produce (mcpwm_foc.c:3808,
     * "voltage_normalize = 1/(2/3*V_bus)"). Dividing by (v_bus * sqrt(3)/2) instead,
     * as this did, scales the modulation by 1/sqrt(3): the motor gets 77% of the
     * commanded voltage and hits the duty ceiling correspondingly early.
     */
    float mod_alpha = v_alpha * 1.5f / v_bus;
    float mod_beta = v_beta * 1.5f / v_bus;

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

    /* Per-phase clamp, as the reference does it: t_max = top * (1 - (1 - max_mod) * 0.5)
     * (motor/foc_math.c:374). A magnitude clamp on the vector instead is a different
     * limiter and produces different duties once saturated. */
    float t_max = 1.0f - (1.0f - duty_max) * 0.5f;
    if (da < 0.0f)
        da = 0.0f;
    else if (da > t_max)
        da = t_max;
    if (db < 0.0f)
        db = 0.0f;
    else if (db > t_max)
        db = t_max;
    if (dc < 0.0f)
        dc = 0.0f;
    else if (dc > t_max)
        dc = t_max;

    *duty_a = da;
    *duty_b = db;
    *duty_c = dc;
}

/*
 * Reference: the block at the top of foc_observer_update, motor/foc_math.c:34-76.
 * Literals keep the reference's types (0.1 and 2.0 are doubles there), because
 * narrowing them moves the result in the last bits - the same lesson as sincos and
 * atan2.
 */
void foc_observer_adjust_params(float r_ohm, float l_henry, float lambda_wb, float ld_lq_diff,
                                float id, float iq, float i_abs_filter, float l_current_max,
                                float lambda_est, float sat_comp, foc_sat_comp_mode_t sat_mode,
                                float r_temp_comp, bool temp_comp, foc_observer_type_t type,
                                float *r_out, float *l_out, float *lambda_out) {
    float r = r_ohm;
    float l = l_henry;
    float lambda = lambda_wb;

    /* The reference's condition is a chain of >= tests over the ordered observer
     * enum, where the first term already covers the rest; written once here. */
    bool lambda_tracking = (type >= FOC_OBSERVER_ORTEGA_LAMBDA_COMP);

    switch (sat_mode) {
    case FOC_SAT_COMP_LAMBDA:
        if (lambda_tracking) {
            l = l * (lambda_est / lambda);
        }
        break;

    case FOC_SAT_COMP_FACTOR: {
        const float comp_fact = sat_comp * (i_abs_filter / l_current_max);
        l -= l * comp_fact;
        lambda -= lambda * comp_fact;
        break;
    }

    case FOC_SAT_COMP_LAMBDA_AND_FACTOR: {
        if (lambda_tracking) {
            l = l * (lambda_est / lambda);
        }
        const float comp_fact = sat_comp * (i_abs_filter / l_current_max);
        l -= l * comp_fact;
        break;
    }

    default:
        break;
    }

    /* Temperature compensation: the resistance comes from the temperature model. */
    if (temp_comp) {
        r = r_temp_comp;
    }

    /* Adjust inductance for saliency. */
    if (fabsf(id) > 0.1 || fabsf(iq) > 0.1) {
        l = l - ld_lq_diff / 2.0 + ld_lq_diff * SQ(iq) / (SQ(id) + SQ(iq));
    }

    *r_out = r;
    *l_out = l;
    *lambda_out = lambda;
}

/*
 * Reference: motor/foc_math.c:492 foc_run_pid_control_speed, copied step for step.
 * The literals and the filter form are the reference's: `1.0 / 20.0` is a double
 * there, and UTILS_LP_FAST is `value -= c * (value - sample)`, not the algebraically
 * equal `value += c * (sample - value)`. Both differences are visible in the last
 * bits, which is what the differential harness compares.
 */
void foc_run_pid_speed(foc_speed_pid_t *pid, const foc_speed_pid_params_t *params,
                       bool in_speed_mode, bool index_found, float rpm, float rpm_command, float dt,
                       float *iq_set) {
    if (!in_speed_mode) {
        pid->i_term = 0.0f;
        pid->prev_error = 0.0f;
        pid->d_filter = 0.0f;
        return;
    }

    if (params->ramp_erpms_s > 0.0f) {
        float step = params->ramp_erpms_s * dt;
        if (pid->set_rpm < rpm_command) {
            pid->set_rpm =
                (pid->set_rpm + step < rpm_command) ? (pid->set_rpm + step) : rpm_command;
        } else if (pid->set_rpm > rpm_command) {
            pid->set_rpm =
                (pid->set_rpm - step > rpm_command) ? (pid->set_rpm - step) : rpm_command;
        }

        if (!index_found) {
            pid->set_rpm = obs_truncate_abs(pid->set_rpm, params->openloop_rpm);
        }

        if (params->invert_direction) {
            pid->set_rpm = obs_truncate(pid->set_rpm, -params->l_max_erpm, -params->l_min_erpm);
        } else {
            pid->set_rpm = obs_truncate(pid->set_rpm, params->l_min_erpm, params->l_max_erpm);
        }
    }

    float error = pid->set_rpm - rpm;

    /* Too low a setpoint: reset the loop, release the motor. */
    if (fabsf(pid->set_rpm) < params->min_erpm) {
        pid->i_term = 0.0f;
        pid->prev_error = error;
        *iq_set = 0.0f;
        return;
    }

    float p_term = error * params->kp * (1.0 / 20.0);
    float d_term = (error - pid->prev_error) * (params->kd / dt) * (1.0 / 20.0);

    pid->d_filter -= params->kd_filter * (pid->d_filter - d_term);
    d_term = pid->d_filter;

    pid->prev_error = error;

    float output = p_term + pid->i_term + d_term;
    output = obs_truncate_abs(output, 1.0f);

    /* Integrator wind-up protection. */
    pid->i_term += error * params->ki * dt * (1.0 / 20.0);
    pid->i_term = obs_truncate_abs(pid->i_term, 1.0f);

    if (params->ki < 1e-9f) {
        pid->i_term = 0.0f;
    }

    if (!params->allow_braking) {
        if (rpm > 20.0f && output < 0.0f) {
            output = 0.0f;
        }
        if (rpm < -20.0f && output > 0.0f) {
            output = 0.0f;
        }
    }

    *iq_set = output * params->lo_current_max * params->current_max_scale;
}

void foc_observer_init(foc_observer_t *obs, float initial_lambda) {
    obs->x1 = initial_lambda;
    obs->x2 = 0.0f;
    obs->lambda_est = initial_lambda;
    obs->phase = 0.0f;
    /* The MXLEMMING observers integrate L * (i - i_last); leaving these to the
     * caller's stack makes the first sample depend on garbage. */
    obs->i_alpha_last = 0.0f;
    obs->i_beta_last = 0.0f;
}

void foc_observer_update(foc_observer_t *obs, float v_alpha, float v_beta, float i_alpha,
                         float i_beta, float dt, float r_ohm, float l_henry, float lambda_wb,
                         float gamma, foc_observer_type_t type) {
    float l_ia = l_henry * i_alpha;
    float l_ib = l_henry * i_beta;
    float r_ia = r_ohm * i_alpha;
    float r_ib = r_ohm * i_beta;
    float gamma_half = gamma * 0.5f;

    /*
     * The reference's branch structure, copied per case (motor/foc_math.c:90-199).
     * Every observer keeps a different subset of state and has its own convergence
     * rule, so selecting one over another is not a tuning choice.
     */
    switch (type) {
    case FOC_OBSERVER_ORTEGA_ORIGINAL: {
        float err = SQ(lambda_wb) - (SQ(obs->x1 - l_ia) + SQ(obs->x2 - l_ib));

        /* Forcing this term to stay negative helps convergence (reference comment,
         * see ObserverPermanentMagnet.pdf). */
        if (err > 0.0f) {
            err = 0.0f;
        }

        float x1_dot = v_alpha - r_ia + gamma_half * (obs->x1 - l_ia) * err;
        float x2_dot = v_beta - r_ib + gamma_half * (obs->x2 - l_ib) * err;

        obs->x1 += x1_dot * dt;
        obs->x2 += x2_dot * dt;
        break;
    }

    case FOC_OBSERVER_MXLEMMING:
    case FOC_OBSERVER_MXLEMMING_LAMBDA_COMP: {
        obs->x1 += (v_alpha - r_ia) * dt - l_henry * (i_alpha - obs->i_alpha_last);
        obs->x2 += (v_beta - r_ib) * dt - l_henry * (i_beta - obs->i_beta_last);

        if (type == FOC_OBSERVER_MXLEMMING_LAMBDA_COMP) {
            float err = SQ(obs->lambda_est) - (SQ(obs->x1) + SQ(obs->x2));
            obs->lambda_est += 0.1f * gamma_half * obs->lambda_est * -err * dt;
            obs->lambda_est = obs_truncate(obs->lambda_est, lambda_wb * 0.3f, lambda_wb * 2.5f);

            obs->x1 = obs_truncate_abs(obs->x1, obs->lambda_est);
            obs->x2 = obs_truncate_abs(obs->x2, obs->lambda_est);
        } else {
            obs->x1 = obs_truncate_abs(obs->x1, lambda_wb);
            obs->x2 = obs_truncate_abs(obs->x2, lambda_wb);
        }

        /* Set these to 0 to allow using the same atan2 code as for Ortega. */
        l_ia = 0.0f;
        l_ib = 0.0f;
        break;
    }

    case FOC_OBSERVER_ORTEGA_LAMBDA_COMP: {
        float err = SQ(obs->lambda_est) - (SQ(obs->x1 - l_ia) + SQ(obs->x2 - l_ib));

        obs->lambda_est += 0.2f * gamma_half * obs->lambda_est * -err * dt;
        obs->lambda_est = obs_truncate(obs->lambda_est, lambda_wb * 0.3f, lambda_wb * 2.5f);

        if (err > 0.0f) {
            err = 0.0f;
        }

        float x1_dot = v_alpha - r_ia + gamma_half * (obs->x1 - l_ia) * err;
        float x2_dot = v_beta - r_ib + gamma_half * (obs->x2 - l_ib) * err;

        obs->x1 += x1_dot * dt;
        obs->x2 += x2_dot * dt;
        break;
    }

    case FOC_OBSERVER_MXV:
    case FOC_OBSERVER_MXV_LAMBDA_COMP:
    case FOC_OBSERVER_MXV_LAMBDA_COMP_LIN: {
        obs->x1 += (v_alpha - r_ia) * dt;
        obs->x2 += (v_beta - r_ib) * dt;

        if (type == FOC_OBSERVER_MXV_LAMBDA_COMP_LIN) {
            float mag = sqrtf(SQ(obs->x1 - l_ia) + SQ(obs->x2 - l_ib));
            /* Reference: UTILS_LP_FAST(lambda_est, mag, 0.1 * gamma_half * dt * SQ(lambda_est)) */
            obs->lambda_est +=
                (0.1f * gamma_half * dt * SQ(obs->lambda_est)) * (mag - obs->lambda_est);
            obs->lambda_est = obs_truncate(obs->lambda_est, lambda_wb * 0.3f, lambda_wb * 2.5f);

            if (mag > obs->lambda_est) {
                obs->x1 = (obs->x1 / mag) * obs->lambda_est;
                obs->x2 = (obs->x2 / mag) * obs->lambda_est;
            }
        } else if (type == FOC_OBSERVER_MXV_LAMBDA_COMP) {
            float err = SQ(obs->lambda_est) - (SQ(obs->x1 - l_ia) + SQ(obs->x2 - l_ib));
            obs->lambda_est += 0.2f * gamma_half * obs->lambda_est * -err * dt;
            obs->lambda_est = obs_truncate(obs->lambda_est, lambda_wb * 0.3f, lambda_wb * 2.5f);

            float mag = sqrtf(SQ(obs->x1 - l_ia) + SQ(obs->x2 - l_ib));
            if (mag > obs->lambda_est) {
                obs->x1 = (obs->x1 / mag) * obs->lambda_est;
                obs->x2 = (obs->x2 / mag) * obs->lambda_est;
            }
        } else {
            float mag = sqrtf(SQ(obs->x1 - l_ia) + SQ(obs->x2 - l_ib));
            if (mag > lambda_wb) {
                obs->x1 = (obs->x1 / mag) * lambda_wb;
                obs->x2 = (obs->x2 / mag) * lambda_wb;
            }
        }
        break;
    }

    default:
        break;
    }

    obs->i_alpha_last = i_alpha;
    obs->i_beta_last = i_beta;

    obs->x1 = (obs->x1 != obs->x1) ? 0.0f : obs->x1; /* UTILS_NAN_ZERO */
    obs->x2 = (obs->x2 != obs->x2) ? 0.0f : obs->x2;

    /* Prevent the magnitude from getting too low, as that makes the angle very unstable. */
    float flux_mag = sqrtf(SQ(obs->x1) + SQ(obs->x2));
    if (flux_mag < (lambda_wb * 0.5f)) {
        obs->x1 *= 1.1f;
        obs->x2 *= 1.1f;
    }

    float psi_alpha = obs->x1 - l_ia;
    float psi_beta = obs->x2 - l_ib;

    obs->phase = foc_fast_atan2(psi_beta, psi_alpha);
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

/*
 * Reference util/utils_math.c: utils_step_towards, utils_truncate_number, utils_min_abs and
 * utils_max_abs, plus the SIGN macro from util/utils_math.h:58. All four are small enough
 * that re-deriving them would only introduce doubt, and SIGN's x == 0 case (+1.0, not 0) is
 * load-bearing in the field-weakening backoff below.
 */
float foc_sign(float x) {
    return (x < 0.0f) ? -1.0f : 1.0f;
}

void foc_step_towards(float *value, float goal, float step) {
    if (*value < goal) {
        if ((*value + step) < goal) {
            *value += step;
        } else {
            *value = goal;
        }
    } else if (*value > goal) {
        if ((*value - step) > goal) {
            *value -= step;
        } else {
            *value = goal;
        }
    }
}

void foc_truncate_number(float *number, float min, float max) {
    if (*number > max) {
        *number = max;
    } else if (*number < min) {
        *number = min;
    }
}

void foc_truncate_number_abs(float *number, float max) {
    foc_truncate_number(number, -max, max);
}

float foc_min_abs(float va, float vb) {
    return (fabsf(va) < fabsf(vb)) ? va : vb;
}

float foc_max_abs(float va, float vb) {
    return (fabsf(va) > fabsf(vb)) ? va : vb;
}

void foc_run_fw(foc_fw_state_t *state, const foc_fw_params_t *params, bool mode_allows, float dt) {
    if (params->current_max < fmaxf(params->cc_min_current, 0.001f)) {
        return;
    }

    /*
     * The reference's gate: the running state plus one of the current/brake/speed modes, or
     * having been in field weakening already - the latter so a mode change does not strand a
     * live FW setpoint. The mode half is passed in, because this function does not know the
     * state machine; the setpoint half is checked here, as the reference does.
     */
    if (!mode_allows && state->i_fw_set <= params->cc_min_current) {
        return;
    }

    float fw_current_now = 0.0f;
    if (params->duty_start < 0.99f &&
        state->duty_abs_filtered > params->duty_start * params->l_max_duty) {
        float i_fw_max = params->current_max;

        /*
         * When more field weakening than is achievable is requested, the current controller
         * puts almost all voltage in vd and the iq controller can lose its headroom; the
         * backoff uses the iq error to pull the setpoint back. Comment and arithmetic are the
         * reference's (foc_math.c:735-742).
         */
        if (params->backoff > 0.001f) {
            float i_err_backoff =
                foc_sign(state->speed_erpm) * (state->iq - state->iq_target) / i_fw_max;
            i_err_backoff *= params->backoff;
            foc_truncate_number(&i_err_backoff, 0.0f, 1.0f);
            i_fw_max *= (1.0f - i_err_backoff);
        }

        fw_current_now = FOC_MAP(state->duty_abs_filtered, params->duty_start * params->l_max_duty,
                                 params->l_max_duty, 0.0f, i_fw_max);
    }

    if (params->ramp_time < dt) {
        state->i_fw_set = fw_current_now;
    } else {
        foc_step_towards(&state->i_fw_set, fw_current_now,
                         (dt / params->ramp_time) * params->current_max);
    }
}

void foc_apply_mtpa(uint8_t mtpa_mode, float ld_lq_diff, float lambda, float iq_filter,
                    float *iq_set, float *id_set) {
    if (mtpa_mode == FOC_MTPA_MODE_OFF || ld_lq_diff == 0.0f) {
        return;
    }

    /*
     * Reference mcpwm_foc.c:3633-3639. The 8.0 and 4.0 literals are double on purpose - the
     * reference writes them that way, and 8.0f/4.0f would move the last bits.
     */
    float iq_ref = *iq_set;
    if (mtpa_mode == FOC_MTPA_MODE_IQ_MEASURED) {
        iq_ref = foc_min_abs(*iq_set, iq_filter);
    }

    *id_set = (lambda - sqrtf(SQ(lambda) + 8.0 * SQ(ld_lq_diff * iq_ref))) / (4.0 * ld_lq_diff);
    *iq_set = foc_sign(*iq_set) * sqrtf(SQ(*iq_set) - SQ(*id_set));
}
