#include "bldc_drive/bldc_commutation.h"

#include <math.h>
#include <string.h>

/*
 * The same constant app/foc_core's own header defines, and for the same reason: this translation
 * unit is not given the toolchain's M_PI, and the value has to stay the double literal the
 * reference uses (mcpwm_foc.c's RAD2DEG_f divides by it, and the reference's own M_PI is glibc's
 * double).
 */
#define M_PI 3.14159265358979323846

void bldc_build_hall_tables(const int8_t hall_to_phase[8], int8_t forward[8], int8_t reverse[8]) {
    /* mcpwm.c:502: the reference's own map, indexed by the step it stored. */
    static const int8_t fwd_to_rev[7] = {-1, 1, 6, 5, 4, 3, 2};

    if (hall_to_phase == (void *)0 || forward == (void *)0 || reverse == (void *)0) {
        return;
    }

    for (int i = 0; i < 8; i++) {
        const int8_t step = hall_to_phase[i];
        reverse[i] = step;

        /*
         * The reference's own two cases are "below one" and everything else, and its map has seven
         * entries because its table only ever holds one to six. A table that holds something else
         * is not one this layer produced, so it is passed through rather than indexed with - a read
         * past the map is the one thing this port does not reproduce.
         */
        if (step < 1 || step > 6) {
            forward[i] = step;
            continue;
        }

        forward[i] = fwd_to_rev[step];
    }
}

int bldc_comm_step_advance(int comm_step, int steps) {
    /* mcpwm.c:2597-2603, its two whiles rather than a modulo, so the arithmetic is the same one. */
    comm_step += steps;
    while (comm_step > 6) {
        comm_step -= 6;
    }
    while (comm_step < 1) {
        comm_step += 6;
    }
    return comm_step;
}

int bldc_tacho_step_delta(int comm_step, int last_step) {
    /* mcpwm.c:2559-2566. */
    const int step = comm_step - 1;
    int tacho_diff = (step - last_step) % 6;

    if (tacho_diff > 3) {
        tacho_diff -= 6;
    } else if (tacho_diff < -2) {
        tacho_diff += 6;
    }

    return tacho_diff;
}

bool bldc_sensorless_now(uint8_t sensor_mode, float rpm, float hall_sl_erpm) {
    /* mcpwm.c:2587-2593, whose speed comparison is against the absolute value of its own rpm. */
    return (sensor_mode == BLDC_SENSOR_MODE_SENSORLESS) ||
           ((sensor_mode == BLDC_SENSOR_MODE_HYBRID) && (fabsf(rpm) > hall_sl_erpm));
}

uint8_t bldc_hall_phase(bool hall1, bool hall2, bool hall3) {
    /* mcpwm.c:2307, mcpwm_read_hall_phase, its own bit order. */
    return (uint8_t)((hall1 ? 1u : 0u) | (hall2 ? 2u : 0u) | (hall3 ? 4u : 0u));
}

void bldc_hall_commutation(int comm_step, int hall_phase, bool running, bool has_commutated,
                           bldc_hall_commutation_t *out) {
    if (out == (void *)0) {
        return;
    }

    /* mcpwm.c:1939-1952, in the reference's own order. */
    out->comm_step = comm_step;
    out->step_changed = false;
    out->apply = false;

    if (comm_step != hall_phase) {
        out->comm_step = hall_phase;
        out->step_changed = true;
        if (running) {
            out->apply = true;
        }
    } else if (running && !has_commutated) {
        out->apply = true;
    }
}

int bldc_hall_phase_from_table(const int8_t forward[8], const int8_t reverse[8], uint8_t reading,
                               int direction) {
    if (forward == (void *)0 || reverse == (void *)0) {
        return 0;
    }

    /*
     * mcpwm.c:2303: one array, indexed with the direction's own offset. This port keeps the two
     * halves as two arrays, so the same lookup is the choice between them - and the reading is
     * taken to its three bits, which the reference's own read_hall() already guarantees.
     */
    const int index = (int)(reading & 7u);
    return (int)((direction != 0) ? reverse[index] : forward[index]);
}

void bldc_hall_detect_reset(bldc_hall_detect_counts_t counts) {
    /* mcpwm.c:2243's memset over the same eight by seven. */
    memset(counts, 0, sizeof(int) * 8u * 7u);
}

