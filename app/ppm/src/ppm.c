#include "ppm/ppm.h"

#include <math.h>
#include <string.h>

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
    if (app->config.pulse_deadband_us < 0.0f) {
        app->config.pulse_deadband_us = 50.0f;
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
    float deadband = app->config.pulse_deadband_us;
    float raw_out = 0.0f;

    if (p > center + deadband) {
        float span = app->config.pulse_max_us - (center + deadband);
        if (span > 0.0f) {
            raw_out = (p - (center + deadband)) / span;
        }
    } else if (p < center - deadband) {
        float span = (center - deadband) - app->config.pulse_min_us;
        if (span > 0.0f) {
            raw_out = (p - (center - deadband)) / span;
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

edge_module_t *ppm_module(ppm_app_t *app) {
    return app ? &app->module : NULL;
}
