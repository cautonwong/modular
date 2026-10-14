#include "ppm/ppm.h"

#include <math.h>
#include <string.h>

/* applications/app_ppm.c:190-215's three laws, defined below and used by the update. */
static float ppm_deadband(float val, float tres, float max);
static float ppm_curve(float val, float curve_acc, float curve_brake, int mode);
static void ppm_step_towards(float *value, float goal, float step);

static edge_status_t ppm_poll(edge_module_t *mod) {
    ppm_app_t *app = (ppm_app_t *)edge_module_data(mod);
    return ppm_update(app, 0.02f);
}

static edge_status_t ppm_on_event(edge_module_t *mod, const edge_event_t *evt) {
    (void)mod;
    (void)evt;
    return EDGE_OK;
}

static edge_status_t ppm_power_off(edge_module_t *mod) {
    ppm_app_t *app = (ppm_app_t *)edge_module_data(mod);
    if (app) {
        app->output_norm = 0.0f;
        app->signal_lost = true;
    }
    return EDGE_OK;
}

void ppm_construct(ppm_app_t *app, uint32_t module_id, uint32_t priority,
                   const ppm_config_t *config, const ppm_receiver_port_t *receiver_port) {
    if (!app) {
        return;
    }

    memset(app, 0, sizeof(*app));
    app->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 20u,
        .poll = ppm_poll,
        .on_event = ppm_on_event,
        .power_off = ppm_power_off,
        .private_data = app,
    };

    if (config) {
        app->config = *config;
    }
    if (app->config.pulse_min_us <= 0.0f) {
        app->config.pulse_min_us = 1000.0f;
    }
    if (app->config.pulse_max_us <= 0.0f) {
        app->config.pulse_max_us = 2000.0f;
    }
    if (app->config.pulse_center_us <= 0.0f) {
        app->config.pulse_center_us = 1500.0f;
    }
    if (app->config.timeout_s <= 0.0f) {
        app->config.timeout_s = 0.2f;
    }

    /* applications/app_ppm.c:266: that mode's flag starts set, so the first thing it does is decide
     * whether the speed has fallen far enough to let go of the brake. */
    app->force_brake = true;

    if (receiver_port) {
        app->receiver_port = *receiver_port;
    }
    app->safe_start_unlocked = !app->config.safe_start;
    app->signal_lost = true;
    app->output_norm = 0.0f;
}

edge_status_t ppm_init(ppm_app_t *app) {
    if (!app || !app->receiver_port.read_pulse_us) {
        return EDGE_EINVAL;
    }
    app->safe_start_unlocked = !app->config.safe_start;
    app->signal_lost = true;
    app->output_norm = 0.0f;
    return EDGE_OK;
}

