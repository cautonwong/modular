#include "motor_id/motor_id.h"

#include <math.h>
#include <string.h>

/* The procedures advance in whole milliseconds and dt arrives in seconds, the repo's unit. */
#define MOTOR_ID_MS_PER_SECOND 1000.0f
/* Reference mcpwm_foc.c:1838, the wait for the current to rise and the motor to lock. */
#define MOTOR_ID_SETTLE_MS 50u
/* Reference mcpwm_foc.c:1849, the cap on the sample wait. */
#define MOTOR_ID_SAMPLE_TIMEOUT_MS 10000u
/* Reference util/utils_math.h:75 RPM2RADPS_f, whose M_PI is glibc's double. This file cannot
 * include foc_math.h to borrow its definition - an app does not include another app - so the same
 * expression is written here, with the same literals. */
#define MOTOR_ID_RPM_TO_RAD_S (2.0 * 3.14159265358979323846 / 60.0)

/* Defined below with the flux-linkage procedures; the tick switch dispatches to them. */
static void motor_id_flux_tick(motor_id_app_t *app);
static void motor_id_sensored_tick(motor_id_app_t *app);

static edge_status_t motor_id_poll(edge_module_t *mod) {
    (void)mod;
    return EDGE_OK;
}

static edge_status_t motor_id_on_event(edge_module_t *mod, const edge_event_t *evt) {
    (void)mod;
    (void)evt;
    return EDGE_OK;
}

/*
 * The cleanup block the reference runs when a measurement ends (:1874-1881, and the same block
 * inside both fault paths): setpoints zeroed, the phase override cleared, the PWM stopped.
 */
static void motor_id_stop_motor(motor_id_app_t *app) {
    (void)app->measure_port.set_current(app->measure_port.self, 0.0f);
    (void)app->measure_port.set_phase_override(app->measure_port.self, 0.0f, false);
    (void)app->measure_port.stop(app->measure_port.self);
    app->ramp_current_a = 0.0f;
}

static void motor_id_fail(motor_id_app_t *app, uint32_t fault_code) {
    app->fault_code = fault_code;
    app->result.valid = false;
    motor_id_stop_motor(app);
    app->state = MOTOR_ID_STATE_FAILED;
}

static edge_status_t motor_id_power_off(edge_module_t *mod) {
    motor_id_app_t *app = (motor_id_app_t *)edge_module_data(mod);
    if (app != (void *)0 && app->measure_port.stop != (void *)0) {
        motor_id_stop_motor(app);
        app->state = MOTOR_ID_STATE_IDLE;
    }
    return EDGE_OK;
}

/*
 * Reference util/utils_math.h:129 utils_step_towards, transcribed rather than approximated: when
 * the step would pass the goal the value is snapped to it, and the comparisons are the reference's
 * own. Getting this wrong would make the ramp slightly longer or shorter than the reference's.
 */