void bldc_hall_detect_sample(bldc_hall_detect_counts_t counts, uint8_t reading, int comm_step,
                             bool in_first_half) {
    /* mcpwm.c:1874-1877: the gate is the caller's to make, and the column is the step it is at. */
    if (!in_first_half || comm_step < 1 || comm_step > 6) {
        return;
    }

    counts[reading & 7u][comm_step]++;
}

int bldc_hall_detect_result(bldc_hall_detect_counts_t counts, bool hall_sensor_port,
                            int8_t out[8]) {
    if (!hall_sensor_port || out == (void *)0) {
        return hall_sensor_port ? -1 : -3;
    }

    /* mcpwm.c:2262-2275: each reading's step is the one with the most samples, and only a reading
     * with more than fifteen of them names anything. */
    for (int i = 0; i < 8; i++) {
        int samples = 0;
        int res = -1;

        for (int j = 1; j < 7; j++) {
            if (counts[i][j] > samples) {
                samples = counts[i][j];
                if (samples > BLDC_HALL_DETECT_MIN_SAMPLES) {
                    res = j;
                }
            }
            out[i] = (int8_t)res;
        }
    }

    /* mcpwm.c:2279-2297: a table reads back when exactly two readings name nothing and the other
     * six name six different steps. */
    int invalid_samp_num = 0;
    int nums[7] = {0, 0, 0, 0, 0, 0, 0};
    int tot_nums = 0;

    for (int i = 0; i < 8; i++) {
        if (out[i] == -1) {
            invalid_samp_num++;
        } else if (nums[out[i]] == 0) {
            nums[out[i]] = 1;
            tot_nums++;
        }
    }

    return (invalid_samp_num == 2 && tot_nums == 6) ? 0 : -1;
}

/*
 * util/utils_math.h's utils_map, which nothing else in this app needed until now: the linear
 * interpolation the reference writes its limits in, its own arithmetic rather than a
 * reinterpretation.
 */
