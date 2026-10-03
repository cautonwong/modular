#include "zmk_backlight/backlight.h"

enum {
    ZMK_ACTIVITY_ACTIVE = 0,
    ZMK_ACTIVITY_IDLE = 1,
    ZMK_ACTIVITY_SLEEP = 2,
};

static edge_status_t update_hardware(zmk_backlight_app_t *app) {
    if (app->hw.set_brightness == NULL) {
        return EDGE_OK;
    }
    uint8_t effective_brt = (app->on && !app->is_sleeping) ? app->brightness : 0;
    return app->hw.set_brightness(app->hw.self, effective_brt);
}

static edge_status_t app_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t app_power_off(edge_module_t *module) {
    zmk_backlight_app_t *app = (zmk_backlight_app_t *)edge_module_data(module);
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->is_sleeping = true;
    return update_hardware(app);
}

void zmk_backlight_construct(zmk_backlight_app_t *app, uint32_t module_id, uint8_t priority,
                             const zmk_backlight_hw_if_t *hw) {
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

    app->brightness = 100;
    app->step = ZMK_BACKLIGHT_DEFAULT_STEP;
    app->on = true;
    app->auto_off_idle = true;
    app->is_sleeping = false;
}

edge_status_t zmk_backlight_init(zmk_backlight_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    return update_hardware(app);
}

edge_status_t zmk_backlight_on(zmk_backlight_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    if (app->brightness == 0) {
        app->brightness = app->step;
    }
    app->on = true;
    return update_hardware(app);
}

edge_status_t zmk_backlight_off(zmk_backlight_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->on = false;
    return update_hardware(app);
}

edge_status_t zmk_backlight_toggle(zmk_backlight_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->on = !app->on;
    return update_hardware(app);
}

edge_status_t zmk_backlight_set_brt(zmk_backlight_app_t *app, uint8_t brightness) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    if (brightness > ZMK_BACKLIGHT_BRT_MAX) {
        brightness = ZMK_BACKLIGHT_BRT_MAX;
    }
    app->brightness = brightness;
    app->on = (brightness > 0);
    return update_hardware(app);
}

edge_status_t zmk_backlight_adjust_brt(zmk_backlight_app_t *app, int8_t direction) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    int16_t new_brt = (int16_t)app->brightness + ((int16_t)direction * app->step);
    if (new_brt < 0) {
        new_brt = 0;
    } else if (new_brt > ZMK_BACKLIGHT_BRT_MAX) {
        new_brt = ZMK_BACKLIGHT_BRT_MAX;
    }
    app->brightness = (uint8_t)new_brt;
    app->on = (new_brt > 0);
    return update_hardware(app);
}

edge_status_t zmk_backlight_cycle_brt(zmk_backlight_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    if (app->brightness >= ZMK_BACKLIGHT_BRT_MAX) {
        return zmk_backlight_set_brt(app, 0);
    }
    return zmk_backlight_adjust_brt(app, 1);
}

uint8_t zmk_backlight_get_brt(const zmk_backlight_app_t *app) {
    return (app != NULL && app->on) ? app->brightness : 0;
}

bool zmk_backlight_is_on(const zmk_backlight_app_t *app) {
    return (app != NULL) ? app->on : false;
}

edge_status_t zmk_backlight_on_activity_state(zmk_backlight_app_t *app, uint8_t activity_state) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    if (activity_state == ZMK_ACTIVITY_SLEEP ||
        (activity_state == ZMK_ACTIVITY_IDLE && app->auto_off_idle)) {
        app->is_sleeping = true;
    } else {
        app->is_sleeping = false;
    }
    return update_hardware(app);
}