static void motor_id_step_towards(float *value, float goal, float step) {
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

/*
 * Reference :1869-1871: the two averages, then their ratio. Dividing the sums directly would be
 * algebraically equal and not bit-identical, so the order is kept.
 *
 * The reference publishes whatever the accumulator holds even when its sample wait timed out
 * before the first sample, which divides by zero; that number is still computed here, but a result
 * with no samples behind it is marked invalid so the caller cannot mistake it for a measurement.
 */
static void motor_id_publish_resistance(motor_id_app_t *app) {
    float i_sum = 0.0f;
    float v_sum = 0.0f;
    uint32_t count = 0u;
    (void)app->measure_port.read_samples(app->measure_port.self, &i_sum, &v_sum, &count);

    const float current_avg = i_sum / (float)count;
    const float voltage_avg = v_sum / (float)count;
    app->result.r_ohm = voltage_avg / current_avg;
    app->result.valid = (count > 0u);

    if (app->stop_after) {
        motor_id_stop_motor(app);
    }
    app->state = MOTOR_ID_STATE_COMPLETE;
}

/* One millisecond of the reference's measure_resistance, whose blocking loop this projects. */
static void motor_id_tick(motor_id_app_t *app) {
    switch (app->state) {
    case MOTOR_ID_STATE_FLUX_CONFIG:
    case MOTOR_ID_STATE_FLUX_RAMP:
    case MOTOR_ID_STATE_FLUX_BASELINE:
    case MOTOR_ID_STATE_FLUX_SPINUP:
    case MOTOR_ID_STATE_FLUX_SETTLE:
    case MOTOR_ID_STATE_FLUX_UNDRIVEN:
    case MOTOR_ID_STATE_FLUX_OBSERVER:
    case MOTOR_ID_STATE_FLUX_STOP:
    case MOTOR_ID_STATE_FLUX_BACK_EMF:
        motor_id_flux_tick(app);
        return;

    case MOTOR_ID_STATE_SENSORED_CONFIG:
    case MOTOR_ID_STATE_SENSORED_RELEASE:
    case MOTOR_ID_STATE_SENSORED_SPINUP:
    case MOTOR_ID_STATE_SENSORED_SAMPLE:
        motor_id_sensored_tick(app);
        return;

    case MOTOR_ID_STATE_RAMP:
        /*
         * :1818-1834. The reference's while loop steps the setpoint, checks the fault and sleeps
         * one millisecond, then re-tests its condition - so the millisecond that completes the
         * ramp is its last one and the settle that follows starts on the next, with no millisecond
         * spent on noticing. A target that is already at the setpoint therefore costs one
         * millisecond here where the reference would spend none; nothing in the reference's own
         * callers measures twice at the same current without stopping in between.
         */
        if (fabsf(app->ramp_current_a - app->target_current_a) > 0.001) {
            motor_id_step_towards(&app->ramp_current_a, app->target_current_a,
                                  fabsf(app->target_current_a) / 200.0);
            (void)app->measure_port.set_current(app->measure_port.self, app->ramp_current_a);
            const uint32_t fault = app->measure_port.get_fault(app->measure_port.self);
            if (fault != 0u) {
                motor_id_fail(app, fault);
                return;
            }
            /* Still short of the target: this millisecond was a ramp step like the reference's,
             * and the next one continues. When the step above did reach it, the fall-through
             * below moves on without spending another. */
            if (fabsf(app->ramp_current_a - app->target_current_a) > 0.001) {
                return;
            }
        }
        app->ms = 0u;
        app->state = MOTOR_ID_STATE_SETTLE;
        return;

    case MOTOR_ID_STATE_SETTLE:
        /*
         * :1837-1843: one fixed wait, then the accumulator is cleared. Cleared before sampling
         * rather than after, which is why reading and clearing are separate port calls.
         */
        if (++app->ms >= MOTOR_ID_SETTLE_MS) {
            (void)app->measure_port.reset_samples(app->measure_port.self);
            app->ms = 0u;
            app->state = MOTOR_ID_STATE_SAMPLE;
        }
        return;

    case MOTOR_ID_STATE_SAMPLE: {
        /*
         * :1846-1860: the count is tested first, then a millisecond passes, then the timeout and
         * the fault check - the order inside the reference's loop body. The count comes from the
         * control loop, which the caller keeps running between steps.
         */
        uint32_t count = 0u;
        (void)app->measure_port.read_samples(app->measure_port.self, (void *)0, (void *)0, &count);
        if (count >= app->target_samples) {
            motor_id_publish_resistance(app);
            return;
        }
        if (++app->ms > MOTOR_ID_SAMPLE_TIMEOUT_MS) {
            /* The reference breaks out of the wait with whatever it collected, and publishes it. */
            motor_id_publish_resistance(app);
            return;
        }
        const uint32_t fault = app->measure_port.get_fault(app->measure_port.self);
        if (fault != 0u) {
            motor_id_fail(app, fault);
        }
        return;
    }

    default:
        return;
    }
}

static void motor_id_sensored_tick(motor_id_app_t *app);

void motor_id_construct(motor_id_app_t *app, uint32_t module_id, uint32_t priority,
                        const motor_id_measure_port_t *measure_port) {
    if (app == (void *)0) {
        return;
    }

    memset(app, 0, sizeof(*app));
    app->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 20u,
        .poll = motor_id_poll,
        .on_event = motor_id_on_event,
        .power_off = motor_id_power_off,
        .private_data = app,
    };

    if (measure_port != (void *)0) {
        app->measure_port = *measure_port;
    }
    app->state = MOTOR_ID_STATE_IDLE;
}

edge_status_t motor_id_init(motor_id_app_t *app) {
    if (app == (void *)0) {
        return EDGE_EINVAL;
    }

    /*
     * Every callback the procedures use has to be there. A missing one would otherwise show up as
     * a measurement that quietly does nothing.
     */
    if (app->measure_port.set_phase_override == (void *)0 ||
        app->measure_port.set_current == (void *)0 ||
        app->measure_port.reset_samples == (void *)0 ||
        app->measure_port.read_samples == (void *)0 || app->measure_port.get_fault == (void *)0 ||
        app->measure_port.stop == (void *)0) {
        return EDGE_EINVAL;
    }

    app->state = MOTOR_ID_STATE_IDLE;
    return EDGE_OK;
}

edge_status_t motor_id_measure_resistance(motor_id_app_t *app, float current_a, uint32_t samples,
                                          bool stop_after) {
    if (app == (void *)0) {
        return EDGE_EINVAL;
    }
    if (app->measure_port.set_phase_override == (void *)0 ||
        app->measure_port.set_current == (void *)0 ||
        app->measure_port.reset_samples == (void *)0 ||
        app->measure_port.read_samples == (void *)0 || app->measure_port.get_fault == (void *)0 ||
        app->measure_port.stop == (void *)0) {
        return EDGE_EINVAL;
    }
    if (samples == 0u) {
        return EDGE_EINVAL;
    }
    if (app->state == MOTOR_ID_STATE_RAMP || app->state == MOTOR_ID_STATE_SETTLE ||
        app->state == MOTOR_ID_STATE_SAMPLE) {
        return EDGE_EBUSY;
    }

    /*
     * :1803-1811: the phase is held at zero, the control mode becomes CURRENT with id = 0, and the
     * ramp starts from whatever the setpoint was - zero when nothing ran before it.
     */
    (void)app->measure_port.set_phase_override(app->measure_port.self, 0.0f, true);
    (void)app->measure_port.set_current(app->measure_port.self, app->ramp_current_a);

    app->target_current_a = current_a;
    app->target_samples = samples;
    app->stop_after = stop_after;
    app->fault_code = 0u;
    app->result.valid = false;
    app->ms = 0u;
    app->ms_accum = 0.0f;
    app->state = MOTOR_ID_STATE_RAMP;
    return EDGE_OK;
}

