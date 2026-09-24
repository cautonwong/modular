#include "pas/pas.h"
#include <math.h>
#include <string.h>

static edge_status_t pas_poll(edge_module_t *mod) {
    pas_app_t *app = (pas_app_t *)edge_module_data(mod);
    return pas_update(app, 0.02f);
}

static edge_status_t pas_on_event(edge_module_t *mod, const edge_event_t *evt) {
    (void)mod;
    (void)evt;
    return EDGE_OK;
}

static edge_status_t pas_power_off(edge_module_t *mod) {
    pas_app_t *app = (pas_app_t *)edge_module_data(mod);
    if (app) {
        app->current_demand_a = 0.0f;
        app->active = false;
    }
    return EDGE_OK;
}

void pas_construct(pas_app_t *app, uint32_t module_id, uint32_t priority,
                   const pas_config_t *config, const pas_port_t *port) {
    if (!app) {
        return;
    }

    memset(app, 0, sizeof(*app));
    app->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 20u,
        .poll = pas_poll,
        .on_event = pas_on_event,
        .power_off = pas_power_off,
        .private_data = app,
    };

    if (config) {
        app->config = *config;
    }
    if (app->config.min_cadence_rpm <= 0.0f) {
        app->config.min_cadence_rpm = 10.0f;
    }
    if (app->config.max_cadence_rpm <= 0.0f) {
        app->config.max_cadence_rpm = 100.0f;
    }
    if (app->config.assist_ratio <= 0.0f) {
        app->config.assist_ratio = 1.0f;
    }
    if (app->config.max_motor_current_a <= 0.0f) {
        app->config.max_motor_current_a = 20.0f;
    }

    if (port) {
        app->port = *port;
    }
    app->active = false;
}

edge_status_t pas_init(pas_app_t *app) {
    if (!app || !app->port.read_cadence_rpm) {
        return EDGE_EINVAL;
    }
    app->current_demand_a = 0.0f;
    app->active = false;
    return EDGE_OK;
}

edge_status_t pas_update(pas_app_t *app, float dt) {
    if (!app || dt < 0.0f) {
        return EDGE_EINVAL;
    }

    float cadence = 0.0f;
    if (app->port.read_cadence_rpm(app->port.self, &cadence) != EDGE_OK) {
        app->current_demand_a = 0.0f;
        app->active = false;
        return EDGE_OK;
    }
    app->measured_cadence_rpm = cadence;

    if (cadence < app->config.min_cadence_rpm) {
        app->current_demand_a = 0.0f;
        app->active = false;
        return EDGE_OK;
    }

    app->active = true;

    float demand = 0.0f;
    if (app->config.type == PAS_SENSOR_TORQUE_AND_CADENCE && app->port.read_torque_nm) {
        float torque = 0.0f;
        app->port.read_torque_nm(app->port.self, &torque);
        app->measured_torque_nm = torque;
        demand = torque * app->config.assist_ratio * (cadence / 60.0f);
    } else {
        /* Cadence only mode */
        float cadence_norm = (cadence - app->config.min_cadence_rpm) /
                             (app->config.max_cadence_rpm - app->config.min_cadence_rpm);
        if (cadence_norm > 1.0f)
            cadence_norm = 1.0f;
        demand = cadence_norm * app->config.max_motor_current_a * app->config.assist_ratio;
    }

    if (demand > app->config.max_motor_current_a) {
        demand = app->config.max_motor_current_a;
    }
    if (demand < 0.0f) {
        demand = 0.0f;
    }

    app->current_demand_a = demand;
    return EDGE_OK;
}

float pas_get_current_demand(const pas_app_t *app) {
    if (!app || !app->active) {
        return 0.0f;
    }
    return app->current_demand_a;
}

bool pas_is_active(const pas_app_t *app) {
    if (!app) {
        return false;
    }
    return app->active;
}

edge_module_t *pas_module(pas_app_t *app) {
    return app ? &app->module : NULL;
}
