#include "zmk_pm/pm.h"

static void notify_state(zmk_pm_app_t *self, uint8_t state) {
    self->current_state = state;
    if (self->sink != NULL && self->sink->on_activity_state_changed != NULL) {
        self->sink->on_activity_state_changed(self->sink->self, state);
    }
}

static edge_status_t zmk_pm_poll(edge_module_t *module) {
    zmk_pm_app_t *self = (zmk_pm_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return zmk_pm_tick(self, self->current_time_ms);
}

static edge_status_t zmk_pm_power_off(edge_module_t *module) {
    zmk_pm_app_t *self = (zmk_pm_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    notify_state(self, ZMK_ACTIVITY_SLEEP);
    return EDGE_OK;
}

void zmk_pm_construct(zmk_pm_app_t *self, uint32_t module_id, uint32_t priority,
                      const zmk_pm_config_t *config, const zmk_pm_sink_if_t *sink) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 1u,
        .budget = 0u,
        .next_due = 0u,
        .poll = zmk_pm_poll,
        .on_event = NULL,
        .power_off = zmk_pm_power_off,
        .private_data = self,
    };
    self->sink = sink;
    if (config != NULL) {
        self->config = *config;
    } else {
        self->config.idle_timeout_ms = 30000;
        self->config.sleep_timeout_ms = 900000;
    }
    self->current_state = ZMK_ACTIVITY_ACTIVE;
}

edge_status_t zmk_pm_init(zmk_pm_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->current_state = ZMK_ACTIVITY_ACTIVE;
    self->last_activity_time_ms = 0;
    self->current_time_ms = 0;
    return EDGE_OK;
}

// cppcheck-suppress constParameterPointer
edge_status_t zmk_pm_shutdown(zmk_pm_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

void zmk_pm_notify_activity(zmk_pm_app_t *self, uint32_t timestamp_ms) {
    if (self == NULL) {
        return;
    }
    self->last_activity_time_ms = timestamp_ms;
    self->current_time_ms = timestamp_ms;
    if (self->current_state != ZMK_ACTIVITY_ACTIVE) {
        notify_state(self, ZMK_ACTIVITY_ACTIVE);
    }
}

edge_status_t zmk_pm_tick(zmk_pm_app_t *self, uint32_t timestamp_ms) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->current_time_ms = timestamp_ms;
    uint32_t elapsed = timestamp_ms - self->last_activity_time_ms;

    if (self->config.sleep_timeout_ms > 0 && elapsed >= self->config.sleep_timeout_ms) {
        if (self->current_state != ZMK_ACTIVITY_SLEEP) {
            notify_state(self, ZMK_ACTIVITY_SLEEP);
        }
    } else if (self->config.idle_timeout_ms > 0 && elapsed >= self->config.idle_timeout_ms) {
        if (self->current_state != ZMK_ACTIVITY_IDLE && self->current_state != ZMK_ACTIVITY_SLEEP) {
            notify_state(self, ZMK_ACTIVITY_IDLE);
        }
    }

    return EDGE_OK;
}

uint8_t zmk_pm_get_activity_state(const zmk_pm_app_t *self) {
    if (self == NULL) {
        return ZMK_ACTIVITY_SLEEP;
    }
    return self->current_state;
}