/*
 * Flux linkage, conf_general_measure_flux_linkage_openloop (conf_general.c:967-1316). The
 * reference runs it as one blocking mill: a temporary configuration, a current ramp at a
 * standstill, a baseline of what the motor draws without turning, a spin-up watched until the duty
 * reaches its target, an average over ten seconds of driving, and then the same measurement with
 * the phases off. Each of those is a phase here, one millisecond of the procedure per accumulated
 * millisecond, as the resistance measurement is.
 *
 * Two readings of the reference are worth naming rather than hiding. Its 200-step current ramp has
 * no sleep in it (:1057-1071), so it runs within one millisecond here too - faster than a motor
 * would settle, which the measurement tolerates because the ramp is only there to reach the
 * current, not to be a profile. And its resistance and inductance defaults are taken only when the
 * commanded current is below cc_min_current (:987-996), which is the reference's own quirk and is
 * kept.
 */
#define MOTOR_ID_FLUX_TC_US 1500.0f         /* :1000, the current loop's time constant */
#define MOTOR_ID_FLUX_DUTY_SCALE 0.9f       /* :1012 */
#define MOTOR_ID_FLUX_CONFIG_SETTLE_MS 500u /* :1024 */
#define MOTOR_ID_FLUX_RAMP_STEPS 200u       /* :1057 */
#define MOTOR_ID_FLUX_BASELINE_MS 1000u     /* :1076 */
#define MOTOR_ID_FLUX_MAX_TIME_MS 15000u    /* :1096 */
#define MOTOR_ID_FLUX_DUTY_FALL_MS 4000u    /* :1126, :1133 */
#define MOTOR_ID_FLUX_DUTY_FALL_FACTOR 0.7f /* :1126 */
#define MOTOR_ID_FLUX_STILL_FACTOR 1.1f     /* :1133 */
#define MOTOR_ID_FLUX_MAX_RPM 12000.0f      /* :1145 */
#define MOTOR_ID_FLUX_SETTLE_MS 1000u       /* :1147 */
#define MOTOR_ID_FLUX_SAMPLE_MS 10000u      /* :1156, the average's own loop */
#define MOTOR_ID_FLUX_OBSERVER_MS 500u      /* :1186 */
#define MOTOR_ID_FLUX_BRIDGE_MS 5u          /* :1195 */
#define MOTOR_ID_FLUX_UNDRIVEN_MS 2000u     /* :1216 */
#define MOTOR_ID_FLUX_UNDRIVEN_DUTY 0.02f   /* :1218 */

/*
 * The flux procedure's failed exits. They are not motor_id_fail(): the reference writes -1, -2 or
 * -3 into the linkage and leaves its fault code alone (conf_general.c:1125, :1133, :1140), so
 * there is no fault to report - but the temporary configuration still has to come back and the
 * motor still has to stop, on every path.
 */
static void motor_id_flux_fail(motor_id_app_t *app, float reason) {
    app->flux_fail_reason = reason;
    app->result.valid = false;
    motor_id_stop_motor(app);
    if (app->measure_port.leave_measurement_config != (void *)0) {
        (void)app->measure_port.leave_measurement_config(app->measure_port.self);
    }
    app->state = MOTOR_ID_STATE_FAILED;
}

/* The four the aggregation divides by, in the reference's order (:1162-1165). */
static void motor_id_publish_flux_averages(motor_id_app_t *app) {
    const float vq_avg = app->flux_vq_sum / app->flux_samples;
    const float vd_avg = app->flux_vd_sum / app->flux_samples;
    const float iq_avg = app->flux_iq_sum / app->flux_samples;
    const float id_avg = app->flux_id_sum / app->flux_samples;

    /* :1170-1172. The speed is the ramp's own value, not a measurement, and the magnitudes are the
     * reference's NORM2_f: sqrt(vq^2 + vd^2) and sqrt(iq^2 + id^2). */
    const float rad_s = app->flux_rpm_now * (float)MOTOR_ID_RPM_TO_RAD_S;
    const float v_mag = sqrtf(vq_avg * vq_avg + vd_avg * vd_avg);
    const float i_mag = sqrtf(iq_avg * iq_avg + id_avg * id_avg);
    app->result.flux_linkage_wb =
        (v_mag - app->flux_res_ohm * i_mag) / rad_s - i_mag * app->flux_ind_h;
    app->flux_fail_reason = 0.0f;
}

