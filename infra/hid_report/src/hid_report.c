#include "hid_report/hid_report.h"

static void copy_bytes(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
}

void hid_report_builder_init(hid_report_builder_t *self) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
}

void hid_report_builder_clear(hid_report_builder_t *self) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->boot_dirty = true;
    self->nkro_dirty = true;
    self->consumer_dirty = true;
    self->mouse_dirty = true;
}

edge_status_t hid_report_press_keycode(hid_report_builder_t *self, uint8_t keycode) {
    if (self == NULL || keycode == HID_KEY_NONE) {
        return EDGE_EINVAL;
    }

    /* Check if keycode is a modifier */
    if (keycode >= HID_KEY_LCTRL && keycode <= HID_KEY_RGUI) {
        uint8_t bit = (uint8_t)(1u << (keycode - HID_KEY_LCTRL));
        self->modifiers |= bit;
        self->boot_dirty = true;
        self->nkro_dirty = true;
        return EDGE_OK;
    }

    /* Update NKRO bitmap */
    uint8_t byte_idx = keycode / 8u;
    uint8_t bit_idx = keycode % 8u;
    if (byte_idx < HID_REPORT_KEYBOARD_NKRO_BYTES) {
        self->nkro_bitmap[byte_idx] |= (uint8_t)(1u << bit_idx);
        self->nkro_dirty = true;
    }

    /* Update Boot 6KRO keys */
    for (size_t i = 0; i < HID_REPORT_KEYBOARD_6KRO_MAX; i++) {
        if (self->boot_keys[i] == keycode) {
            return EDGE_OK; /* Already present */
        }
    }
    for (size_t i = 0; i < HID_REPORT_KEYBOARD_6KRO_MAX; i++) {
        if (self->boot_keys[i] == 0u) {
            self->boot_keys[i] = keycode;
            self->boot_dirty = true;
            return EDGE_OK;
        }
    }

    return EDGE_ENOSPC; /* Boot report full, but NKRO is set */
}

edge_status_t hid_report_release_keycode(hid_report_builder_t *self, uint8_t keycode) {
    if (self == NULL || keycode == HID_KEY_NONE) {
        return EDGE_EINVAL;
    }

    /* Check if keycode is a modifier */
    if (keycode >= HID_KEY_LCTRL && keycode <= HID_KEY_RGUI) {
        uint8_t bit = (uint8_t)(1u << (keycode - HID_KEY_LCTRL));
        self->modifiers &= (uint8_t)~bit;
        self->boot_dirty = true;
        self->nkro_dirty = true;
        return EDGE_OK;
    }

    /* Update NKRO bitmap */
    uint8_t byte_idx = keycode / 8u;
    uint8_t bit_idx = keycode % 8u;
    if (byte_idx < HID_REPORT_KEYBOARD_NKRO_BYTES) {
        self->nkro_bitmap[byte_idx] &= (uint8_t) ~(1u << bit_idx);
        self->nkro_dirty = true;
    }

    /* Update Boot 6KRO keys */
    for (size_t i = 0; i < HID_REPORT_KEYBOARD_6KRO_MAX; i++) {
        if (self->boot_keys[i] == keycode) {
            self->boot_keys[i] = 0u;
            self->boot_dirty = true;
        }
    }

    return EDGE_OK;
}

edge_status_t hid_report_set_modifiers(hid_report_builder_t *self, uint8_t modifiers) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->modifiers = modifiers;
    self->boot_dirty = true;
    self->nkro_dirty = true;
    return EDGE_OK;
}

edge_status_t hid_report_add_modifiers(hid_report_builder_t *self, uint8_t modifiers) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->modifiers |= modifiers;
    self->boot_dirty = true;
    self->nkro_dirty = true;
    return EDGE_OK;
}

edge_status_t hid_report_remove_modifiers(hid_report_builder_t *self, uint8_t modifiers) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->modifiers &= (uint8_t)~modifiers;
    self->boot_dirty = true;
    self->nkro_dirty = true;
    return EDGE_OK;
}

bool hid_report_is_keycode_pressed(const hid_report_builder_t *self, uint8_t keycode) {
    if (self == NULL || keycode == HID_KEY_NONE) {
        return false;
    }
    if (keycode >= HID_KEY_LCTRL && keycode <= HID_KEY_RGUI) {
        uint8_t bit = (uint8_t)(1u << (keycode - HID_KEY_LCTRL));
        return (self->modifiers & bit) != 0u;
    }
    uint8_t byte_idx = keycode / 8u;
    uint8_t bit_idx = keycode % 8u;
    if (byte_idx < HID_REPORT_KEYBOARD_NKRO_BYTES) {
        return (self->nkro_bitmap[byte_idx] & (1u << bit_idx)) != 0u;
    }
    return false;
}

