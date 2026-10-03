#include "zmk_pointing/pointing.h"

static edge_status_t zmk_pointing_poll(edge_module_t *module) {
    const zmk_pointing_app_t *self = (const zmk_pointing_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

static edge_status_t zmk_pointing_power_off(edge_module_t *module) {
    zmk_pointing_app_t *self = (zmk_pointing_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->current_buttons = 0;
    return EDGE_OK;
}

void zmk_pointing_construct(zmk_pointing_app_t *self, uint32_t module_id, uint32_t priority,
                            const zmk_pointing_hid_if_t *hid) {
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
        .poll = zmk_pointing_poll,
        .on_event = NULL,
        .power_off = zmk_pointing_power_off,
        .private_data = self,
    };
    self->hid = hid;
    self->x_scaler = (zmk_pointing_scaler_t){1, 1};
    self->y_scaler = (zmk_pointing_scaler_t){1, 1};
    self->scroll_scaler = (zmk_pointing_scaler_t){1, 1};
}

edge_status_t zmk_pointing_init(zmk_pointing_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->current_buttons = 0;
    self->total_dx = 0;
    self->total_dy = 0;
    return EDGE_OK;
}

edge_status_t zmk_pointing_shutdown(zmk_pointing_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->current_buttons = 0;
    return EDGE_OK;
}

void zmk_pointing_set_scaler(zmk_pointing_app_t *self, int16_t mul, int16_t div) {
    if (self != NULL && div != 0) {
        self->x_scaler = (zmk_pointing_scaler_t){mul, div};
        self->y_scaler = (zmk_pointing_scaler_t){mul, div};
        self->scroll_scaler = (zmk_pointing_scaler_t){mul, div};
    }
}

edge_status_t zmk_pointing_motion(zmk_pointing_app_t *self, int16_t dx, int16_t dy, int8_t v_scroll,
                                  int8_t h_scroll) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    int16_t scaled_dx = (int16_t)((dx * self->x_scaler.multiplier) / self->x_scaler.divisor);
    int16_t scaled_dy = (int16_t)((dy * self->y_scaler.multiplier) / self->y_scaler.divisor);
    int8_t scaled_v =
        (int8_t)((v_scroll * self->scroll_scaler.multiplier) / self->scroll_scaler.divisor);
    int8_t scaled_h =
        (int8_t)((h_scroll * self->scroll_scaler.multiplier) / self->scroll_scaler.divisor);

    self->total_dx += scaled_dx;
    self->total_dy += scaled_dy;

    if (self->hid != NULL && self->hid->report_motion != NULL) {
        return self->hid->report_motion(self->hid->self, scaled_dx, scaled_dy, scaled_v, scaled_h);
    }
    return EDGE_OK;
}

edge_status_t zmk_pointing_button_press(zmk_pointing_app_t *self, uint8_t button_mask) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->current_buttons |= button_mask;
    if (self->hid != NULL && self->hid->report_buttons != NULL) {
        return self->hid->report_buttons(self->hid->self, self->current_buttons);
    }
    return EDGE_OK;
}

edge_status_t zmk_pointing_button_release(zmk_pointing_app_t *self, uint8_t button_mask) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->current_buttons &= (uint8_t)~button_mask;
    if (self->hid != NULL && self->hid->report_buttons != NULL) {
        return self->hid->report_buttons(self->hid->self, self->current_buttons);
    }
    return EDGE_OK;
}