/* One millisecond of the flux-linkage procedure. */
static void motor_id_flux_tick(motor_id_app_t *app) {
    switch (app->state) {
    case MOTOR_ID_STATE_FLUX_CONFIG:
        /*
         * :1019-1032: the configuration is installed, then the reference waits until the fault
         * clears, up to 500 milliseconds. If it has not, the old configuration goes back and the
         * fault is the result.
         */
        if (app->measure_port.get_fault(app->measure_port.self) == 0u ||
            ++app->ms >= MOTOR_ID_FLUX_CONFIG_SETTLE_MS) {
            if (app->measure_port.get_fault(app->measure_port.self) != 0u) {
                motor_id_fail(app, app->measure_port.get_fault(app->measure_port.self));
                motor_id_flux_fail(app, 0.0f);
                return;
            }
            app->ms = 0u;
            app->state = MOTOR_ID_STATE_FLUX_RAMP;
        }
        return;

    case MOTOR_ID_STATE_FLUX_RAMP: {
        /*
         * :1055-1071: two hundred steps of i * current / 200 with the speed held at zero, each
         * followed by a fault check. The reference has no sleep in this loop, so it happens inside
         * one millisecond here as well.
         */
        for (uint32_t i = 0u; i < MOTOR_ID_FLUX_RAMP_STEPS; ++i) {
            const float step = (float)i * app->flux_current_a / (float)MOTOR_ID_FLUX_RAMP_STEPS;
            (void)app->measure_port.set_openloop_current(app->measure_port.self, step, 0.0f);
            const uint32_t fault = app->measure_port.get_fault(app->measure_port.self);
            if (fault != 0u) {
                motor_id_fail(app, fault);
                motor_id_flux_fail(app, 0.0f);
                return;
            }
        }
        app->ms = 0u;
        app->flux_duty_still = 0.0f;
        app->state = MOTOR_ID_STATE_FLUX_BASELINE;
        return;
    }

    case MOTOR_ID_STATE_FLUX_BASELINE: {
        /*
         * :1075-1093: a thousand milliseconds of |duty| averaged, which is what the motor draws
         * without turning - the reference's way of telling a motor that is being spun from one that
         * is sitting still. The counters keep running while the setpoint is still zero.
         */
        float duty = 0.0f;
        (void)app->measure_port.read_duty(app->measure_port.self, &duty);
        app->flux_duty_still += fabsf(duty);
        if (++app->ms >= MOTOR_ID_FLUX_BASELINE_MS) {
            app->flux_duty_still /= (float)MOTOR_ID_FLUX_BASELINE_MS;
            app->ms = 0u;
            app->flux_cnt_ms = 0u;
            app->flux_duty_max = 0.0f;
            app->flux_rpm_now = 0.0f;
            app->state = MOTOR_ID_STATE_FLUX_SPINUP;
        }
        return;
    }

    case MOTOR_ID_STATE_FLUX_SPINUP: {
        /*
         * :1096-1146. The reference's loop tests its condition before its body, so a motor already
         * at the target leaves with no samples at all - and its aggregation then divides by zero,
         * which this port keeps and marks as not valid rather than inventing a replacement for.
         */
        float duty_first = 0.0f;
        (void)app->measure_port.read_duty(app->measure_port.self, &duty_first);
        if (fabsf(duty_first) >= app->flux_duty_target) {
            app->ms = 0u;
            app->state = MOTOR_ID_STATE_FLUX_SETTLE;
            return;
        }

        ++app->flux_cnt_ms;
        float duty = 0.0f;
        (void)app->measure_port.read_duty(app->measure_port.self, &duty);
        const float duty_now = fabsf(duty);
        if (duty_now > app->flux_duty_max) {
            app->flux_duty_max = duty_now;
        }

        if (app->flux_cnt_ms >= MOTOR_ID_FLUX_MAX_TIME_MS) {
            motor_id_flux_fail(app, -1.0f);
            return;
        }
        if (app->flux_cnt_ms > MOTOR_ID_FLUX_DUTY_FALL_MS &&
            duty_now < app->flux_duty_max * MOTOR_ID_FLUX_DUTY_FALL_FACTOR) {
            motor_id_flux_fail(app, -2.0f);
            return;
        }
        if (app->flux_cnt_ms > MOTOR_ID_FLUX_DUTY_FALL_MS &&
            app->flux_duty_target < app->flux_duty_still * MOTOR_ID_FLUX_STILL_FACTOR) {
            motor_id_flux_fail(app, -3.0f);
            return;
        }
        if (app->flux_rpm_now >= MOTOR_ID_FLUX_MAX_RPM) {
            app->ms = 0u;
            app->state = MOTOR_ID_STATE_FLUX_SETTLE;
            return;
        }

        app->flux_rpm_now += app->flux_erpm_per_sec / 1000.0f;
        /* :1130-1132: the speed ramp is meaningless without the drive that follows it, and the
         * reference's own callers pass the inverted speed when the configuration inverts the
         * direction - which is where this port applies it, in the product's adapter. */
        (void)app->measure_port.set_openloop_current(app->measure_port.self, app->flux_current_a,
                                                     app->flux_rpm_now);
        const uint32_t fault = app->measure_port.get_fault(app->measure_port.self);
        if (fault != 0u) {
            motor_id_fail(app, fault);
            motor_id_flux_fail(app, 0.0f);
            return;
        }
        return;
    }

    case MOTOR_ID_STATE_FLUX_SETTLE:
        /* :1147: one second of driving is left to go by before the average is taken, then the
         * counters for it are cleared. */
        if (++app->ms >= MOTOR_ID_FLUX_SETTLE_MS) {
            app->ms = 0u;
            app->flux_vq_sum = 0.0f;
            app->flux_vd_sum = 0.0f;
            app->flux_iq_sum = 0.0f;
            app->flux_id_sum = 0.0f;
            app->flux_samples = 0.0f;
            app->state = MOTOR_ID_STATE_FLUX_UNDRIVEN;
        }
        return;

    case MOTOR_ID_STATE_FLUX_UNDRIVEN: {
        /*
         * :1156-1168: ten thousand milliseconds of the four quantities, each with the fault checked
         * after it. What is averaged is what the drive was doing, which is why the speed in the
         * formula is the ramp's own value rather than a measurement of anything.
         */
        float v_d = 0.0f;
        float v_q = 0.0f;
        float i_d = 0.0f;
        float i_q = 0.0f;
        (void)app->measure_port.read_vdq(app->measure_port.self, &v_d, &v_q);
        (void)app->measure_port.read_idq(app->measure_port.self, &i_d, &i_q);
        app->flux_vq_sum += v_q;
        app->flux_vd_sum += v_d;
        app->flux_iq_sum += i_q;
        app->flux_id_sum += i_d;
        app->flux_samples += 1.0f;
        const uint32_t fault = app->measure_port.get_fault(app->measure_port.self);
        if (fault != 0u) {
            motor_id_fail(app, fault);
            motor_id_flux_fail(app, 0.0f);
            return;
        }
        if (++app->ms >= MOTOR_ID_FLUX_SAMPLE_MS) {
            motor_id_publish_flux_averages(app);
            app->ms = 0u;
            app->state = MOTOR_ID_STATE_FLUX_OBSERVER;
        }
        return;
    }

    case MOTOR_ID_STATE_FLUX_OBSERVER:
        /* :1186: the observer is given half a second on the configuration the measurement leaves
         * behind. The observer gain the reference writes there is not a configuration field in
         * this port, so nothing is assigned; the pause is kept because the timing either side of it
         * is part of what the reference's own sequence does. */
        if (++app->ms >= MOTOR_ID_FLUX_OBSERVER_MS) {
            app->ms = 0u;
            app->state = MOTOR_ID_STATE_FLUX_STOP;
        }
        return;

    case MOTOR_ID_STATE_FLUX_STOP:
        /* :1191-1195: the phases stop, the setpoints are cleared and the bridges are left five
         * milliseconds; the first tick of this state is the one that stops them. */
        if (app->ms == 0u) {
            motor_id_stop_motor(app);
            app->ms = 1u;
            return;
        }
        if (app->ms > MOTOR_ID_FLUX_BRIDGE_MS) {
            app->ms = 0u;
            app->state = MOTOR_ID_STATE_FLUX_BACK_EMF;
            return;
        }
        ++app->ms;
        return;

    case MOTOR_ID_STATE_FLUX_BACK_EMF: {
        /*
         * :1216-1226: two thousand milliseconds of vq / rad_s, accumulated only where the duty is
         * below a fiftieth - the phases are off, so what is left is the back EMF. The sample count
         * rises with the millisecond rather than with the accumulation, which is what makes
         * undriven_samples a count of the opportunities and not of the samples.
         */
        float duty = 0.0f;
        (void)app->measure_port.read_duty(app->measure_port.self, &duty);
        if (fabsf(duty) < MOTOR_ID_FLUX_UNDRIVEN_DUTY) {
            float rad_s = 0.0f;
            float v_d = 0.0f;
            float v_q = 0.0f;
            (void)app->measure_port.read_speed_rad_s(app->measure_port.self, &rad_s);
            (void)app->measure_port.read_vdq(app->measure_port.self, &v_d, &v_q);
            if (rad_s != 0.0f) {
                app->flux_linkage_sum += v_q / rad_s;
            }
        }
        app->flux_linkage_samples += 1.0f;
        if (++app->ms >= MOTOR_ID_FLUX_UNDRIVEN_MS) {
            app->result.linkage_undriven_wb = app->flux_linkage_sum / app->flux_linkage_samples;
            app->result.undriven_samples = app->flux_linkage_samples;
            app->result.valid = true;
            if (app->stop_after) {
                motor_id_stop_motor(app);
            }
            /* The temporary configuration comes back on the successful path too (:1204). */
            (void)app->measure_port.leave_measurement_config(app->measure_port.self);
            app->state = MOTOR_ID_STATE_COMPLETE;
        }
        return;
    }

    default:
        return;
    }
}

