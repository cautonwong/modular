#include "zmk_battery/battery.h"

uint8_t zmk_battery_mv_to_pct(uint16_t mv) {
    // Standard LiPo / Li-ion curve (4200mV -> 100%, 3450mV -> 0%)
    if (mv >= 4200) {
        return 100;
    }
    if (mv <= 3450) {
        return 0;
    }
    int32_t pct = ((int32_t)mv * 2) / 15 - 459;
    return (uint8_t)pct;
}

static edge_status_t app_poll(edge_module_t *module) {
    zmk_battery_app_t *app = (zmk_battery_app_t *)edge_module_data(module);
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    return zmk_battery_sample(app);
}

static edge_status_t app_power_off(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

void zmk_battery_construct(zmk_battery_app_t *app, uint32_t module_id, uint8_t priority,
                           const zmk_battery_hw_if_t *hw,
                           const zmk_battery_event_sink_if_t *event_sink) {
    if (app == NULL) {
        return;
    }
    *app = (__typeof__(*app)){0};
    app->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 60000u, // Poll every 60s
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

    app->last_mv = 4200;
    app->last_percentage = 100;
    app->last_charging = false;
}

edge_status_t zmk_battery_init(zmk_battery_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    return zmk_battery_sample(app);
}

edge_status_t zmk_battery_sample(zmk_battery_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    if (app->hw.read_millivolts == NULL) {
        return EDGE_OK;
    }

    uint16_t mv = 0;
    edge_status_t rc = app->hw.read_millivolts(app->hw.self, &mv);
    if (rc != EDGE_OK) {
        return rc;
    }

    bool charging = (app->hw.is_charging != NULL) ? app->hw.is_charging(app->hw.self) : false;
    uint8_t pct = zmk_battery_mv_to_pct(mv);

    bool changed = (pct != app->last_percentage || charging != app->last_charging);
    app->last_mv = mv;
    app->last_percentage = pct;
    app->last_charging = charging;

    if (changed && app->event_sink.post_battery_changed != NULL) {
        return app->event_sink.post_battery_changed(app->event_sink.self, pct, charging);
    }

    return EDGE_OK;
}

uint8_t zmk_battery_get_percentage(const zmk_battery_app_t *app) {
    return (app != NULL) ? app->last_percentage : 0;
}

uint16_t zmk_battery_get_millivolts(const zmk_battery_app_t *app) {
    return (app != NULL) ? app->last_mv : 0;
}

bool zmk_battery_is_charging(const zmk_battery_app_t *app) {
    return (app != NULL) ? app->last_charging : false;
}