edge_status_t ppm_update(ppm_app_t *app, float dt) {
    if (!app || dt < 0.0f) {
        return EDGE_EINVAL;
    }

    float pulse_us = 0.0f;
    edge_status_t st = app->receiver_port.read_pulse_us(app->receiver_port.self, &pulse_us);

    bool signal_ok = (st == EDGE_OK);
    if (app->receiver_port.is_signal_present) {
        signal_ok = signal_ok && app->receiver_port.is_signal_present(app->receiver_port.self);
    }

    if (signal_ok && pulse_us >= (app->config.pulse_min_us - 200.0f) &&
        pulse_us <= (app->config.pulse_max_us + 200.0f)) {
        app->last_pulse_us = pulse_us;
        app->time_since_last_pulse_s = 0.0f;
        app->signal_lost = false;
    } else {
        app->time_since_last_pulse_s += dt;
        if (app->time_since_last_pulse_s > app->config.timeout_s) {
            app->signal_lost = true;
            app->output_norm = 0.0f;
            return EDGE_OK;
        }
    }

    if (app->signal_lost) {
        app->output_norm = 0.0f;
        return EDGE_OK;
    }

    float p = app->last_pulse_us;
    float center = app->config.pulse_center_us;
    float raw_out = 0.0f;

    /* applications/app_ppm.c:151-159: two straight lines meeting at the centre pulse, with no band
     * of their own around it. The reference reaches this value the long way - its decoded [-1, 1]
     * one mapped onto microseconds and mapped back - which is the round trip it looks like, so the
     * straight line is what it amounts to. */
    if (p > center) {
        float span = app->config.pulse_max_us - center;
        if (span > 0.0f) {
            raw_out = (p - center) / span;
        }
    } else if (p < center) {
        float span = center - app->config.pulse_min_us;
        if (span > 0.0f) {
            raw_out = (p - center) / span;
        }
    } else {
        raw_out = 0.0f;
    }

    if (raw_out > 1.0f) {
        raw_out = 1.0f;
    }
    if (raw_out < -1.0f) {
        raw_out = -1.0f;
    }

    /* applications/app_ppm.c:135-137: a detached application substitutes the override for what it
     * decoded, and the reference takes that value as given - the clamp above is the reading's, so
     * the substitution is made after it rather than before. */
    if (app->detached) {
        raw_out = app->override_norm;
    }

    /* applications/app_ppm.c:151-159's default branch is what the straight lines above amount to,
     * and what the decoded level reports is this value as it stands here - before the group below
     * re-maps it. That is the distinction the reference draws between input_val and what it
     * commands. */
    app->decoded_norm = raw_out;

    /* applications/app_ppm.c:144-149: the one group of modes whose command is the decoded value
     * re-mapped onto [0, 1] rather than left as it is. */
    switch (app->config.mode) {
    case PPM_MODE_CURRENT_NOREV:
    case PPM_MODE_DUTY_NOREV:
    case PPM_MODE_PID_NOREV:
    case PPM_MODE_PID_POSITION_360:
        raw_out = (raw_out + 1.0f) / 2.0f;
        break;
    default:
        break;
    }

    /* applications/app_ppm.c:190-215, in the reference's own order: the deadband on the decoded
     * value, then the curve, then a ramp whose time depends on which way the value is going. */
    raw_out = ppm_deadband(raw_out, app->config.hyst, 1.0f);
    raw_out = ppm_curve(raw_out, app->config.throttle_exp, app->config.throttle_exp_brake,
                        app->config.throttle_exp_mode);
    {
        const float ramp_time = (fabsf(raw_out) > fabsf(app->output_ramp))
                                    ? app->config.ramp_time_pos
                                    : app->config.ramp_time_neg;
        if (ramp_time > 0.01f) {
            ppm_step_towards(&app->output_ramp, raw_out, dt / ramp_time);
            raw_out = app->output_ramp;
        }
    }

    if (!app->safe_start_unlocked) {
        if (fabsf(raw_out) < 0.05f) {
            app->safe_start_unlocked = true;
        } else {
            app->output_norm = 0.0f;
            return EDGE_OK;
        }
    }

    app->output_norm = raw_out;
    return EDGE_OK;
}

float ppm_get_last_pulse_us(const ppm_app_t *app) {
    return app != (void *)0 ? app->last_pulse_us : 0.0f;
}

float ppm_get_output(const ppm_app_t *app) {
    if (!app || app->signal_lost) {
        return 0.0f;
    }
    return app->output_norm;
}

bool ppm_is_safe(const ppm_app_t *app) {
    if (!app) {
        return false;
    }
    return !app->signal_lost && app->safe_start_unlocked;
}

/*
 * applications/app_ppm.c:190-215's three laws, each the reference's own. The deadband is a rescale
 * that meets the endpoints rather than a band that zeroes, the curve is utils_math.c:437-490's -
 * the same one this port keeps in app/throttle, copied here because one application may not call
 * another
 * - and the ramp is a step of dt/ramp_time, in seconds, toward the target.
 */
static float ppm_deadband(float val, float tres, float max) {
    if (fabsf(val) < tres) {
        return 0.0f;
    }
    const float k = max / (max - tres);
    if (val > 0.0f) {
        return k * val + max * (1.0f - k);
    }
    return -(k * -val + max * (1.0f - k));
}

static float ppm_curve(float val, float curve_acc, float curve_brake, int mode) {
    float v = val;
    if (v < -1.0f) {
        v = -1.0f;
    }
    if (v > 1.0f) {
        v = 1.0f;
    }

    const float val_a = fabsf(v);
    const float curve = (v >= 0.0f) ? curve_acc : curve_brake;
    float ret = 0.0f;

    if (mode == 0) { /* Exponential */
        if (curve >= 0.0f) {
            ret = 1.0f - powf(1.0f - val_a, 1.0f + curve);
        } else {
            ret = powf(val_a, 1.0f - curve);
        }
    } else if (mode == 1) { /* Natural */
        if (fabsf(curve) < 1e-10f) {
            ret = val_a;
        } else if (curve >= 0.0f) {
            ret = 1.0f - ((expf(curve * (1.0f - val_a)) - 1.0f) / (expf(curve) - 1.0f));
        } else {
            ret = (expf(-curve * val_a) - 1.0f) / (expf(-curve) - 1.0f);
        }
    } else if (mode == 2) { /* Polynomial */
        if (curve >= 0.0f) {
            ret = 1.0f - ((1.0f - val_a) / (1.0f + curve * val_a));
        } else {
            ret = val_a / (1.0f - curve * (1.0f - val_a));
        }
    } else { /* Linear */
        ret = val_a;
    }

    if (v < 0.0f) {
        ret = -ret;
    }
    return ret;
}