edge_status_t motor_id_measure_flux_linkage_openloop(motor_id_app_t *app, float current_a,
                                                     float duty, float erpm_per_sec, float res_ohm,
                                                     float ind_h, float config_res_ohm,
                                                     float config_ind_h, float config_duty_max) {
    if (app == (void *)0) {
        return EDGE_EINVAL;
    }
    if (app->measure_port.enter_measurement_config == (void *)0 ||
        app->measure_port.leave_measurement_config == (void *)0 ||
        app->measure_port.set_openloop_current == (void *)0 ||
        app->measure_port.read_vdq == (void *)0 || app->measure_port.read_idq == (void *)0 ||
        app->measure_port.read_duty == (void *)0 ||
        app->measure_port.read_speed_rad_s == (void *)0 ||
        app->measure_port.get_fault == (void *)0 || app->measure_port.stop == (void *)0) {
        return EDGE_EINVAL;
    }
    if (app->state != MOTOR_ID_STATE_IDLE && app->state != MOTOR_ID_STATE_COMPLETE &&
        app->state != MOTOR_ID_STATE_FAILED) {
        return EDGE_EBUSY;
    }

    /* :987-996: the defaults for the resistance and inductance are taken only when the commanded
     * current is at or below cc_min_current, which is the reference's own condition. */
    app->result.valid = false;
    if (res_ohm <= 0.0f) {
        res_ohm = config_res_ohm;
    }
    if (ind_h <= 0.0f) {
        ind_h = config_ind_h;
    }
    if (res_ohm <= 0.0f || ind_h <= 0.0f) {
        app->state = MOTOR_ID_STATE_FAILED;
        return EDGE_EINVAL;
    }

    /*
     * :1012: the duty asked for, capped at nine tenths of what the configuration allows. The
     * current gains come from the resistance and inductance the reference's caller passes, with the
     * time constant of :1000: kp = ind * bw and ki = res * bw with bw = 1 / (1500 us).
     */
    float duty_limit = duty;
    if (duty_limit > config_duty_max * MOTOR_ID_FLUX_DUTY_SCALE) {
        duty_limit = config_duty_max * MOTOR_ID_FLUX_DUTY_SCALE;
    }
    const float bw = 1.0f / (MOTOR_ID_FLUX_TC_US * 1.0e-6f);
    app->flux_duty_target = duty_limit;
    app->flux_current_a = current_a;
    app->flux_erpm_per_sec = erpm_per_sec;
    app->flux_res_ohm = res_ohm;
    app->flux_ind_h = ind_h;
    app->flux_duty_still = 0.0f;
    app->flux_duty_max = 0.0f;
    app->flux_rpm_now = 0.0f;
    app->flux_cnt_ms = 0u;
    app->flux_fail_reason = 0.0f;
    app->ms = 0u;
    app->ms_accum = 0.0f;
    app->stop_after = true;

    /* The temporary configuration goes in before the first phase, exactly where the reference
     * installs it (:1019-1032), and comes back on every exit path. */
    if (app->measure_port.enter_measurement_config(app->measure_port.self, ind_h * bw,
                                                   res_ohm * bw) != EDGE_OK) {
        app->state = MOTOR_ID_STATE_FAILED;
        return EDGE_EINVAL;
    }
    app->state = MOTOR_ID_STATE_FLUX_CONFIG;
    return EDGE_OK;
}

