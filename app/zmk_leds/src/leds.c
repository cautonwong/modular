#include "zmk_leds/leds.h"

enum {
    ZMK_ACTIVITY_ACTIVE = 0,
    ZMK_ACTIVITY_IDLE = 1,
    ZMK_ACTIVITY_SLEEP = 2,
};

static bool is_led_disabled(const zmk_led_config_t *config, const zmk_leds_app_t *app) {
    if (app->pm_suspended) {
        return true;
    }
    if (app->usb_powered) {
        return false;
    }
    switch (app->activity_state) {
    case ZMK_ACTIVITY_ACTIVE:
        return false;
    case ZMK_ACTIVITY_IDLE:
        return !config->on_while_idle;
    case ZMK_ACTIVITY_SLEEP:
        return true;
    default:
        return false;
    }
}

static uint8_t get_brightness(const zmk_led_config_t *config, const zmk_leds_app_t *app) {
    if (is_led_disabled(config, app)) {
        return 0;
    }
    if (!app->endpoint_connected) {
        return config->disconnected_brightness;
    }
    const bool active = (app->active_indicators & config->indicator_mask) != 0;
    return active ? config->active_brightness : config->inactive_brightness;
}

static edge_status_t app_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t app_power_off(edge_module_t *module) {
    zmk_leds_app_t *app = (zmk_leds_app_t *)edge_module_data(module);
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->pm_suspended = true;
    return zmk_leds_update(app);
}

void zmk_leds_construct(zmk_leds_app_t *app, uint32_t module_id, uint8_t priority,
                        const zmk_leds_hw_if_t *hw) {
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

    if (hw != NULL) {
        app->hw = *hw;
    }
    app->endpoint_connected = true;
    app->usb_powered = true;
    app->activity_state = ZMK_ACTIVITY_ACTIVE;
}

edge_status_t zmk_leds_init(zmk_leds_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    return zmk_leds_update(app);
}

edge_status_t zmk_leds_add_indicator(zmk_leds_app_t *app, const zmk_led_config_t *config) {
    if (app == NULL || config == NULL) {
        return EDGE_EINVAL;
    }
    if (app->config_count >= ZMK_LEDS_MAX_INDICATORS) {
        return EDGE_ENOSPC;
    }
    app->configs[app->config_count++] = *config;
    return zmk_leds_update(app);
}

edge_status_t zmk_leds_update(zmk_leds_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    if (app->hw.set_brightness == NULL) {
        return EDGE_OK;
    }

    for (uint8_t i = 0; i < app->config_count; i++) {
        uint8_t brightness = get_brightness(&app->configs[i], app);
        edge_status_t rc =
            app->hw.set_brightness(app->hw.self, app->configs[i].led_index, brightness);
        if (rc != EDGE_OK) {
            return rc;
        }
    }
    return EDGE_OK;
}

edge_status_t zmk_leds_on_hid_indicators(zmk_leds_app_t *app, uint8_t indicators) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->active_indicators = indicators;
    return zmk_leds_update(app);
}

edge_status_t zmk_leds_on_activity_state(zmk_leds_app_t *app, uint8_t activity_state) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->activity_state = activity_state;
    return zmk_leds_update(app);
}

edge_status_t zmk_leds_on_endpoint_status(zmk_leds_app_t *app, bool connected,
                                          bool is_usb_powered) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->endpoint_connected = connected;
    app->usb_powered = is_usb_powered;
    return zmk_leds_update(app);
}
