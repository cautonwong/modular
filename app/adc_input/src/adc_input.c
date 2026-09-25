#include "adc_input/adc_input.h"

#include <math.h>
#include <string.h>

static edge_status_t adc_input_poll(edge_module_t *mod) {
    adc_input_app_t *app = (adc_input_app_t *)edge_module_data(mod);
    return adc_input_update(app);
}

static edge_status_t adc_input_on_event(edge_module_t *mod, const edge_event_t *evt) {
    (void)mod;
    (void)evt;
    return EDGE_OK;
}

static edge_status_t adc_input_power_off(edge_module_t *mod) {
    adc_input_app_t *app = (adc_input_app_t *)edge_module_data(mod);
    if (app) {
        app->throttle_norm = 0.0f;
        app->brake_norm = 0.0f;
        app->throttle_v = 0.0f;
        app->brake_v = 0.0f;
    }
    return EDGE_OK;
}

void adc_input_construct(adc_input_app_t *app, uint32_t module_id, uint32_t priority,
                         const adc_input_config_t *config, const adc_input_port_t *port) {
    if (!app) {
        return;
    }

    memset(app, 0, sizeof(*app));
    app->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 20u,
        .poll = adc_input_poll,
        .on_event = adc_input_on_event,
        .power_off = adc_input_power_off,
        .private_data = app,
    };

    if (config) {
        app->config = *config;
    }
    if (app->config.voltage_min <= 0.0f) {
        app->config.voltage_min = 0.2f;
    }
    if (app->config.voltage_max <= 0.0f) {
        app->config.voltage_max = 3.2f;
    }
    if (app->config.voltage_start <= 0.0f) {
        app->config.voltage_start = 0.8f;
    }
    if (app->config.voltage_end <= 0.0f) {
        app->config.voltage_end = 2.6f;
    }
    if (app->config.voltage_center <= 0.0f) {
        app->config.voltage_center = 1.7f;
    }

    if (port) {
        app->port = *port;
    }
    app->safe_start_unlocked = !app->config.safe_start;
    app->fault_wire_disconnected = false;
}

edge_status_t adc_input_init(adc_input_app_t *app) {
    if (!app || !app->port.read_throttle_v) {
        return EDGE_EINVAL;
    }
    app->safe_start_unlocked = !app->config.safe_start;
    app->fault_wire_disconnected = false;
    return EDGE_OK;
}

edge_status_t adc_input_update(adc_input_app_t *app) {
    if (!app) {
        return EDGE_EINVAL;
    }

    float v_throttle = 0.0f;
    edge_status_t st = app->port.read_throttle_v(app->port.self, &v_throttle);
    if (st != EDGE_OK) {
        app->fault_wire_disconnected = true;
        app->throttle_norm = 0.0f;
        app->brake_norm = 0.0f;
        return st;
    }

    /* Out of bounds fault check */
    if (v_throttle < app->config.voltage_min || v_throttle > app->config.voltage_max) {
        app->fault_wire_disconnected = true;
        app->throttle_norm = 0.0f;
        app->brake_norm = 0.0f;
        return EDGE_OK;
    }

    app->fault_wire_disconnected = false;
    app->throttle_v = v_throttle;

    /* Normalize throttle */
    float t_norm = 0.0f;
    if (v_throttle <= app->config.voltage_start) {
        t_norm = 0.0f;
    } else if (v_throttle >= app->config.voltage_end) {
        t_norm = 1.0f;
    } else {
        float span = app->config.voltage_end - app->config.voltage_start;
        if (span > 0.001f) {
            t_norm = (v_throttle - app->config.voltage_start) / span;
        }
    }

    /* Check dedicated brake channel */
    float b_norm = 0.0f;
    if (app->config.use_brake_input && app->port.read_brake_v) {
        float v_brake = 0.0f;
        if (app->port.read_brake_v(app->port.self, &v_brake) == EDGE_OK) {
            app->brake_v = v_brake;
            if (v_brake > app->config.brake_start) {
                float b_span = app->config.brake_end - app->config.brake_start;
                if (b_span > 0.001f) {
                    b_norm = (v_brake - app->config.brake_start) / b_span;
                    if (b_norm > 1.0f) {
                        b_norm = 1.0f;
                    }
                }
            }
        }
    }

    /* Safe start check */
    if (!app->safe_start_unlocked) {
        if (t_norm < 0.05f) {
            app->safe_start_unlocked = true;
        } else {
            app->throttle_norm = 0.0f;
            app->brake_norm = 0.0f;
            return EDGE_OK;
        }
    }

    app->throttle_norm = t_norm;
    app->brake_norm = b_norm;

    return EDGE_OK;
}

float adc_input_get_throttle_v(const adc_input_app_t *app) {
    return app != (void *)0 ? app->throttle_v : 0.0f;
}

float adc_input_get_brake_v(const adc_input_app_t *app) {
    return app != (void *)0 ? app->brake_v : 0.0f;
}

float adc_input_get_throttle(const adc_input_app_t *app) {
    if (!app || app->fault_wire_disconnected) {
        return 0.0f;
    }
    return app->throttle_norm;
}

float adc_input_get_brake(const adc_input_app_t *app) {
    if (!app || app->fault_wire_disconnected) {
        return 0.0f;
    }
    return app->brake_norm;
}

bool adc_input_has_fault(const adc_input_app_t *app) {
    if (!app) {
        return true;
    }
    return app->fault_wire_disconnected;
}

edge_module_t *adc_input_module(adc_input_app_t *app) {
    return app ? &app->module : NULL;
}
