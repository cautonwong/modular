#include "nunchuk/nunchuk.h"

#include <math.h>
#include <string.h>

/* applications/app_nunchuk.c:36-40's constants. */
#define NUNCHUK_OUTPUT_ITERATION_TIME_MS 5.0f
#define NUNCHUK_LOCAL_TIMEOUT_MS 2000u

static edge_status_t nunchuk_poll(edge_module_t *mod) {
    nunchuk_app_t *app = (nunchuk_app_t *)edge_module_data(mod);
    (void)app;
    return EDGE_OK;
}

static edge_status_t nunchuk_on_event(edge_module_t *mod, const edge_event_t *evt) {
    (void)mod;
    (void)evt;
    return EDGE_OK;
}

static edge_status_t nunchuk_power_off(edge_module_t *mod) {
    nunchuk_app_t *app = (nunchuk_app_t *)edge_module_data(mod);
    if (app) {
        app->active = false;
    }
    return EDGE_OK;
}

void nunchuk_construct(nunchuk_app_t *app, uint32_t module_id, uint32_t priority,
                       const nunchuk_config_t *config, const nunchuk_port_t *port) {
    if (!app) {
        return;
    }
    memset(app, 0, sizeof(*app));
    app->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 5u,
        .poll = nunchuk_poll,
        .on_event = nunchuk_on_event,
        .power_off = nunchuk_power_off,
        .private_data = app,
    };
    if (config) {
        app->config = *config;
    }
    if (port) {
        app->port = *port;
    }
    /* app_nunchuk.c:50-54: the controller's own centre at start, and its reset state. */
    app->data.js_y = 128u;
    app->data.js_x = 128u;
    app->error = 0;
}

edge_status_t nunchuk_init(nunchuk_app_t *app) {
    if (!app || !app->port.now_ms) {
        return EDGE_EINVAL;
    }
    app->active = true;
    return EDGE_OK;
}

float nunchuk_get_decoded_x(const nunchuk_app_t *app) {
    return app ? (((float)app->data.js_x - 128.0f) / 128.0f) : 0.0f;
}

float nunchuk_get_decoded_y(const nunchuk_app_t *app) {
    return app ? (((float)app->data.js_y - 128.0f) / 128.0f) : 0.0f;
}

bool nunchuk_get_bt_c(const nunchuk_app_t *app) {
    return app ? app->data.bt_c : false;
}

bool nunchuk_get_bt_z(const nunchuk_app_t *app) {
    return app ? app->data.bt_z : false;
}

bool nunchuk_get_is_rev(const nunchuk_app_t *app) {
    return app ? app->data.is_rev : false;
}

bool nunchuk_has_update(const nunchuk_app_t *app) {
    return app ? app->have_update : false;
}

uint32_t nunchuk_get_update_age_ms(const nunchuk_app_t *app) {
    if (app == (nunchuk_app_t *)0 || !app->have_update || !app->port.now_ms) {
        return 0u;
    }
    uint32_t now = 0u;
    if (app->port.now_ms(app->port.self, &now) != EDGE_OK) {
        return 0u;
    }
    return now - app->last_update_ms;
}

bool nunchuk_decode_frame(nunchuk_app_t *app, const uint8_t frame[6], nunchuk_data_t *out) {
    if (app == (nunchuk_app_t *)0 || frame == (const uint8_t *)0) {
        return false;
    }

    /* app_nunchuk.c:243-250: an unchanged frame is not reported again, and it is what clears the
     * error - the reference only does that from this path. */
    if (app->frame_seen && memcmp(app->last_frame, frame, 6u) == 0) {
        return false;
    }
    memcpy(app->last_frame, frame, 6u);
    app->frame_seen = true;

    nunchuk_data_t d;
    memset(&d, 0, sizeof(d));
    d.js_x = frame[0];
    d.js_y = frame[1];
    d.acc_x = (frame[2] << 2) | ((frame[5] >> 2) & 3u);
    d.acc_y = (frame[3] << 2) | ((frame[5] >> 4) & 3u);
    d.acc_z = (frame[4] << 2) | ((frame[5] >> 6) & 3u);
    /* app_nunchuk.c:255-256: both buttons are active low. */
    d.bt_z = ((frame[5] >> 0) & 1u) == 0u;
    d.bt_c = ((frame[5] >> 1) & 1u) == 0u;
    d.rev_has_state = false;
    d.is_rev = false;

    app->error = 0;
    if (out) {
        *out = d;
    }
    return true;
}

void nunchuk_update_data(nunchuk_app_t *app, const nunchuk_data_t *data) {
    if (app == (nunchuk_app_t *)0 || data == (const nunchuk_data_t *)0) {
        return;
    }
    /* app_nunchuk.c:117-121: the frame arrives, so the output side stops being stale. */
    app->data = *data;
    if (app->port.now_ms) {
        uint32_t now = 0u;
        if (app->port.now_ms(app->port.self, &now) == EDGE_OK) {
            app->last_update_ms = now;
        }
    }
    app->have_update = true;
}

