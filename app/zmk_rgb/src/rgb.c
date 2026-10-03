#include "zmk_rgb/rgb.h"

static void flush_to_driver(zmk_rgb_app_t *self) {
    if (self->driver != NULL && self->driver->update_rgb != NULL) {
        self->driver->update_rgb(self->driver->self, self->on, self->hue, self->saturation,
                                 self->brightness, self->effect);
    }
}

static edge_status_t zmk_rgb_poll(edge_module_t *module) {
    const zmk_rgb_app_t *self = (const zmk_rgb_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

static edge_status_t zmk_rgb_power_off(edge_module_t *module) {
    zmk_rgb_app_t *self = (zmk_rgb_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->on = false;
    flush_to_driver(self);
    return EDGE_OK;
}

void zmk_rgb_construct(zmk_rgb_app_t *self, uint32_t module_id, uint32_t priority,
                       const zmk_rgb_driver_if_t *driver) {
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
        .poll = zmk_rgb_poll,
        .on_event = NULL,
        .power_off = zmk_rgb_power_off,
        .private_data = self,
    };
    self->driver = driver;
    self->on = true;
    self->hue = 0;
    self->saturation = 100;
    self->brightness = 100;
    self->effect = ZMK_RGB_EFFECT_SOLID;
    self->speed = 1;
}

edge_status_t zmk_rgb_init(zmk_rgb_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    flush_to_driver(self);
    return EDGE_OK;
}

edge_status_t zmk_rgb_shutdown(zmk_rgb_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->on = false;
    flush_to_driver(self);
    return EDGE_OK;
}

edge_status_t zmk_rgb_toggle(zmk_rgb_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->on = !self->on;
    flush_to_driver(self);
    return EDGE_OK;
}

edge_status_t zmk_rgb_on(zmk_rgb_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->on = true;
    flush_to_driver(self);
    return EDGE_OK;
}

edge_status_t zmk_rgb_off(zmk_rgb_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->on = false;
    flush_to_driver(self);
    return EDGE_OK;
}

edge_status_t zmk_rgb_set_hue(zmk_rgb_app_t *self, uint16_t hue) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->hue = hue % 360;
    flush_to_driver(self);
    return EDGE_OK;
}

edge_status_t zmk_rgb_set_saturation(zmk_rgb_app_t *self, uint8_t saturation) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->saturation = (saturation > 100) ? 100 : saturation;
    flush_to_driver(self);
    return EDGE_OK;
}

edge_status_t zmk_rgb_set_brightness(zmk_rgb_app_t *self, uint8_t brightness) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->brightness = (brightness > 100) ? 100 : brightness;
    flush_to_driver(self);
    return EDGE_OK;
}

edge_status_t zmk_rgb_set_effect(zmk_rgb_app_t *self, uint8_t effect) {
    if (self == NULL || effect >= ZMK_RGB_EFFECT_COUNT) {
        return EDGE_EINVAL;
    }
    self->effect = effect;
    flush_to_driver(self);
    return EDGE_OK;
}

edge_status_t zmk_rgb_next_effect(zmk_rgb_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->effect = (self->effect + 1) % ZMK_RGB_EFFECT_COUNT;
    flush_to_driver(self);
    return EDGE_OK;
}
