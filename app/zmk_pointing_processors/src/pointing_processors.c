#include "zmk_pointing_processors/pointing_processors.h"

static int16_t scale_axis(int16_t val, uint16_t mul, uint16_t div, int16_t *remainder) {
    if (mul == 0 || div == 0) {
        return val;
    }
    int32_t total = (int32_t)val * mul + *remainder;
    int16_t scaled = (int16_t)(total / div);
    *remainder = (int16_t)(total - (scaled * div));
    return scaled;
}

static edge_status_t app_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t app_power_off(edge_module_t *module) {
    zmk_pointing_processors_app_t *app = (zmk_pointing_processors_app_t *)edge_module_data(module);
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    if (app->layer_active && app->sink.set_temp_layer != NULL) {
        app->sink.set_temp_layer(app->sink.self, app->temp_layer.toggle_layer, false);
        app->layer_active = false;
    }
    app->remainder_x = 0;
    app->remainder_y = 0;
    app->remainder_wheel = 0;
    return EDGE_OK;
}

void zmk_pointing_processors_construct(zmk_pointing_processors_app_t *app, uint32_t module_id,
                                       uint8_t priority, const zmk_pointing_proc_sink_if_t *sink) {
    if (app == NULL) {
        return;
    }
    *app = (__typeof__(*app)){0};
    app->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 1u,
        .poll = app_poll,
        .power_off = app_power_off,
        .private_data = app,
    };

    if (sink != NULL) {
        app->sink = *sink;
    }

    app->scaler.mul = 1;
    app->scaler.div = 1;
    app->temp_layer.enabled = false;
}

edge_status_t zmk_pointing_processors_init(zmk_pointing_processors_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->remainder_x = 0;
    app->remainder_y = 0;
    app->remainder_wheel = 0;
    app->layer_active = false;
    return EDGE_OK;
}

void zmk_pointing_processors_set_scaler(zmk_pointing_processors_app_t *app, uint16_t mul,
                                        uint16_t div) {
    if (app == NULL || div == 0) {
        return;
    }
    app->scaler.mul = mul;
    app->scaler.div = div;
}

void zmk_pointing_processors_set_temp_layer(zmk_pointing_processors_app_t *app, uint8_t layer,
                                            uint32_t timeout_ms) {
    if (app == NULL) {
        return;
    }
    app->temp_layer.toggle_layer = layer;
    app->temp_layer.timeout_ms = timeout_ms;
    app->temp_layer.enabled = (timeout_ms > 0);
}

edge_status_t zmk_pointing_processors_process_motion(zmk_pointing_processors_app_t *app,
                                                     int16_t raw_dx, int16_t raw_dy,
                                                     int16_t raw_dwheel, uint32_t timestamp_ms) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }

    int16_t scaled_dx = scale_axis(raw_dx, app->scaler.mul, app->scaler.div, &app->remainder_x);
    int16_t scaled_dy = scale_axis(raw_dy, app->scaler.mul, app->scaler.div, &app->remainder_y);
    int16_t scaled_dwheel =
        scale_axis(raw_dwheel, app->scaler.mul, app->scaler.div, &app->remainder_wheel);

    // Handle momentary layer on motion
    if (app->temp_layer.enabled && (raw_dx != 0 || raw_dy != 0 || raw_dwheel != 0)) {
        if (!app->layer_active && app->sink.set_temp_layer != NULL) {
            app->sink.set_temp_layer(app->sink.self, app->temp_layer.toggle_layer, true);
            app->layer_active = true;
        }
        app->layer_disable_deadline_ms = timestamp_ms + app->temp_layer.timeout_ms;
    }

    if (app->sink.forward_motion != NULL) {
        return app->sink.forward_motion(app->sink.self, scaled_dx, scaled_dy, scaled_dwheel);
    }

    return EDGE_OK;
}

edge_status_t zmk_pointing_processors_process_tick(zmk_pointing_processors_app_t *app,
                                                   uint32_t current_time_ms) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }

    if (app->layer_active && (int32_t)(current_time_ms - app->layer_disable_deadline_ms) >= 0) {
        if (app->sink.set_temp_layer != NULL) {
            app->sink.set_temp_layer(app->sink.self, app->temp_layer.toggle_layer, false);
        }
        app->layer_active = false;
    }

    return EDGE_OK;
}