/* --- the sensored flux-linkage procedure, conf_general.c:742-899 ------------------------------ */
#define MOTOR_ID_SENSORED_CONFIG_MS 500u   /* :763-769, the wait for the fault to clear */
#define MOTOR_ID_SENSORED_RELEASE_MS 1000u /* the wait_for_motor_release bound, then the sleep */
#define MOTOR_ID_SENSORED_ATTEMPTS 4u      /* :795, the first plus three retries */
#define MOTOR_ID_SENSORED_SWITCH_MS 2000u  /* :843, no switch by now means the attempt failed */
#define MOTOR_ID_SENSORED_TIMEOUT_MS 5000u /* :848 */
#define MOTOR_ID_SENSORED_SAMPLE_MS 2000u  /* :875 */

static void motor_id_sensored_fail(motor_id_app_t *app) {
    app->result.valid = false;
    motor_id_stop_motor(app);
    if (app->measure_port.leave_measurement_config != (void *)0) {
        (void)app->measure_port.leave_measurement_config(app->measure_port.self);
    }
    app->state = MOTOR_ID_STATE_FAILED;
}

/*
 * The start-up limits each attempt loosens (conf_general.c:807-826): the first changes only the
 * cycle integral limit, the second also doubles the minimum electrical speed and drops the limit to
 * twenty, the third quadruples the speed and switches the commutation mode to its delay setting.
 * What the first attempt leaves alone is passed as the caller's own min_erpm, which is the value
 * the command reads out of the configuration.
 */
static void motor_id_sensored_apply_attempt(motor_id_app_t *app) {
    const motor_id_measure_port_t *p = &app->measure_port;
    if (p->set_startup_limits == (void *)0) {
        return;
    }
    if (app->sensored_pass == 1u) {
        (void)p->set_startup_limits(p->self, app->sensored_min_erpm, 250.0f, false);
    } else if (app->sensored_pass == 2u) {
        (void)p->set_startup_limits(p->self, 2.0f * app->sensored_min_erpm, 20.0f, false);
    } else if (app->sensored_pass == 3u) {
        (void)p->set_startup_limits(p->self, 4.0f * app->sensored_min_erpm, 20.0f, true);
    }
}

