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