int nunchuk_get_error(const nunchuk_app_t *app) {
    return app ? app->error : 0;
}

void nunchuk_set_error(nunchuk_app_t *app, int error) {
    if (app) {
        app->error = error;
    }
}

bool nunchuk_is_active(const nunchuk_app_t *app) {
    return app ? app->active : false;
}

edge_module_t *nunchuk_module(nunchuk_app_t *app) {
    return app ? &app->module : NULL;
}

static float nunchuk_deadband(float val, float tres, float max) {
    if (fabsf(val) < tres) {
        return 0.0f;
    }
    const float k = max / (max - tres);
    if (val > 0.0f) {
        return k * val + max * (1.0f - k);
    }
    return -(k * -val + max * (1.0f - k));
}

static float nunchuk_curve(float val, float curve_acc, float curve_brake, int mode) {
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
    return (v < 0.0f) ? -ret : ret;
}

static void nunchuk_step_towards(float *value, float goal, float step) {
    if (*value < goal) {
        *value = ((*value + step) < goal) ? (*value + step) : goal;
    } else if (*value > goal) {
        *value = ((*value - step) > goal) ? (*value - step) : goal;
    }
}

static float nunchuk_map(float x, float in_min, float in_max, float out_min, float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/*
 * applications/app_nunchuk.c:218-527, the output thread's decision half. Every early continue of
 * the reference is a pass that commands nothing, and the product applies whatever this returns.
 */
nunchuk_command_t nunchuk_policy(nunchuk_app_t *app, const nunchuk_policy_in_t *in, float dt) {
    nunchuk_command_t out;
    memset(&out, 0, sizeof(out));
    out.kind = NUNCHUK_CMD_NONE;
    if (app == (nunchuk_app_t *)0 || in == (const nunchuk_policy_in_t *)0) {
        return out;
    }

    /* The gates: a timeout, a controller error, no mode, a stale frame, or a disabled output. */
    if (app->error != 0 || app->config.ctrl_type == NUNCHUK_MODE_NONE) {
        app->was_pid = false;
        return out;
    }
    if (nunchuk_get_update_age_ms(app) > NUNCHUK_LOCAL_TIMEOUT_MS) {
        app->was_pid = false;
        return out;
    }

    float rpm_local = fabsf(in->rpm_now);
    float rpm_lowest = rpm_local;
    float current_highest = in->current_now;
    float duty_highest_abs = fabsf(in->duty_now);
    if (app->config.multi_esc && app->port.read_peer_aggregate) {
        float peer_rpm = 0.0f;
        float peer_current = 0.0f;
        float peer_duty = 0.0f;
        if (app->port.read_peer_aggregate(app->port.self, &peer_rpm, &peer_current, &peer_duty) ==
            EDGE_OK) {
            rpm_lowest = peer_rpm;
            current_highest = peer_current;
            duty_highest_abs = peer_duty;
        }
    }

    const float max_current_diff = in->l_current_max * in->l_current_max_scale * 0.2f;

    /* Both buttons at once is the reference's own off switch. */
    if (app->data.bt_c && app->data.bt_z) {
        app->was_pid = false;
        return out;
    }

    /* The reverse latch: it only moves while the current is small, and a frame can carry its own
     * direction state instead of the button. */
    if (fabsf(in->current_now) < max_current_diff) {
        if (app->data.rev_has_state) {
            app->is_reverse = app->data.is_rev;
        } else if (app->data.bt_z && !app->was_z) {
            app->is_reverse = !app->is_reverse;
        }
    }
    if (app->config.ctrl_type == NUNCHUK_MODE_CURRENT_NOREV ||
        app->config.ctrl_type == NUNCHUK_MODE_CURRENT_BIDIRECTIONAL) {
        app->is_reverse = false;
    }
    app->was_z = app->data.bt_z;

    float out_val = nunchuk_get_decoded_y(app);
    out_val = nunchuk_deadband(out_val, app->config.hyst, 1.0f);
    out_val = nunchuk_curve(out_val, app->config.throttle_exp, app->config.throttle_exp_brake,
                            (int)app->config.throttle_exp_mode);

    if (app->data.bt_c) {
        /* The stick ramps a speed target, and only when the motor is not already turning hard the
         * other way. */
        if (!app->was_pid) {
            app->pid_rpm = in->rpm_now;
            if ((app->is_reverse && app->pid_rpm > 0.0f) ||
                (!app->is_reverse && app->pid_rpm < 0.0f)) {
                return out;
            }
            app->was_pid = true;
        } else if (app->is_reverse) {
            if (app->pid_rpm > 0.0f) {
                app->pid_rpm = 0.0f;
            }
            app->pid_rpm -= (out_val * app->config.stick_erpm_per_s_in_cc) * dt;
            if (app->pid_rpm < (in->rpm_now - app->config.stick_erpm_per_s_in_cc)) {
                app->pid_rpm = in->rpm_now - app->config.stick_erpm_per_s_in_cc;
            }
        } else {
            if (app->pid_rpm < 0.0f) {
                app->pid_rpm = 0.0f;
            }
            app->pid_rpm += (out_val * app->config.stick_erpm_per_s_in_cc) * dt;
            if (app->pid_rpm > (in->rpm_now + app->config.stick_erpm_per_s_in_cc)) {
                app->pid_rpm = in->rpm_now + app->config.stick_erpm_per_s_in_cc;
            }
        }

        app->prev_current = in->current_now;
        out.kind = NUNCHUK_CMD_PID_SPEED;
        out.pid_rpm = app->pid_rpm;
        return out;
    }

    app->was_pid = false;

    float current = 0.0f;
    bool coast_brake = false;
    const float coast_brake_current = fabsf(app->config.coast_brake_level * in->lo_current_min);
    if (fabsf(out_val) < 0.01f && app->config.coast_brake_level > 0.005f &&
        (fabsf(app->prev_current) < coast_brake_current || app->coast_brake_prev)) {
        current = -coast_brake_current;
        coast_brake = true;
    } else if (app->config.ctrl_type == NUNCHUK_MODE_CURRENT_BIDIRECTIONAL) {
        if ((out_val > 0.0f && in->duty_now > 0.0f) || (out_val < 0.0f && in->duty_now < 0.0f)) {
            current = out_val * in->lo_current_max;
        } else {
            current = out_val * fabsf(in->lo_current_min);
        }
    } else {
        if (out_val >= 0.0f && ((app->is_reverse ? -1.0f : 1.0f) * in->duty_now) > 0.0f) {
            current = out_val * in->lo_current_max;
        } else {
            current = out_val * fabsf(in->lo_current_min);
        }
    }

    /* Leaving the coast brake sets the previous current to what is actually flowing. */
    if (app->coast_brake_prev && !coast_brake) {
        app->prev_current = in->current_now;
    }
    app->coast_brake_prev = coast_brake;

    /* The duty path that lets the nunchuk reverse into a slow spin. */
    if (app->config.use_smart_rev && app->config.ctrl_type != NUNCHUK_MODE_CURRENT_BIDIRECTIONAL) {
        bool duty_control = false;
        if (out_val < -0.92f && duty_highest_abs < (in->l_min_duty * 1.5f) &&
            fabsf(current_highest) < (in->l_current_max * in->l_current_max_scale * 0.7f)) {
            duty_control = true;
        }
        if (duty_control || (app->was_duty_control && out_val < -0.1f)) {
            app->was_duty_control = true;
            const float goal = app->config.smart_rev_max_duty * -out_val;
            nunchuk_step_towards(&app->duty_rev, app->is_reverse ? goal : -goal,
                                 app->config.smart_rev_max_duty * dt /
                                     app->config.smart_rev_ramp_time);
            app->prev_current = in->current_now;
            out.kind = NUNCHUK_CMD_DUTY;
            out.duty = app->duty_rev;
            return out;
        }
        app->duty_rev = in->duty_now;
        app->was_duty_control = false;
    }

    /* The ramp, whose step is a fraction of the whole current range the configuration allows. */
    const float current_range = in->l_current_max * in->l_current_max_scale +
                                fabsf(in->l_current_min) * in->l_current_min_scale;
    float ramp_time = (fabsf(current) > fabsf(app->prev_current)) ? app->config.ramp_time_pos
                                                                  : app->config.ramp_time_neg;
    if (coast_brake) {
        ramp_time = app->config.coast_brake_ramp_time;
    }
    if (ramp_time > 0.01f) {
        const float ramp_step =
            (NUNCHUK_OUTPUT_ITERATION_TIME_MS * current_range) / (ramp_time * 1000.0f);
        float current_goal = app->prev_current;
        nunchuk_step_towards(&current_goal, current, ramp_step);
        current = current_goal;
    }
    app->prev_current = current;

    if (current < 0.0f &&
        (app->config.ctrl_type != NUNCHUK_MODE_CURRENT_BIDIRECTIONAL || coast_brake)) {
        out.kind = NUNCHUK_CMD_BRAKE;
        out.current = current;
        return out;
    }

    current = app->is_reverse ? -current : current;
    float current_out = current;

    /* Traction control, against the slowest wheel the bus has reported. */
    if (app->config.multi_esc && app->config.tc && app->config.tc_max_diff > 1.0f) {
        const bool is_braking =
            (current > 0.0f && in->duty_now < 0.0f) || (current < 0.0f && in->duty_now > 0.0f);
        if (!is_braking) {
            const float diff = rpm_local - rpm_lowest;
            current_out = nunchuk_map(diff, 0.0f, app->config.tc_max_diff, current, 0.0f);
            if (fabsf(current_out) < in->cc_min_current) {
                current_out = 0.0f;
            }
        }
    }

    out.kind = NUNCHUK_CMD_CURRENT;
    out.current = current_out;
    return out;
}