/* One millisecond of the sensored flux-linkage procedure. */
static void motor_id_sensored_tick(motor_id_app_t *app) {
    const motor_id_measure_port_t *p = &app->measure_port;
    switch (app->state) {
    case MOTOR_ID_STATE_SENSORED_CONFIG:
        /*
         * :763-775: the configuration is installed, then up to five hundred milliseconds are spent
         * waiting for the fault to clear. If it has not, the old configuration goes back and the
         * procedure reports that it could not measure - there is no linkage to publish.
         */
        if (p->get_fault(p->self) == 0u || ++app->ms >= MOTOR_ID_SENSORED_CONFIG_MS) {
            if (p->get_fault(p->self) != 0u) {
                motor_id_sensored_fail(app);
                return;
            }
            app->ms = 0u;
            app->sensored_pass = 0u;
            app->sensored_switch_done = false;
            (void)p->set_current(p->self, app->sensored_current_a);
            motor_id_sensored_apply_attempt(app);
            app->state = MOTOR_ID_STATE_SENSORED_SPINUP;
        }
        return;

    case MOTOR_ID_STATE_SENSORED_RELEASE: {
        /*
         * :796-826: release the motor and wait for it, bounded by the second the reference allows.
         * The staged configuration is installed again there as well, which here is a no-op: it
         * never left. The drive resumes in the spin-up state, which spends the second it sleeps
         * first.
         */
        bool running = false;
        if (p->release_motor != (void *)0) {
            (void)p->release_motor(p->self);
        }
        if (p->is_running != (void *)0) {
            (void)p->is_running(p->self, &running);
        }
        if (running) {
            if (++app->ms >= MOTOR_ID_SENSORED_RELEASE_MS) {
                motor_id_sensored_fail(app);
            }
            return;
        }
        app->ms = 0u;
        app->flux_cnt_ms = 0u;
        app->sensored_switch_done = false;
        (void)p->set_current(p->self, app->sensored_current_a);
        motor_id_sensored_apply_attempt(app);
        app->state = MOTOR_ID_STATE_SENSORED_SPINUP;
        return;
    }

    case MOTOR_ID_STATE_SENSORED_SPINUP: {
        /*
         * :819-856. The reference sleeps a second after releasing the motor before it drives again,
         * then per millisecond: reads the duty, switches the commutation mode once the duty is
         * halfway to the target, and gives up on this attempt if the switch has not happened within
         * two seconds or at five seconds altogether.
         */
        if (app->ms < MOTOR_ID_SENSORED_RELEASE_MS) {
            ++app->ms;
            return;
        }

        float duty = 0.0f;
        (void)p->read_duty(p->self, &duty);
        const float duty_now = fabsf(duty);
        ++app->flux_cnt_ms;

        if (duty_now >= (app->sensored_duty / 2.0f) && !app->sensored_switch_done) {
            if (p->set_startup_limits != (void *)0) {
                (void)p->set_startup_limits(p->self, app->sensored_min_erpm, 20.0f, true);
            }
            app->sensored_switch_done = true;
        }

        if (duty_now >= app->sensored_duty) {
            app->ms = 0u;
            app->sensored_avg_voltage = 0.0f;
            app->sensored_avg_rpm = 0.0f;
            app->sensored_avg_current = 0.0f;
            app->sensored_samples = 0.0f;
            app->state = MOTOR_ID_STATE_SENSORED_SAMPLE;
            return;
        }

        const bool gave_up =
            (!app->sensored_switch_done && app->flux_cnt_ms > MOTOR_ID_SENSORED_SWITCH_MS) ||
            app->flux_cnt_ms >= MOTOR_ID_SENSORED_TIMEOUT_MS;
        if (gave_up) {
            if ((app->sensored_pass + 1u) >= MOTOR_ID_SENSORED_ATTEMPTS) {
                motor_id_sensored_fail(app);
                return;
            }
            ++app->sensored_pass;
            app->ms = 0u;
            app->flux_cnt_ms = 0u;
            app->sensored_switch_done = false;
            app->state = MOTOR_ID_STATE_SENSORED_RELEASE;
            return;
        }

        /* :853-856: the current is re-armed every millisecond, and a fault aborts the procedure. */
        (void)p->set_current(p->self, app->sensored_current_a);
        if (p->get_fault(p->self) != 0u) {
            motor_id_sensored_fail(app);
        }
        return;
    }

    case MOTOR_ID_STATE_SENSORED_SAMPLE: {
        /*
         * :875-894: two thousand milliseconds of the bus voltage times the duty, the mechanical rpm
         * and the total current, averaged; then the measured current's own drop over the winding -
         * twice, for the two phases it flows through - comes off the voltage, and the linkage is
         * that over sqrt(3) times the angular speed. The current is the FOC's own filtered id and
         * iq, which is what the reference's total current is made of too.
         */
        float v_bus = 0.0f;
        float duty = 0.0f;
        float rpm = 0.0f;
        float i_d = 0.0f;
        float i_q = 0.0f;
        (void)p->read_vbus(p->self, &v_bus);
        (void)p->read_duty(p->self, &duty);
        (void)p->read_rpm(p->self, &rpm);
        (void)p->read_idq(p->self, &i_d, &i_q);
        app->sensored_avg_voltage += v_bus * duty;
        app->sensored_avg_rpm += rpm;
        app->sensored_avg_current += sqrtf(i_d * i_d + i_q * i_q);
        app->sensored_samples += 1.0f;
        if (++app->ms < MOTOR_ID_SENSORED_SAMPLE_MS) {
            return;
        }

        app->sensored_avg_voltage /= app->sensored_samples;
        app->sensored_avg_rpm /= app->sensored_samples;
        app->sensored_avg_current /= app->sensored_samples;
        app->sensored_avg_voltage -= app->sensored_avg_current * app->sensored_res_ohm * 2.0f;
        app->result.flux_linkage_wb =
            app->sensored_avg_voltage /
            (sqrtf(3.0f) * app->sensored_avg_rpm * (float)MOTOR_ID_RPM_TO_RAD_S);
        app->result.valid = true;
        motor_id_stop_motor(app);
        if (p->leave_measurement_config != (void *)0) {
            (void)p->leave_measurement_config(p->self);
        }
        app->state = MOTOR_ID_STATE_COMPLETE;
        return;
    }

    default:
        return;
    }
}

