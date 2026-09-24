#include "nunchuk/nunchuk.h"
#include <math.h>
#include <string.h>

static edge_status_t nunchuk_poll(edge_module_t *mod) {
    nunchuk_app_t *app = (nunchuk_app_t *)edge_module_data(mod);
    return nunchuk_update(app, 0.02f);
}

static edge_status_t nunchuk_on_event(edge_module_t *mod, const edge_event_t *evt) {
    (void)mod;
    (void)evt;
    return EDGE_OK;
}

static edge_status_t nunchuk_power_off(edge_module_t *mod) {
    nunchuk_app_t *app = (nunchuk_app_t *)edge_module_data(mod);
    if (app) {
        app->output_norm = 0.0f;
        app->disconnected = true;
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
        .period = 20u,
        .poll = nunchuk_poll,
        .on_event = nunchuk_on_event,
        .power_off = nunchuk_power_off,
        .private_data = app,
    };

    if (config) {
        app->config = *config;
    }
    if (app->config.deadband <= 0.0f) {
        app->config.deadband = 0.05f;
    }
    if (app->config.timeout_s <= 0.0f) {
        app->config.timeout_s = 0.2f;
    }

    if (port) {
        app->port = *port;
    }
    app->disconnected = true;
}

edge_status_t nunchuk_init(nunchuk_app_t *app) {
    if (!app || !app->port.read_data) {
        return EDGE_EINVAL;
    }
    app->disconnected = true;
    app->output_norm = 0.0f;
    app->cruise_active = false;
    return EDGE_OK;
}

edge_status_t nunchuk_update(nunchuk_app_t *app, float dt) {
    if (!app || dt < 0.0f) {
        return EDGE_EINVAL;
    }

    uint8_t js_x = 128, js_y = 128;
    int16_t acc_x = 0, acc_y = 0, acc_z = 0;
    bool btn_c = false, btn_z = false;

    edge_status_t st =
        app->port.read_data(app->port.self, &js_x, &js_y, &acc_x, &acc_y, &acc_z, &btn_c, &btn_z);

    if (st == EDGE_OK) {
        app->disconnected = false;
        app->time_since_update_s = 0.0f;
        app->btn_c_pressed = btn_c;
        app->btn_z_pressed = btn_z;

        /* Normalize Y axis: 128 is center -> [-1.0, 1.0] */
        float raw_y = ((float)js_y - 128.0f) / 128.0f;
        if (raw_y > 1.0f)
            raw_y = 1.0f;
        if (raw_y < -1.0f)
            raw_y = -1.0f;

        if (fabsf(raw_y) < app->config.deadband) {
            raw_y = 0.0f;
        }

        /* Button Z handles cruise control / activation */
        if (btn_c && !app->cruise_active && fabsf(raw_y) > 0.1f) {
            app->cruise_active = true;
            app->cruise_value = raw_y;
        } else if (!btn_c) {
            app->cruise_active = false;
        }

        if (app->cruise_active) {
            app->output_norm = app->cruise_value;
        } else {
            app->output_norm = raw_y;
        }
    } else {
        app->time_since_update_s += dt;
        if (app->time_since_update_s > app->config.timeout_s) {
            app->disconnected = true;
            app->output_norm = 0.0f;
            app->cruise_active = false;
        }
    }

    return EDGE_OK;
}

float nunchuk_get_output(const nunchuk_app_t *app) {
    if (!app || app->disconnected) {
        return 0.0f;
    }
    return app->output_norm;
}

bool nunchuk_is_connected(const nunchuk_app_t *app) {
    if (!app) {
        return false;
    }
    return !app->disconnected;
}

edge_module_t *nunchuk_module(nunchuk_app_t *app) {
    return app ? &app->module : NULL;
}
