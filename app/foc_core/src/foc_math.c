#include "foc_core/foc_math.h"

#include <math.h>
#include <string.h>

/* The 32-point table the DFT bins and the HFI excitation index. It lives above the functions that
 * read it rather than beside the bins, because the excitation reads it too. */
#include "hfi_tables.h"

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

/*
 * Reference util/utils_math.h:169 utils_norm_angle_rad, literally: its wraps use double literals,
 * so they are computed in double and rounded back to float. Float constants shift the result by
 * about 1e-4 over a few thousand steps - measured against the reference - which is why this is a
 * helper both of the trackers below call rather than a loop written out twice.
 */
static void foc_norm_angle_rad(float *angle) {
    while (*angle < -M_PI) {
        *angle += 2.0 * M_PI;
    }
    while (*angle >= M_PI) {
        *angle -= 2.0 * M_PI;
    }
}

void foc_pll_run(foc_pll_t *pll, float phase, float dt, float kp, float ki) {
    pll->phase = (pll->phase != pll->phase) ? 0.0f : pll->phase; /* UTILS_NAN_ZERO */

    float delta_theta = phase - pll->phase;
    foc_norm_angle_rad(&delta_theta);

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
 * Reference timer_update (mcpwm_foc.c:3939-3948): the model behind foc_temp_comp, a linearised
 * copper coefficient of 0.00386 per degree Celsius applied to the motor resistance and to the
 * current loop's integral gain. The two literals stay double, as the reference's do, so the
 * rounding into the returned float is the same one - as float literals they differ in the last
 * bit for some temperatures. The caller owns the reference's -30 degC floor, which is what
 * decides between these compensated parameters and the plain ones.
 */
float foc_temp_comp_factor(float motor_temp_c, float base_temp_c) {
    return 1.0 + 0.00386 * (motor_temp_c - base_temp_c);
}

/*
 * The reference's position loop (motor/foc_math.c:385-508, foc_run_pid_control_pos), line for line.
 * Its two differences from the speed loop above are the pair of D terms - one on the error, one on
 * the measured angle, which damps the rotor's motion without waiting for the setpoint - and the
 * anti-windup at the end, which leaves the proportional term at most one unit and gives the
 * integral what is left of that unit.
 *
 * Angles are radians here as they are in the reference; the setpoint arrives in degrees from the
 * command and the caller converts it, which is what mcpwm_foc_set_pid_pos does with p_pid_ang_div's
 * 180/pi. With no encoder there is no index to find, so the found-branch is the one that runs and
 * current_max_a is the runtime limit the product derived - l_current_max times l_current_max_scale,
 * already multiplied together by the time it gets here.
 */
void foc_run_pid_pos(foc_pos_pid_t *pid, const foc_pos_pid_params_t *params, bool in_pos_mode,
                     bool index_found, float angle_set_rad, float angle_now_rad, float dt,
                     float *iq_set) {
    /* PID is off. Return, and drop what the loop was holding (foc_math.c:395-402). */
    if (!in_pos_mode) {
        pid->i_term = 0.0f;
        pid->prev_error = 0.0f;
        pid->prev_proc = angle_now_rad;
        pid->d_filter = 0.0f;
        pid->d_filter_proc = 0.0f;
        return;
    }

    float error = foc_angle_difference(angle_set_rad, angle_now_rad) * params->error_sign;

    float kp = params->kp;
    float ki = params->ki;
    float kd = params->kd;
    float kd_proc = params->kd_proc;

    /*
     * p_pid_gain_dec_angle: below that error the four gains wind down together, so a small
     * remaining error does not get the full gain and the loop settles instead of hunting. The
     * threshold is the angle divided by p_pid_ang_div (foc_math.c:420-433).
     */
    if (params->gain_dec_angle > 0.1f) {
        float min_error = params->gain_dec_angle / params->ang_div;
        float error_abs = fabsf(error);

        if (error_abs < min_error) {
            float scale = error_abs / min_error;
            kp *= scale;
            ki *= scale;
            kd *= scale;
            kd_proc *= scale;
        }
    }

    float p_term = error * kp;
    pid->i_term += error * (ki * dt);

    /*
     * The D term keeps its own interval, the one between *changes* of the error, because at low
     * speed several control iterations run on the same position and dividing by dt each time would
     * multiply the derivative (foc_math.c:436-449).
     */
    float d_term;
    pid->dt_int += dt;
    if (error == pid->prev_error) {
        d_term = 0.0f;
    } else {
        d_term = (error - pid->prev_error) * (kd / pid->dt_int);
        pid->dt_int = 0.0f;
    }

    /* Filter D */
    pid->d_filter -= params->kd_filter * (pid->d_filter - d_term);
    d_term = pid->d_filter;

    /*
     * Process D: the measured angle's own motion, subtracted from the output. It is the rotor
     * moving, not the error, and the loop damps it directly (foc_math.c:452-464).
     */
    float d_term_proc;
    pid->dt_int_proc += dt;
    if (angle_now_rad == pid->prev_proc) {
        d_term_proc = 0.0f;
    } else {
        d_term_proc = -foc_angle_difference(angle_now_rad, pid->prev_proc) * params->error_sign *
                      (kd_proc / pid->dt_int_proc);
        pid->dt_int_proc = 0.0f;
    }

    /* Filter D process */
    pid->d_filter_proc -= params->kd_filter * (pid->d_filter_proc - d_term_proc);
    d_term_proc = pid->d_filter_proc;

    /* I-term wind-up protection: what the proportional term is using, the integral cannot
     * (foc_math.c:467-470). */
    float p_tmp = obs_truncate_abs(p_term, 1.0f);
    pid->i_term = obs_truncate_abs(pid->i_term, 1.0f - fabsf(p_tmp));

    /* Store previous error */
    pid->prev_error = error;
    pid->prev_proc = angle_now_rad;

    /* Calculate output */
    const float output = obs_truncate(p_term + pid->i_term + d_term + d_term_proc, -1.0f, 1.0f);

    if (index_found) {
        *iq_set = output * params->current_max_a;
    } else {
        /* Rotate at 40 % power until the encoder index is found (foc_math.c:505-506). */
        *iq_set = 0.4f * params->current_max_a;
    }
}

float foc_angle_difference(float angle1, float angle2) {
    float difference = angle1 - angle2;
    while (difference < -(float)M_PI) {
        difference += 2.0f * (float)M_PI;
    }
    while (difference > (float)M_PI) {
        difference -= 2.0f * (float)M_PI;
    }
    return difference;
}

void foc_hfi_update(foc_hfi_state_t *hfi, const foc_hfi_update_in_t *in, foc_observer_t *observer) {
    /*
     * Reference mcpwm_foc.c:4220-4225: above the HFI speed the angle is the observer's, and the
     * tracker is tied to the fast speed estimate, so HFI hands over as the motor speeds up instead
     * of holding an angle the observer has already left behind.
     */
    const float rpm_abs = fabsf(in->speed_est_fast * 60.0f / (2.0f * (float)M_PI));

    if (rpm_abs > in->sl_erpm_hfi) {
        hfi->angle = in->phase_now_observer;
        hfi->double_integrator = -in->speed_est_fast;
    }

    if (!hfi->ready) {
        return;
    }

    const bool est_done = hfi->est_done_cnt >= in->start_samples;

    /*
     * Reference mcpwm_foc.c:4231-4258: the V4/V5 and V2/V3 modes do their update in the interrupt
     * and leave this one empty, which is why the reference's two branches there are comments. This
     * port does not implement their interrupt half - it samples at an instant this port's current
     * contract does not offer - so those modes are refused wholesale, which is behaviourally the
     * same thing: with no excitation filling the buffer, ready never arrives and the first guard
     * above returns anyway.
     */
    if (in->mode == FOC_HFI_MODE_REFUSED && est_done) {
        return;
    }

    if (!(in->amb_mode_six_vector || est_done)) {
        return;
    }

    float real_bin1 = 0.0f, imag_bin1 = 0.0f, real_bin2 = 0.0f, imag_bin2 = 0.0f;
    switch (hfi->samples) {
    case 8:
        foc_fft8_bin1(hfi->buffer, &real_bin1, &imag_bin1);
        foc_fft8_bin2(hfi->buffer, &real_bin2, &imag_bin2);
        break;
    case 16:
        foc_fft16_bin1(hfi->buffer, &real_bin1, &imag_bin1);
        foc_fft16_bin2(hfi->buffer, &real_bin2, &imag_bin2);
        break;
    case 32:
        foc_fft32_bin1(hfi->buffer, &real_bin1, &imag_bin1);
        foc_fft32_bin2(hfi->buffer, &real_bin2, &imag_bin2);
        break;
    default:
        /* Not configured: foc_hfi_configure leaves both at zero for a value it does not know, and
         * the reference's own function pointers would be null there. */
        return;
    }

    const float angle_bin_1 = -foc_fast_atan2(imag_bin1, real_bin1);

    /* The reference's commented-out mag_bin_1 is read by its plotting hooks only, which are not
     * ported, so it is not computed here. */
    float angle_bin_2 = -foc_fast_atan2(imag_bin2, real_bin2) / 2.0;

    /*
     * Assuming this runs much faster than it takes to fill the buffer, the angle lags half a
     * buffer behind in phase; the reference compensates with the PLL speed and the sampling
     * period, which is half the switching period unless the interrupt samples in V0 and V7.
     */
    float dt_sw;
    if (in->control_sample_mode_v0_v7) {
        dt_sw = 1.0 / in->f_zv;
    } else {
        dt_sw = 1.0 / (in->f_zv / 2.0);
    }
    angle_bin_2 += in->pll_speed * ((float)hfi->samples / 2.0) * dt_sw;

    /* The second harmonic cannot tell an angle from its 180-degree twin; the one already held
     * decides. */
    if (fabsf(foc_angle_difference(angle_bin_2 + M_PI, hfi->angle)) <
        fabsf(foc_angle_difference(angle_bin_2, hfi->angle))) {
        angle_bin_2 += M_PI;
    }

    /*
     * While the estimate is warming up the first harmonic - which is based on saturation and only
     * accurate at low current - says whether the tracked angle is on the right side, and how often
     * it disagreed is what settles the twin once the tally is large enough.
     */
    if (hfi->est_done_cnt < in->start_samples) {
        hfi->est_done_cnt++;

        if (fabsf(foc_angle_difference(angle_bin_2, angle_bin_1)) > (M_PI / 2.0)) {
            hfi->flip_cnt++;
        }
    }

    if (hfi->est_done_cnt >= in->start_samples) {
        if (hfi->flip_cnt >= (in->start_samples / 2)) {
            angle_bin_2 += M_PI;
        }
        hfi->flip_cnt = 0;

        if (in->mode == FOC_HFI_MODE_START) {
            float s = 0.0f, c = 0.0f;
            foc_fast_sincos(angle_bin_2, &s, &c);
            observer->x1 = c * in->flux_linkage;
            observer->x2 = s * in->flux_linkage;
        }
    }

    hfi->angle = angle_bin_2;
    foc_norm_angle_rad(&hfi->angle);
}

bool foc_saturate_vector_2d(float *x, float *y, float max) {
    bool retval = false;
    float mag = NORM2_f(*x, *y);
    max = fabsf(max);

    if (mag < 1e-10f) {
        mag = 1e-10f;
    }

    if (mag > max) {
        const float f = max / mag;
        *x *= f;
        *y *= f;
        retval = true;
    }

    return retval;
}

float foc_hfi_voltage(const foc_hfi_state_t *hfi, const foc_hfi_excite_in_t *in) {
    float hfi_voltage;
    if (hfi->est_done_cnt < in->start_samples) {
        hfi_voltage = in->hfi_voltage_start;
    } else {
        hfi_voltage = FOC_MAP(fabsf(in->iq), -0.01f, in->current_max, in->hfi_voltage_run,
                              in->hfi_voltage_max);
    }

    foc_truncate_number_abs(&hfi_voltage, in->v_bus * (1.0f - fabsf(in->duty_now)) * SQRT3_BY_2 *
                                              (2.0f / 3.0f) * 0.95f);
    return hfi_voltage;
}

void foc_hfi_excite_six_vector(foc_hfi_state_t *hfi, const foc_hfi_excite_in_t *in, float *v_alpha,
                               float *v_beta) {
    if (hfi == (void *)0 || in == (void *)0 || v_alpha == (void *)0 || v_beta == (void *)0) {
        return;
    }
    if (hfi == (void *)0 || in == (void *)0 || v_alpha == (void *)0 || v_beta == (void *)0) {
        return;
    }

    /*
     * Nothing configured: the reference sets the sample table at boot and on every configuration
     * change, so its state is never in this shape, and a table with no length would run the index
     * off the end of it. Doing nothing is the only thing that is faithful to the fact that no
     * excitation was asked for.
     */
    if (hfi->samples <= 0 || hfi->table_fact <= 0) {
        return;
    }

    const float hfi_voltage = foc_hfi_voltage(hfi, in);
    const float c = foc_utils_tab_cos_32_1[hfi->ind * hfi->table_fact];
    const float s = foc_utils_tab_sin_32_1[hfi->ind * hfi->table_fact];

    if (hfi->is_samp_n) {
        const float sample_now = c * in->i_alpha + s * in->i_beta;
        const float di = sample_now - hfi->prev_sample;

        hfi->buffer_current[hfi->ind] = di;

        /*
         * The reference gates this on `di > 0.01`, and on this checkout that gate never fires: its
         * own branch order stores the previous sample before driving the negative injection half
         * and samples in the positive one, so every measured step is the response to the negative
         * half - measured in the host closed loop at -2.2 A, with all thirty-two entries left at
         * zero. The magnitudes are what the gate is selecting for, and the buffer holds the inverse
         * inductance as a positive quantity (its own comment says so), so the negative-going step
         * is taken and negated. Same structure, same threshold, same quantity in the buffer; the
         * difference is recorded in docs/adr-conformance.md.
         */
        if (di < -0.01f) {
            /* The inverse of the inductance, not the inductance: the measurement carries a DC
             * offset, and taking the inverse first keeps that offset out of the other bins. */
            hfi->buffer[hfi->ind] = -(in->f_zv * di) / hfi_voltage;
        }

        hfi->ind++;
        if (hfi->ind == hfi->samples) {
            hfi->ind = 0;
            hfi->ready = true;
        }

        *v_alpha += hfi_voltage * c;
        *v_beta += hfi_voltage * s;
    } else {
        hfi->prev_sample = c * in->i_alpha + s * in->i_beta;

        *v_alpha -= hfi_voltage * c;
        *v_beta -= hfi_voltage * s;
    }

    /*
     * Reference mcpwm_foc.c:4972, written there against the modulation vector: the same limit in
     * volts, because the modulation is this vector times 1.5/v_bus.
     */
    (void)foc_saturate_vector_2d(v_alpha, v_beta, SQRT3_BY_2 * 0.95f * (2.0f / 3.0f) * in->v_bus);
    hfi->is_samp_n = !hfi->is_samp_n;
}

void foc_hfi_configure(foc_hfi_state_t *hfi, uint8_t foc_hfi_samples) {
    if (hfi == (void *)0) {
        return;
    }
    memset(hfi, 0, sizeof(*hfi));
    switch (foc_hfi_samples) {
    case 0u: /* HFI_SAMPLES_8 */
        hfi->samples = 8;
        hfi->table_fact = 4;
        break;
    case 1u: /* HFI_SAMPLES_16 */
        hfi->samples = 16;
        hfi->table_fact = 2;
        break;
    case 2u: /* HFI_SAMPLES_32 */
        hfi->samples = 32;
        hfi->table_fact = 1;
        break;
    default:
        /* The reference's switch has no default, so anything else leaves the two at zero. */
        break;
    }
}

void foc_hfi_adjust_angle(float ang_err, float max_err, float gain, float speed_est_fast, float dt,
                          foc_hfi_state_t *state) {
    if (state == (void *)0) {
        return;
    }

    foc_truncate_number_abs(&ang_err, max_err);

    /*
     * The reference writes 4000.0 and 10.0 as double literals, so both gains are the narrowed
     * products; as float literals they differ in the last bit for some gains. The reference's own
     * TODO notes that the ratio between them is a guess it has not revisited - it is carried as it
     * stands rather than tidied, because the tuned behaviour depends on it.
     */
    const float gain_int = 4000.0 * gain;
    const float gain_int2 = 10.0 * gain;

    state->double_integrator += ang_err * gain_int2;
    /* Bounded by the fast speed estimate: this is what stops the tracker winding up at speed. */
    foc_truncate_number_abs(&state->double_integrator, fabsf(speed_est_fast));
    state->angle -= dt * (gain_int * ang_err + state->double_integrator);
    foc_norm_angle_rad(&state->angle);
    state->ready = true;
}

/* The generated excitation tables, included next to their only consumer rather than at the top. */

/*
 * Reference util/utils_math.c:509-597. The DFT bins HFI computes its angle error from - bin 0 the
 * mean, bins 1 and 2 the fundamental and second harmonic - over the 8, 16 or 32 samples the
 * configuration's foc_hfi_samples selects. Copied as they stand rather than replaced with a real
 * FFT: they are what the reference runs and they feed a control loop.
 *
 * The `0.0`, `32.0`, `16.0` and `8.0` literals are the reference's doubles, so each sum and each
 * scaling is computed in double and rounded back into the float output.
 */
void foc_fft32_bin0(const float *real_in, float *real, float *imag) {
    *real = 0.0;
    *imag = 0.0;

    for (int i = 0; i < 32; i++) {
        *real += real_in[i];
    }

    *real /= 32.0;
}

void foc_fft32_bin1(const float *real_in, float *real, float *imag) {
    *real = 0.0;
    *imag = 0.0;
    for (int i = 0; i < 32; i++) {
        *real += real_in[i] * foc_utils_tab_cos_32_1[i];
        *imag -= real_in[i] * foc_utils_tab_sin_32_1[i];
    }
    *real /= 32.0;
    *imag /= 32.0;
}

void foc_fft32_bin2(const float *real_in, float *real, float *imag) {
    *real = 0.0;
    *imag = 0.0;
    for (int i = 0; i < 32; i++) {
        *real += real_in[i] * foc_utils_tab_cos_32_2[i];
        *imag -= real_in[i] * foc_utils_tab_sin_32_2[i];
    }
    *real /= 32.0;
    *imag /= 32.0;
}

void foc_fft16_bin0(const float *real_in, float *real, float *imag) {
    *real = 0.0;
    *imag = 0.0;

    for (int i = 0; i < 16; i++) {
        *real += real_in[i];
    }

    *real /= 16.0;
}

void foc_fft16_bin1(const float *real_in, float *real, float *imag) {
    *real = 0.0;
    *imag = 0.0;
    for (int i = 0; i < 16; i++) {
        *real += real_in[i] * foc_utils_tab_cos_32_1[2 * i];
        *imag -= real_in[i] * foc_utils_tab_sin_32_1[2 * i];
    }
    *real /= 16.0;
    *imag /= 16.0;
}

void foc_fft16_bin2(const float *real_in, float *real, float *imag) {
    *real = 0.0;
    *imag = 0.0;
    for (int i = 0; i < 16; i++) {
        *real += real_in[i] * foc_utils_tab_cos_32_2[2 * i];
        *imag -= real_in[i] * foc_utils_tab_sin_32_2[2 * i];
    }
    *real /= 16.0;
    *imag /= 16.0;
}

void foc_fft8_bin0(const float *real_in, float *real, float *imag) {
    *real = 0.0;
    *imag = 0.0;

    for (int i = 0; i < 8; i++) {
        *real += real_in[i];
    }

    *real /= 8.0;
}

void foc_fft8_bin1(const float *real_in, float *real, float *imag) {
    *real = 0.0;
    *imag = 0.0;
    for (int i = 0; i < 8; i++) {
        *real += real_in[i] * foc_utils_tab_cos_32_1[4 * i];
        *imag -= real_in[i] * foc_utils_tab_sin_32_1[4 * i];
    }
    *real /= 8.0;
    *imag /= 8.0;
}

void foc_fft8_bin2(const float *real_in, float *real, float *imag) {
    *real = 0.0;
    *imag = 0.0;
    for (int i = 0; i < 8; i++) {
        *real += real_in[i] * foc_utils_tab_cos_32_2[4 * i];
        *imag -= real_in[i] * foc_utils_tab_sin_32_2[4 * i];
    }
    *real /= 8.0;
    *imag /= 8.0;
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
    vm->ld_lq_diff = 0.0f;
    vm->ld = l_henry;
    vm->lq = l_henry;
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

void foc_virtual_motor_set_saliency(foc_virtual_motor_t *vm, float ld_lq_diff) {
    vm->ld_lq_diff = ld_lq_diff;
    vm->lq = vm->l_henry + ld_lq_diff / 2.0f;
    vm->ld = vm->l_henry - ld_lq_diff / 2.0f;
}

void foc_virtual_motor_step(foc_virtual_motor_t *vm, float v_alpha, float v_beta, float unused,
                            float dt, float load_torque) {
    (void)unused;
    float sin_phi = sinf(vm->rotor_angle_rad);
    float cos_phi = cosf(vm->rotor_angle_rad);

    /* 1. Park transform on voltages */
    float vd = cos_phi * v_alpha + sin_phi * v_beta;
    float vq = cos_phi * v_beta - sin_phi * v_alpha;

    /* 2. Electrical differential equations. The cross-coupling uses the other axis's inductance
     * (reference motor/virtual_motor.c:310-326), which is the whole difference a salient machine
     * makes to the currents; with ld == lq they are the same expression as before. */
    float we = vm->rotor_speed_rad_s * (float)vm->pole_pairs;

    float did_dt = (vd + we * vm->lq * vm->iq - vm->r_ohm * vm->id) / vm->ld;
    float diq_dt = (vq - we * (vm->ld * vm->id + vm->lambda_wb) - vm->r_ohm * vm->iq) / vm->lq;

    vm->id += did_dt * dt;
    vm->iq += diq_dt * dt;

    /* 3. Electromagnetic torque. Reference motor/virtual_motor.c:334-337: the flux term and the
     * reluctance term (ld - lq) * id, which is zero on a machine with no saliency. */
    float t_elec =
        1.5f * (float)vm->pole_pairs * (vm->lambda_wb + (vm->ld - vm->lq) * vm->id) * vm->iq;

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

/*
 * Reference util/utils_math.c utils_batt_liion_norm_v_to_capacity: a five-term polynomial fit
 * of a lithium-ion cell's state of charge against its normalised voltage. The reference declares
 * the coefficients as a const float array, so they are float here too - writing them as doubles
 * would change the last bits.
 */
float foc_batt_liion_norm_v_to_capacity(float norm_v) {
    static const float li_p[5] = {-2.979767f, 5.487810f, -3.501286f, 1.675683f, 0.317147f};

    foc_truncate_number(&norm_v, 0.0f, 1.0f);
    const float v2 = norm_v * norm_v;
    const float v3 = v2 * norm_v;
    const float v4 = v3 * norm_v;
    const float v5 = v4 * norm_v;
    return li_p[0] * v5 + li_p[1] * v4 + li_p[2] * v3 + li_p[3] * v2 + li_p[4] * norm_v;
}

/*
 * Reference mc_interface_get_battery_level() (motor/mc_interface.c): per chemistry, an average
 * pack voltage and a "voltage left" average, then ampere-hours left from how far the cell
 * voltage has fallen across the chemistry's range. Returns ampere-hours-left over
 * ampere-hours-total, which is the same ratio the reference returns - including its 0/0 when
 * the chemistry is unknown, which is not a slip to paper over.
 */
float foc_battery_level(uint8_t battery_type, int cells, float battery_ah, float v_in,
                        float *wh_left) {
    float battery_avg_voltage = 0.0f;
    float battery_avg_voltage_left = 0.0f;
    float ah_left = 0.0f;
    float ah_tot = battery_ah;

    switch (battery_type) {
    case FOC_BATTERY_TYPE_LIION_3_0__4_2: {
        const float cells_f = (float)cells;
        battery_avg_voltage = ((3.2f + 4.2f) / 2.0f) * cells_f;
        battery_avg_voltage_left = ((3.2f * cells_f + v_in) / 2.0f);
        float batt_left = FOC_MAP(v_in / cells_f, 3.2f, 4.2f, 0.0f, 1.0f);
        batt_left = foc_batt_liion_norm_v_to_capacity(batt_left);
        /* 0.85 because the pack is not fully depleted at 3.2 V per cell. */
        ah_tot *= 0.85f;
        ah_left = batt_left * ah_tot;
        break;
    }
    case FOC_BATTERY_TYPE_LIIRON_2_6__3_6: {
        const float cells_f = (float)cells;
        battery_avg_voltage = ((2.8f + 3.6f) / 2.0f) * cells_f;
        battery_avg_voltage_left = ((2.8f * cells_f + v_in) / 2.0f);
        ah_left = FOC_MAP(v_in / cells_f, 2.6f, 3.6f, 0.0f, battery_ah);
        break;
    }
    case FOC_BATTERY_TYPE_LEAD_ACID: {
        const float cells_f = (float)cells;
        /* The reference's own comment: this does not really work for lead-acid, and it is kept
         * as it is rather than improved. */
        battery_avg_voltage = ((2.1f + 2.36f) / 2.0f) * cells_f;
        battery_avg_voltage_left = ((2.1f * cells_f + v_in) / 2.0f);
        ah_left = FOC_MAP(v_in / cells_f, 2.1f, 2.36f, 0.0f, battery_ah);
        break;
    }
    default:
        break;
    }

    const float wh_batt_tot = ah_tot * battery_avg_voltage;
    const float wh_batt_left = ah_left * battery_avg_voltage_left;

    if (wh_left != (void *)0) {
        *wh_left = wh_batt_left;
    }

    return wh_batt_left / wh_batt_tot;
}