edge_status_t motor_id_measure_flux_linkage_sensored(motor_id_app_t *app, float current_a,
                                                     float duty, float min_erpm, float res_ohm,
                                                     float config_res_ohm) {
    if (app == (void *)0) {
        return EDGE_EINVAL;
    }
    const motor_id_measure_port_t *p = &app->measure_port;
    if (p->enter_sensored_measurement_config == (void *)0 || p->read_vbus == (void *)0 ||
        p->read_rpm == (void *)0 || p->release_motor == (void *)0 || p->is_running == (void *)0 ||
        p->set_startup_limits == (void *)0 || p->set_current == (void *)0 ||
        p->read_duty == (void *)0 || p->read_idq == (void *)0 || p->get_fault == (void *)0 ||
        p->stop == (void *)0 || p->leave_measurement_config == (void *)0) {
        return EDGE_EINVAL;
    }
    if (app->state != MOTOR_ID_STATE_IDLE && app->state != MOTOR_ID_STATE_COMPLETE &&
        app->state != MOTOR_ID_STATE_FAILED) {
        return EDGE_EBUSY;
    }

    /* The resistance is the caller's, or the configuration's when none is supplied. */
    if (res_ohm <= 0.0f) {
        res_ohm = config_res_ohm;
    }
    if (res_ohm <= 0.0f) {
        app->state = MOTOR_ID_STATE_FAILED;
        return EDGE_EINVAL;
    }

    app->result.valid = false;
    app->sensored_current_a = current_a;
    app->sensored_duty = duty;
    app->sensored_min_erpm = min_erpm;
    app->sensored_res_ohm = res_ohm;
    app->sensored_pass = 0u;
    app->sensored_switch_done = false;
    app->sensored_avg_voltage = 0.0f;
    app->sensored_avg_rpm = 0.0f;
    app->sensored_avg_current = 0.0f;
    app->sensored_samples = 0.0f;
    app->ms = 0u;
    app->ms_accum = 0.0f;
    app->flux_cnt_ms = 0u;
    app->stop_after = true;

    /* The configuration the procedure measures in goes in before the first phase, and comes back on
     * every exit path. */
    if (p->enter_sensored_measurement_config(p->self) != EDGE_OK) {
        app->state = MOTOR_ID_STATE_FAILED;
        return EDGE_EINVAL;
    }
    app->state = MOTOR_ID_STATE_SENSORED_CONFIG;
    return EDGE_OK;
}

edge_status_t motor_id_measure_r_l(motor_id_app_t *app) {
    if (app == (void *)0) {
        return EDGE_EINVAL;
    }
    return EDGE_ENOTSUP;
}

edge_status_t motor_id_measure_flux_linkage(motor_id_app_t *app) {
    if (app == (void *)0) {
        return EDGE_EINVAL;
    }
    return EDGE_ENOTSUP;
}

edge_status_t motor_id_detect_hall(motor_id_app_t *app) {
    if (app == (void *)0) {
        return EDGE_EINVAL;
    }
    return EDGE_ENOTSUP;
}

edge_status_t motor_id_step(motor_id_app_t *app, float dt) {
    if (app == (void *)0 || dt <= 0.0f) {
        return EDGE_EINVAL;
    }

    /*
     * The procedure runs on milliseconds, so time accumulates and one millisecond of it runs per
     * millisecond accumulated. A phase that finishes hands over to the next inside the same call,
     * as the reference's blocking call does, and every phase is bounded in milliseconds - so the
     * loop ends even for a large dt.
     */
    app->ms_accum += dt * MOTOR_ID_MS_PER_SECOND;
    while (app->ms_accum >= 1.0f) {
        app->ms_accum -= 1.0f;
        motor_id_tick(app);
        if (app->state == MOTOR_ID_STATE_COMPLETE || app->state == MOTOR_ID_STATE_FAILED) {
            break;
        }
    }

    return EDGE_OK;
}

const motor_id_result_t *motor_id_get_result(const motor_id_app_t *app) {
    if (app == (void *)0) {
        return (void *)0;
    }
    return &app->result;
}

uint32_t motor_id_get_fault(const motor_id_app_t *app) {
    return (app != (void *)0) ? app->fault_code : 0u;
}

edge_module_t *motor_id_module(motor_id_app_t *app) {
    return (app != (void *)0) ? &app->module : (void *)0;
}