static float bldc_map(float x, float in_min, float in_max, float out_min, float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void bldc_rpm_dep_calc(const bldc_rpm_dep_params_t *params, float rpm_abs, float v_in,
                       bldc_rpm_dep_t *out) {
    if (params == (void *)0 || out == (void *)0) {
        return;
    }

    /* mcpwm.c:1323-1341, statement for statement, its clamps included. */
    out->cycle_int_limit = params->sl_cycle_int_limit;
    out->cycle_int_limit_running =
        out->cycle_int_limit +
        v_in * params->sl_bemf_coupling_k /
            ((rpm_abs > params->sl_min_erpm) ? rpm_abs : params->sl_min_erpm);
    out->cycle_int_limit_running =
        bldc_map(rpm_abs, 0.0f, params->sl_cycle_int_rpm_br, out->cycle_int_limit_running,
                 out->cycle_int_limit_running * params->sl_phase_advance_at_br);
    out->cycle_int_limit_max = out->cycle_int_limit + v_in * params->sl_bemf_coupling_k /
                                                          params->sl_min_erpm_cycle_int_limit;

    if (out->cycle_int_limit_running < 1.0f) {
        out->cycle_int_limit_running = 1.0f;
    }

    if (out->cycle_int_limit_running > out->cycle_int_limit_max) {
        out->cycle_int_limit_running = out->cycle_int_limit_max;
    }

    out->comm_time_sum = params->m_bldc_f_sw_max / ((rpm_abs / 60.0f) * 6.0f);
    out->comm_time_sum_min_rpm = params->m_bldc_f_sw_max / ((params->sl_min_erpm / 60.0f) * 6.0f);
}

bool bldc_cycle_integrator_adds(float v_diff, float pwm_cycles_sum, float last_pwm_cycles_sum,
                                bool has_commutated, float ph_now_raw, float duty, float v_in) {
    /* mcpwm.c:1889-1900. */
    if (v_diff <= 0.0f) {
        return false;
    }

    /*
     * The reference truncates this band's limit into an int, and its phase reading is an ADC count;
     * so is this one, which is why the comparison can be made in floats without changing it.
     */
    const int min = (int)((1.0f - fabsf(duty)) * v_in * 0.3f);
    float band = (float)min;
    if (band > (v_in / 4.0f)) {
        band = v_in / 4.0f;
    }

    return (pwm_cycles_sum > (last_pwm_cycles_sum / 2.0f)) || !has_commutated ||
           ((ph_now_raw > band) && (ph_now_raw < (v_in - band)));
}

bool bldc_comm_sensorless_step(const bldc_comm_input_t *in, const bldc_rpm_dep_params_t *params,
                               const bldc_rpm_dep_t *dep, bldc_comm_state_t *state) {
    if (in == (void *)0 || params == (void *)0 || dep == (void *)0 || state == (void *)0) {
        return false;
    }

    /* mcpwm.c:1886-1888: what the gate let in is integrated over the switching period. */
    if (bldc_cycle_integrator_adds(in->v_diff, in->pwm_cycles_sum, in->last_pwm_cycles_sum,
                                   in->has_commutated, in->ph_now_raw, in->duty, in->v_in)) {
        state->cycle_integrator += in->v_diff / in->switching_frequency_now;
    }

    /*
     * mcpwm.c:1902-1913: the INTEGRATE mode commutes once the integral passes the limit the speed
     * leaves - the running one, or the configuration's own before anything has commutated - or the
     * ceiling it is held under. Both resets are the reference's.
     */
    if (in->comm_mode == BLDC_COMM_MODE_INTEGRATE) {
        const float scaled = 0.0005f * in->vdiv_corr;
        const float limit =
            (in->has_commutated ? dep->cycle_int_limit_running : dep->cycle_int_limit) * scaled;

        if ((state->cycle_integrator >= (dep->cycle_int_limit_max * scaled)) ||
            (state->cycle_integrator >= limit)) {
            state->cycle_integrator = 0.0f;
            state->cycle_sum = 0.0f;
            return true;
        }
        return false;
    }

    /*
     * mcpwm.c:1914-1929: the DELAY mode counts switching periods while the measurement stays
     * positive and commutes at its own speed-dependent threshold, folding the integral into the two
     * figures the detection reads back. A measurement that is not positive forces both to zero.
     */
    if (in->comm_mode == BLDC_COMM_MODE_DELAY) {
        if (in->v_diff > 0.0f) {
            state->cycle_sum += params->m_bldc_f_sw_max / in->switching_frequency_now;

            const float threshold =
                bldc_map(in->rpm_abs, 0.0f, params->sl_cycle_int_rpm_br, dep->comm_time_sum / 2.0f,
                         (dep->comm_time_sum / 2.0f) * params->sl_phase_advance_at_br);

            if (state->cycle_sum >= threshold) {
                state->cycle_integrator_sum +=
                    state->cycle_integrator * (1.0f / (0.0005f * in->vdiv_corr));
                state->cycle_integrator_iterations += 1.0f;
                state->cycle_integrator = 0.0f;
                state->cycle_sum = 0.0f;
                return true;
            }
        } else {
            state->cycle_integrator = 0.0f;
            state->cycle_sum = 0.0f;
        }
        return false;
    }

    return false;
}

/*
 * util/utils_math.h's utils_norm_angle: the reference's own two whiles rather than a modulo, which
 * is the faster form it says it is.
 */
static void bldc_norm_angle_deg(float *angle) {
    while (*angle < 0.0f) {
        *angle += 360.0f;
    }
    while (*angle >= 360.0f) {
        *angle -= 360.0f;
    }
}

int bldc_hall_angle_table(const float sin_hall[8], const float cos_hall[8],
                          const int hall_iterations[8], uint8_t table[8], bool *result) {
    if (sin_hall == (void *)0 || cos_hall == (void *)0 || hall_iterations == (void *)0 ||
        table == (void *)0 || result == (void *)0) {
        return 0;
    }

    /*
     * mcpwm_foc.c:2464-2474, statement for statement. The reference's RAD2DEG_f is the arctangent
     * times the double quotient of a hundred and eighty over pi, cast to a float, and its scaling
     * to the table's two hundred counts keeps its own two double literals.
     */
    int fails = 0;

    for (int i = 0; i < 8; i++) {
        if (hall_iterations[i] > 30) {
            float ang = atan2f(sin_hall[i], cos_hall[i]) * (float)(180.0 / M_PI);
            bldc_norm_angle_deg(&ang);
            table[i] = (uint8_t)(ang * 200.0 / 360.0);
        } else {
            table[i] = 255u;
            fails++;
        }
    }

    *result = (fails == 2);
    return fails;
}
