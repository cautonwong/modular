#include "ble_passkey/ble_passkey.h"

static edge_status_t ble_passkey_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t ble_passkey_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    return EDGE_OK;
}

static edge_status_t ble_passkey_power_off(edge_module_t *module) {
    ble_passkey_app_t *self = (ble_passkey_app_t *)module->private_data;
    if (self != NULL) {
        self->state = BLE_PASSKEY_IDLE;
    }
    return EDGE_OK;
}

void ble_passkey_construct(ble_passkey_app_t *self, uint32_t module_id, uint32_t priority) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->module.module_id = module_id;
    self->module.priority = priority;
    self->module.period = 100u;
    self->module.budget = 1u;
    self->module.next_due = 0u;
    self->module.poll = ble_passkey_poll;
    self->module.on_event = ble_passkey_on_event;
    self->module.power_off = ble_passkey_power_off;
    self->module.private_data = self;

    self->passkey = 0;
    self->state = BLE_PASSKEY_IDLE;
}

edge_status_t ble_passkey_init(ble_passkey_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    ble_passkey_construct(self, 0x3B00u, 40u);
    return EDGE_OK;
}

edge_status_t ble_passkey_shutdown(ble_passkey_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->state = BLE_PASSKEY_IDLE;
    return EDGE_OK;
}

edge_status_t ble_passkey_show(ble_passkey_app_t *self, uint32_t key, uint32_t timeout_ms) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->passkey = key % 1000000u; /* 6-digit key */
    self->timeout_ms = timeout_ms;
    self->state = BLE_PASSKEY_DISPLAYING;
    return EDGE_OK;
}

void ble_passkey_confirm(ble_passkey_app_t *self) {
    if (self != NULL) {
        self->state = BLE_PASSKEY_CONFIRMED;
    }
}

void ble_passkey_dismiss(ble_passkey_app_t *self) {
    if (self != NULL) {
        self->state = BLE_PASSKEY_IDLE;
    }
}

uint32_t ble_passkey_get_key(const ble_passkey_app_t *self) {
    return self != NULL ? self->passkey : 0;
}

ble_passkey_state_t ble_passkey_get_state(const ble_passkey_app_t *self) {
    return self != NULL ? self->state : BLE_PASSKEY_IDLE;
}
