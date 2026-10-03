#include "zmk_ext_power/ext_power.h"

enum {
    ZMK_ACTIVITY_ACTIVE = 0,
    ZMK_ACTIVITY_IDLE = 1,
    ZMK_ACTIVITY_SLEEP = 2,
};

static edge_status_t update_hardware(zmk_ext_power_app_t *app) {
    bool effective_on = app->enabled && (!app->pm_sleeping || !app->auto_off_on_sleep);
    if (app->hw.set_power != NULL) {
        edge_status_t rc = app->hw.set_power(app->hw.self, effective_on);
        if (rc != EDGE_OK) {
            return rc;
        }
    }
    if (app->event_sink.post_power_changed != NULL) {
        return app->event_sink.post_power_changed(app->event_sink.self, effective_on);
    }
    return EDGE_OK;
}

static edge_status_t app_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t app_power_off(edge_module_t *module) {
    zmk_ext_power_app_t *app = (zmk_ext_power_app_t *)edge_module_data(module);
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->pm_sleeping = true;
    return update_hardware(app);
}

void zmk_ext_power_construct(zmk_ext_power_app_t *app, uint32_t module_id, uint8_t priority,
                             const zmk_ext_power_hw_if_t *hw,
                             const zmk_ext_power_event_sink_if_t *event_sink) {
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
    if (event_sink != NULL) {
        app->event_sink = *event_sink;
    }

    app->enabled = true;
    app->auto_off_on_sleep = true;
    app->pm_sleeping = false;
}

edge_status_t zmk_ext_power_init(zmk_ext_power_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    return update_hardware(app);
}

edge_status_t zmk_ext_power_enable(zmk_ext_power_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->enabled = true;
    return update_hardware(app);
}

edge_status_t zmk_ext_power_disable(zmk_ext_power_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->enabled = false;
    return update_hardware(app);
}

edge_status_t zmk_ext_power_toggle(zmk_ext_power_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->enabled = !app->enabled;
    return update_hardware(app);
}

bool zmk_ext_power_is_enabled(const zmk_ext_power_app_t *app) {
    return (app != NULL) ? app->enabled : false;
}

edge_status_t zmk_ext_power_on_activity_change(zmk_ext_power_app_t *app, uint8_t activity_state) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    if (activity_state == ZMK_ACTIVITY_SLEEP) {
        app->pm_sleeping = true;
    } else {
        app->pm_sleeping = false;
    }
    return update_hardware(app);
}
