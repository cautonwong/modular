#include "zmk_display/display.h"

enum {
    ZMK_ACTIVITY_ACTIVE = 0,
    ZMK_ACTIVITY_IDLE = 1,
    ZMK_ACTIVITY_SLEEP = 2,
};

static size_t str_len(const char *s) {
    if (s == NULL) {
        return 0;
    }
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

static void str_copy(char *dest, size_t dest_size, const char *src) {
    if (dest == NULL || dest_size == 0) {
        return;
    }
    if (src == NULL) {
        dest[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < dest_size && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static void str_append(char *dest, size_t dest_size, const char *src) {
    if (dest == NULL || src == NULL || dest_size == 0) {
        return;
    }
    size_t len = str_len(dest);
    if (len >= dest_size - 1) {
        return;
    }
    size_t i = 0;
    while (len + i + 1 < dest_size && src[i] != '\0') {
        dest[len + i] = src[i];
        i++;
    }
    dest[len + i] = '\0';
}

static void append_u32(char *dest, size_t dest_size, uint32_t val) {
    char rev[12];
    int r = 0;
    if (val == 0) {
        str_append(dest, dest_size, "0");
        return;
    }
    while (val > 0 && r < 11) {
        rev[r++] = (char)('0' + (val % 10));
        val /= 10;
    }
    char tmp[12];
    int idx = 0;
    while (r > 0) {
        tmp[idx++] = rev[--r];
    }
    tmp[idx] = '\0';
    str_append(dest, dest_size, tmp);
}

static edge_status_t app_poll(edge_module_t *module) {
    zmk_display_app_t *app = (zmk_display_app_t *)edge_module_data(module);
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    if (app->screen_dirty) {
        return zmk_display_update(app);
    }
    return EDGE_OK;
}

static edge_status_t app_power_off(edge_module_t *module) {
    zmk_display_app_t *app = (zmk_display_app_t *)edge_module_data(module);
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->state.is_sleeping = true;
    app->screen_dirty = true;
    return zmk_display_update(app);
}

void zmk_display_construct(zmk_display_app_t *app, uint32_t module_id, uint8_t priority,
                           const zmk_display_hw_if_t *hw) {
    if (app == NULL) {
        return;
    }
    *app = (__typeof__(*app)){0};
    app->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 50u,
        .poll = app_poll,
        .power_off = app_power_off,
        .private_data = app,
    };

    if (hw != NULL) {
        app->hw = *hw;
    }

    app->state.battery_level = 100;
    app->state.usb_powered = false;
    app->state.ble_connected = true;
    app->state.ble_profile_index = 0;
    app->state.active_layer = 0;
    str_copy(app->state.layer_name, sizeof(app->state.layer_name), "DEF");
    app->state.current_wpm = 0;
    app->state.is_sleeping = false;
    app->screen_dirty = true;
}

edge_status_t zmk_display_init(zmk_display_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->screen_dirty = true;
    return zmk_display_update(app);
}

edge_status_t zmk_display_update(zmk_display_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    if (app->hw.draw_screen == NULL) {
        app->screen_dirty = false;
        return EDGE_OK;
    }

    char line1[32] = {0};
    char line2[32] = {0};
    char line3[32] = {0};
    char line4[32] = {0};

    if (app->state.is_sleeping) {
        str_copy(line1, sizeof(line1), "ZMK KEYBOARD");
        str_copy(line2, sizeof(line2), "   [SLEEP]   ");
        str_copy(line3, sizeof(line3), "             ");
        str_copy(line4, sizeof(line4), "             ");
    } else {
        // Line 1: Output / BLE profile
        str_copy(line1, sizeof(line1), "OUT: ");
        if (app->state.usb_powered) {
            str_append(line1, sizeof(line1), "USB");
        } else if (app->state.ble_connected) {
            str_append(line1, sizeof(line1), "BLE-CONN");
        } else {
            str_append(line1, sizeof(line1), "BLE-DISC");
        }
        str_append(line1, sizeof(line1), " (PRF ");
        append_u32(line1, sizeof(line1), (uint32_t)(app->state.ble_profile_index + 1));
        str_append(line1, sizeof(line1), ")");

        // Line 2: Active Layer
        str_copy(line2, sizeof(line2), "LAYER: ");
        str_append(line2, sizeof(line2), app->state.layer_name);
        str_append(line2, sizeof(line2), " [");
        append_u32(line2, sizeof(line2), app->state.active_layer);
        str_append(line2, sizeof(line2), "]");

        // Line 3: Battery
        str_copy(line3, sizeof(line3), "BAT: ");
        if (app->state.usb_powered) {
            str_append(line3, sizeof(line3), "CHG (");
            append_u32(line3, sizeof(line3), app->state.battery_level);
            str_append(line3, sizeof(line3), "%)");
        } else {
            append_u32(line3, sizeof(line3), app->state.battery_level);
            str_append(line3, sizeof(line3), "%");
        }

        // Line 4: WPM
        str_copy(line4, sizeof(line4), "WPM: ");
        append_u32(line4, sizeof(line4), app->state.current_wpm);
    }

    edge_status_t rc = app->hw.draw_screen(app->hw.self, line1, line2, line3, line4);
    if (rc == EDGE_OK) {
        app->screen_dirty = false;
    }
    return rc;
}

edge_status_t zmk_display_on_battery_state(zmk_display_app_t *app, uint8_t level, bool is_usb) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->state.battery_level = level;
    app->state.usb_powered = is_usb;
    app->screen_dirty = true;
    return EDGE_OK;
}

edge_status_t zmk_display_on_layer_state(zmk_display_app_t *app, uint8_t layer,
                                         const char *layer_name) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->state.active_layer = layer;
    if (layer_name != NULL) {
        str_copy(app->state.layer_name, sizeof(app->state.layer_name), layer_name);
    } else {
        str_copy(app->state.layer_name, sizeof(app->state.layer_name), "L");
        append_u32(app->state.layer_name, sizeof(app->state.layer_name), layer);
    }
    app->screen_dirty = true;
    return EDGE_OK;
}

edge_status_t zmk_display_on_endpoint_state(zmk_display_app_t *app, uint8_t profile_index,
                                            bool connected) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->state.ble_profile_index = profile_index;
    app->state.ble_connected = connected;
    app->screen_dirty = true;
    return EDGE_OK;
}

edge_status_t zmk_display_on_wpm_state(zmk_display_app_t *app, uint16_t wpm) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->state.current_wpm = wpm;
    app->screen_dirty = true;
    return EDGE_OK;
}

edge_status_t zmk_display_on_activity_state(zmk_display_app_t *app, uint8_t activity_state) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->state.is_sleeping = (activity_state == ZMK_ACTIVITY_SLEEP);
    app->screen_dirty = true;
    return EDGE_OK;
}

const zmk_display_state_t *zmk_display_get_state(const zmk_display_app_t *app) {
    return (app != NULL) ? &app->state : NULL;
}
