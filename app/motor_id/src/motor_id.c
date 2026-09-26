#include "motor_id/motor_id.h"

#include <math.h>
#include <string.h>

/* The procedures advance in whole milliseconds and dt arrives in seconds, the repo's unit. */
#define MOTOR_ID_MS_PER_SECOND 1000.0f
/* Reference mcpwm_foc.c:1838, the wait for the current to rise and the motor to lock. */
#define MOTOR_ID_SETTLE_MS 50u
/* Reference mcpwm_foc.c:1849, the cap on the sample wait. */
#define MOTOR_ID_SAMPLE_TIMEOUT_MS 10000u

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
