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

void zmk_rgb_hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (r == NULL || g == NULL || b == NULL) {
        return;
    }
    if (s == 0) {
        uint8_t val = (uint8_t)((v * 255u) / 100u);
        *r = val;
        *g = val;
        *b = val;
        return;
    }

    h = h % 360;
    uint32_t region = h / 60;
    uint32_t remainder = (h - (region * 60)) * 6;

    uint32_t p = (v * (100u - s) * 255u) / 10000u;
    uint32_t q = (v * (100u - ((s * remainder) / 360u)) * 255u) / 10000u;
    uint32_t t = (v * (100u - ((s * (360u - remainder)) / 360u)) * 255u) / 10000u;
    uint32_t val = (v * 255u) / 100u;

    switch (region) {
    case 0:
        *r = (uint8_t)val;
        *g = (uint8_t)t;
        *b = (uint8_t)p;
        break;
    case 1:
        *r = (uint8_t)q;
        *g = (uint8_t)val;
        *b = (uint8_t)p;
        break;
    case 2:
        *r = (uint8_t)p;
        *g = (uint8_t)val;
        *b = (uint8_t)t;
        break;
    case 3:
        *r = (uint8_t)p;
        *g = (uint8_t)q;
        *b = (uint8_t)val;
        break;
    case 4:
        *r = (uint8_t)t;
        *g = (uint8_t)p;
        *b = (uint8_t)val;
        break;
    default:
        *r = (uint8_t)val;
        *g = (uint8_t)p;
        *b = (uint8_t)q;
        break;
    }
}

void zmk_rgb_render_frame(const zmk_rgb_app_t *self, uint32_t timestamp_ms, uint16_t led_count,
                          zmk_rgb_pixel_t *out_pixels) {
    if (self == NULL || out_pixels == NULL || led_count == 0) {
        return;
    }
    if (!self->on) {
        for (uint16_t i = 0; i < led_count; i++) {
            out_pixels[i] = (zmk_rgb_pixel_t){0, 0, 0};
        }
        return;
    }

    uint32_t speed = (self->speed > 0) ? self->speed : 1;
    switch (self->effect) {
    case ZMK_RGB_EFFECT_SOLID: {
        uint8_t r = 0, g = 0, b = 0;
        zmk_rgb_hsv_to_rgb(self->hue, self->saturation, self->brightness, &r, &g, &b);
        for (uint16_t i = 0; i < led_count; i++) {
            out_pixels[i] = (zmk_rgb_pixel_t){r, g, b};
        }
        break;
    }
    case ZMK_RGB_EFFECT_BREATHE: {
        uint32_t phase = (timestamp_ms * speed / 10u) % 360u;
        uint32_t eff_v;
        if (phase < 180u) {
            eff_v = (self->brightness * phase) / 180u;
        } else {
            eff_v = (self->brightness * (360u - phase)) / 180u;
        }
        uint8_t r = 0, g = 0, b = 0;
        zmk_rgb_hsv_to_rgb(self->hue, self->saturation, (uint8_t)eff_v, &r, &g, &b);
        for (uint16_t i = 0; i < led_count; i++) {
            out_pixels[i] = (zmk_rgb_pixel_t){r, g, b};
        }
        break;
    }
    case ZMK_RGB_EFFECT_SPECTRUM: {
        uint16_t eff_h = (uint16_t)((self->hue + (timestamp_ms * speed / 20u)) % 360u);
        uint8_t r = 0, g = 0, b = 0;
        zmk_rgb_hsv_to_rgb(eff_h, self->saturation, self->brightness, &r, &g, &b);
        for (uint16_t i = 0; i < led_count; i++) {
            out_pixels[i] = (zmk_rgb_pixel_t){r, g, b};
        }
        break;
    }
    case ZMK_RGB_EFFECT_SWIRL:
    case ZMK_RGB_EFFECT_RAINBOW: {
        uint32_t base_h = (timestamp_ms * speed / 10u) % 360u;
        for (uint16_t i = 0; i < led_count; i++) {
            uint16_t led_h = (uint16_t)((base_h + (i * 360u / led_count)) % 360u);
            uint8_t r = 0, g = 0, b = 0;
            zmk_rgb_hsv_to_rgb(led_h, self->saturation, self->brightness, &r, &g, &b);
            out_pixels[i] = (zmk_rgb_pixel_t){r, g, b};
        }
        break;
    }
    default:
        break;
    }
}