static void ppm_step_towards(float *value, float goal, float step) {
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

/* applications/app_ppm.c:37. */
#define PPM_MIN_PULSES_WITHOUT_POWER 50

static float ppm_map(float x, float in_min, float in_max, float out_min, float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/* utils_math.c's own two whiles, which is all utils_norm_angle is. */
static float ppm_norm_angle(float angle) {
    while (angle < 0.0f) {
        angle += 360.0f;
    }
    while (angle > 360.0f) {
        angle -= 360.0f;
    }
    return angle;
}

/*
 * applications/app_ppm.c:218-440: the decision half of the loop. Each branch is the reference's
 * own, down to the order of its tests - the one mode whose hysteresis divides a brake from a
 * reversal, the idle counter the safe start requires to hold still twice, and the per-mode command.
 * What the reference does with mc_interface_* calls is returned here as a value for the product to
 * apply.
 */
ppm_command_t ppm_policy(ppm_app_t *app, float servo_val, const ppm_policy_in_t *in) {
    ppm_command_t out;
    memset(&out, 0, sizeof(out));
    out.kind = PPM_CMD_NONE;
    if (app == (void *)0 || in == (const ppm_policy_in_t *)0) {
        return out;
    }

    const float direction_hyst = app->config.max_erpm_for_dir * 0.20f; /* :68 */
    const float rpm_now = in->rpm_now;
    const float rpm_local = in->rpm_local;
    float current = 0.0f;
    bool current_mode = false;
    bool current_mode_brake = false;

    switch (app->config.mode) {
    case PPM_MODE_CURRENT_BRAKE_REV_HYST: {
        current_mode = true;

        /* Hysteresis, a fifth of the speed the configuration gives. */
        if (app->force_brake) {
            if (rpm_local < app->config.max_erpm_for_dir - direction_hyst) {
                app->force_brake = false;
                app->did_idle_once = 0;
            }
        } else {
            if (rpm_local > app->config.max_erpm_for_dir + direction_hyst) {
                app->force_brake = true;
                app->did_idle_once = 0;
            }
        }

        if (servo_val >= 0.0f) {
            if (servo_val == 0.0f) {
                /* With an idle in between, going backwards is allowed. */
                if (app->did_idle_once == 1 && !app->force_brake) {
                    app->did_idle_once = 2;
                }
            } else {
                if (rpm_local > -app->config.max_erpm_for_dir) {
                    app->did_idle_once = 0;
                }
            }

            if (rpm_now >= 0.0f) {
                current = servo_val * in->lo_current_max;
            } else {
                current = servo_val * fabsf(in->lo_current_min);
            }
        } else {
            if (app->force_brake) {
                current_mode_brake = true;
            } else {
                if (rpm_local > -app->config.max_erpm_for_dir) {
                    /* The first time it brakes and is not too fast. */
                    if (app->did_idle_once != 2) {
                        app->did_idle_once = 1;
                        current_mode_brake = true;
                    }
                } else {
                    if (app->did_idle_once == 1) {
                        current_mode_brake = true;
                    } else {
                        /* Going backwards is fine now, and braking would be strange. */
                        app->did_idle_once = 2;
                    }
                }
            }

            if (current_mode_brake) {
                current = fabsf(servo_val * in->lo_current_min);
            } else {
                current = servo_val * fabsf(in->lo_current_min);
            }
        }

        if (fabsf(servo_val) < 0.001f) {
            app->pulses_without_power++;
        }
        break;
    }
    case PPM_MODE_CURRENT:
    case PPM_MODE_CURRENT_NOREV:
        current_mode = true;
        if ((servo_val >= 0.0f && rpm_now >= 0.0f) || (servo_val < 0.0f && rpm_now <= 0.0f)) {
            current = servo_val * in->lo_current_max;
        } else {
            current = servo_val * fabsf(in->lo_current_min);
        }
        if (fabsf(servo_val) < 0.001f) {
            app->pulses_without_power++;
        }
        break;

    case PPM_MODE_CURRENT_NOREV_BRAKE:
    case PPM_MODE_CURRENT_SMART_REV:
        current_mode = true;
        current_mode_brake = servo_val < 0.0f;
        if (servo_val >= 0.0f && rpm_now > 0.0f) {
            current = servo_val * in->lo_current_max;
        } else {
            current = fabsf(servo_val * in->lo_current_min);
        }
        if (fabsf(servo_val) < 0.001f) {
            app->pulses_without_power++;
        }
        break;

    case PPM_MODE_DUTY:
    case PPM_MODE_DUTY_NOREV:
    case PPM_MODE_PID:
    case PPM_MODE_PID_NOREV:
        if (fabsf(servo_val) < 0.001f) {
            app->pulses_without_power++;
        }
        break;

    case PPM_MODE_PID_POSITION_180:
    case PPM_MODE_PID_POSITION_360:
        if (fabsf(servo_val) < 0.02f) {
            app->pulses_without_power++;
        }
        break;

    default:
        return out;
    }

    /* The safe start: at startup, after a timeout or a fault it waits until the input has been idle
     * for enough pulses - and, since the count is compared with itself, twice in a row. */
    const bool holds =
        (app->pulses_without_power < PPM_MIN_PULSES_WITHOUT_POWER && app->config.safe_start);
    if (holds) {
        if (app->pulses_without_power == app->pulses_without_power_before) {
            app->pulses_without_power = 0;
        }
        app->pulses_without_power_before = app->pulses_without_power;

        if (app->servo_error) {
            return out;
        }
        if (current_mode) {
            current = 0.0f;
        }
    } else {
        app->servo_error = false;
    }

    switch (app->config.mode) {
    case PPM_MODE_CURRENT:
    case PPM_MODE_CURRENT_NOREV:
    case PPM_MODE_CURRENT_NOREV_BRAKE:
    case PPM_MODE_CURRENT_SMART_REV:
    case PPM_MODE_CURRENT_BRAKE_REV_HYST:
        out.kind = PPM_CMD_CURRENT;
        out.current = current;
        out.current_mode_brake = current_mode_brake;
        break;

    case PPM_MODE_DUTY:
    case PPM_MODE_DUTY_NOREV:
        if (!holds) {
            out.kind = PPM_CMD_DUTY;
            out.duty = ppm_map(servo_val, -1.0f, 1.0f, -in->l_max_duty, in->l_max_duty);
        }
        break;

    case PPM_MODE_PID:
    case PPM_MODE_PID_NOREV:
        if (!holds) {
            out.kind = PPM_CMD_PID_SPEED;
            out.pid_speed_erpm = servo_val * app->config.pid_max_erpm;
        }
        break;

    case PPM_MODE_PID_POSITION_180:
    case PPM_MODE_PID_POSITION_360: {
        float angle = (app->config.mode == PPM_MODE_PID_POSITION_180) ? (servo_val * 180.0f)
                                                                      : (servo_val * 360.0f);
        angle = ppm_norm_angle(angle);
        if (!holds) {
            /* A more intelligent safe start: wait until the commanded angle is close to where the
             * motor already is before taking over in position mode. */
            if (!in->control_mode_is_position) {
                if (fabsf(angle - in->pid_pos_now) < 10.0f) {
                    out.kind = PPM_CMD_PID_POSITION;
                    out.pid_pos_deg = angle;
                }
            } else {
                out.kind = PPM_CMD_PID_POSITION;
                out.pid_pos_deg = angle;
            }
        }
        break;
    }

    default:
        break;
    }

    return out;
}

/*
 * applications/app_ppm.c:89-91: the decoded level, which is not the command for the four modes that
 * re-map it.
 */
float ppm_get_decoded_level(const ppm_app_t *app) {
    return app ? app->decoded_norm : 0.0f;
}

/*
 * applications/app_ppm.c:93-99: the flag, and the value the loop substitutes for a decoded one.
 */
void ppm_detach(ppm_app_t *app, bool detach) {
    if (app == (void *)0) {
        return;
    }
    app->detached = detach;
}

bool ppm_is_detached(const ppm_app_t *app) {
    return app ? app->detached : false;
}

void ppm_override(ppm_app_t *app, float val) {
    if (app == (void *)0) {
        return;
    }
    app->override_norm = val;
}

float ppm_get_override(const ppm_app_t *app) {
    return app ? app->override_norm : 0.0f;
}

edge_module_t *ppm_module(ppm_app_t *app) {
    return app ? &app->module : NULL;
}