edge_status_t hid_report_press_consumer(hid_report_builder_t *self, uint16_t consumer_code) {
    if (self == NULL || consumer_code == 0u) {
        return EDGE_EINVAL;
    }
    for (size_t i = 0; i < HID_REPORT_CONSUMER_MAX; i++) {
        if (self->consumer_keys[i] == consumer_code) {
            return EDGE_OK;
        }
    }
    for (size_t i = 0; i < HID_REPORT_CONSUMER_MAX; i++) {
        if (self->consumer_keys[i] == 0u) {
            self->consumer_keys[i] = consumer_code;
            self->consumer_dirty = true;
            return EDGE_OK;
        }
    }
    return EDGE_ENOSPC;
}

edge_status_t hid_report_release_consumer(hid_report_builder_t *self, uint16_t consumer_code) {
    if (self == NULL || consumer_code == 0u) {
        return EDGE_EINVAL;
    }
    for (size_t i = 0; i < HID_REPORT_CONSUMER_MAX; i++) {
        if (self->consumer_keys[i] == consumer_code) {
            self->consumer_keys[i] = 0u;
            self->consumer_dirty = true;
        }
    }
    return EDGE_OK;
}

bool hid_report_is_consumer_pressed(const hid_report_builder_t *self, uint16_t consumer_code) {
    if (self == NULL || consumer_code == 0u) {
        return false;
    }
    for (size_t i = 0; i < HID_REPORT_CONSUMER_MAX; i++) {
        if (self->consumer_keys[i] == consumer_code) {
            return true;
        }
    }
    return false;
}

edge_status_t hid_report_press_mouse_button(hid_report_builder_t *self, uint8_t button) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->mouse_report.buttons |= button;
    self->mouse_dirty = true;
    return EDGE_OK;
}

edge_status_t hid_report_release_mouse_button(hid_report_builder_t *self, uint8_t button) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->mouse_report.buttons &= (uint8_t)~button;
    self->mouse_dirty = true;
    return EDGE_OK;
}

edge_status_t hid_report_mouse_move(hid_report_builder_t *self, int8_t dx, int8_t dy, int8_t wheel,
                                    int8_t pan) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->mouse_report.x = dx;
    self->mouse_report.y = dy;
    self->mouse_report.wheel = wheel;
    self->mouse_report.pan = pan;
    self->mouse_dirty = true;
    return EDGE_OK;
}

size_t hid_report_serialize_boot(const hid_report_builder_t *self, uint8_t *out_buf,
                                 size_t max_len) {
    if (self == NULL || out_buf == NULL || max_len < 8u) {
        return 0u;
    }
    out_buf[0] = self->modifiers;
    out_buf[1] = 0u; /* Reserved */
    copy_bytes(&out_buf[2], self->boot_keys, HID_REPORT_KEYBOARD_6KRO_MAX);
    return 8u;
}

size_t hid_report_serialize_nkro(const hid_report_builder_t *self, uint8_t *out_buf,
                                 size_t max_len) {
    if (self == NULL || out_buf == NULL || max_len < (1u + HID_REPORT_KEYBOARD_NKRO_BYTES)) {
        return 0u;
    }
    out_buf[0] = self->modifiers;
    copy_bytes(&out_buf[1], self->nkro_bitmap, HID_REPORT_KEYBOARD_NKRO_BYTES);
    return 1u + HID_REPORT_KEYBOARD_NKRO_BYTES;
}

size_t hid_report_serialize_consumer(const hid_report_builder_t *self, uint8_t *out_buf,
                                     size_t max_len) {
    if (self == NULL || out_buf == NULL || max_len < (HID_REPORT_CONSUMER_MAX * 2u)) {
        return 0u;
    }
    for (size_t i = 0; i < HID_REPORT_CONSUMER_MAX; i++) {
        out_buf[2u * i] = (uint8_t)(self->consumer_keys[i] & 0xFFu);
        out_buf[2u * i + 1u] = (uint8_t)((self->consumer_keys[i] >> 8u) & 0xFFu);
    }
    return HID_REPORT_CONSUMER_MAX * 2u;
}

size_t hid_report_serialize_mouse(const hid_report_builder_t *self, uint8_t *out_buf,
                                  size_t max_len) {
    if (self == NULL || out_buf == NULL || max_len < 5u) {
        return 0u;
    }
    out_buf[0] = self->mouse_report.buttons;
    out_buf[1] = (uint8_t)self->mouse_report.x;
    out_buf[2] = (uint8_t)self->mouse_report.y;
    out_buf[3] = (uint8_t)self->mouse_report.wheel;
    out_buf[4] = (uint8_t)self->mouse_report.pan;
    return 5u;
}
