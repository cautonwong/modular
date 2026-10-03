#include "zmk_endpoints/endpoints.h"

static void notify_change(zmk_endpoints_app_t *self) {
    if (self->sink != NULL && self->sink->post_endpoint_changed != NULL) {
        self->sink->post_endpoint_changed(self->sink->self, self->current_endpoint,
                                          self->active_ble_profile);
    }
}

static edge_status_t zmk_endpoints_poll(edge_module_t *module) {
    const zmk_endpoints_app_t *self = (const zmk_endpoints_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

static edge_status_t zmk_endpoints_power_off(edge_module_t *module) {
    const zmk_endpoints_app_t *self = (const zmk_endpoints_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

void zmk_endpoints_construct(zmk_endpoints_app_t *self, uint32_t module_id, uint32_t priority,
                             const zmk_endpoint_event_sink_if_t *sink) {
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
        .poll = zmk_endpoints_poll,
        .on_event = NULL,
        .power_off = zmk_endpoints_power_off,
        .private_data = self,
    };
    self->sink = sink;
    self->current_endpoint = ZMK_ENDPOINT_USB;
    self->active_ble_profile = 0;
}

edge_status_t zmk_endpoints_init(zmk_endpoints_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->current_endpoint = ZMK_ENDPOINT_USB;
    self->active_ble_profile = 0;
    self->usb_connected = false;
    for (size_t i = 0; i < sizeof(self->ble_connected); ++i)
        self->ble_connected[i] = false;
    return EDGE_OK;
}

// cppcheck-suppress constParameterPointer
edge_status_t zmk_endpoints_shutdown(zmk_endpoints_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

edge_status_t zmk_endpoints_select_endpoint(zmk_endpoints_app_t *self, uint8_t endpoint) {
    if (self == NULL || (endpoint != ZMK_ENDPOINT_USB && endpoint != ZMK_ENDPOINT_BLE)) {
        return EDGE_EINVAL;
    }
    if (self->current_endpoint != endpoint) {
        self->current_endpoint = endpoint;
        notify_change(self);
    }
    return EDGE_OK;
}

edge_status_t zmk_endpoints_toggle_endpoint(zmk_endpoints_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->current_endpoint =
        (self->current_endpoint == ZMK_ENDPOINT_USB) ? ZMK_ENDPOINT_BLE : ZMK_ENDPOINT_USB;
    notify_change(self);
    return EDGE_OK;
}

uint8_t zmk_endpoints_get_current_endpoint(const zmk_endpoints_app_t *self) {
    if (self == NULL) {
        return ZMK_ENDPOINT_USB;
    }
    return self->current_endpoint;
}

edge_status_t zmk_endpoints_select_profile(zmk_endpoints_app_t *self, uint8_t profile) {
    if (self == NULL || profile >= ZMK_MAX_BLE_PROFILES) {
        return EDGE_EINVAL;
    }
    if (self->active_ble_profile != profile) {
        self->active_ble_profile = profile;
        notify_change(self);
    }
    return EDGE_OK;
}

edge_status_t zmk_endpoints_next_profile(zmk_endpoints_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->active_ble_profile = (self->active_ble_profile + 1) % ZMK_MAX_BLE_PROFILES;
    notify_change(self);
    return EDGE_OK;
}

edge_status_t zmk_endpoints_prev_profile(zmk_endpoints_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->active_ble_profile =
        (self->active_ble_profile + ZMK_MAX_BLE_PROFILES - 1) % ZMK_MAX_BLE_PROFILES;
    notify_change(self);
    return EDGE_OK;
}

uint8_t zmk_endpoints_get_active_profile(const zmk_endpoints_app_t *self) {
    if (self == NULL) {
        return 0;
    }
    return self->active_ble_profile;
}

void zmk_endpoints_set_usb_status(zmk_endpoints_app_t *self, bool connected) {
    if (self != NULL) {
        self->usb_connected = connected;
    }
}

void zmk_endpoints_set_ble_status(zmk_endpoints_app_t *self, uint8_t profile, bool connected) {
    if (self != NULL && profile < ZMK_MAX_BLE_PROFILES) {
        self->ble_connected[profile] = connected;
    }
}
