#include "zmk_studio/studio.h"

static void copy_bytes(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
}

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

static edge_status_t send_response(zmk_studio_app_t *app, const uint8_t *resp_payload,
                                   size_t resp_len) {
    if (app->transport.send_frame == NULL) {
        return EDGE_OK;
    }
    uint8_t frame_buf[ZMK_STUDIO_MAX_FRAME_LEN];
    size_t frame_len =
        zmk_studio_frame_encode(resp_payload, resp_len, frame_buf, sizeof(frame_buf));
    if (frame_len == 0) {
        return EDGE_ENOSPC;
    }
    return app->transport.send_frame(app->transport.self, frame_buf, frame_len);
}

static edge_status_t handle_core_request(zmk_studio_app_t *app, const uint8_t *req, size_t len) {
    if (len < 2) {
        return EDGE_EINVAL;
    }
    uint8_t cmd = req[1];
    uint8_t resp[64];
    size_t resp_len = 0;
    resp[0] = ZMK_STUDIO_SUBSYS_CORE;
    resp[1] = cmd;

    switch (cmd) {
    case ZMK_STUDIO_CORE_CMD_GET_DEVICE_INFO: {
        // [subsys][cmd][status:0][name_len][name...][serial:4]
        resp[2] = 0; // OK
        size_t name_len = str_len(app->device_name);
        resp[3] = (uint8_t)name_len;
        copy_bytes(&resp[4], app->device_name, name_len);
        size_t offset = 4 + name_len;
        copy_bytes(&resp[offset], &app->serial_number, 4);
        resp_len = offset + 4;
        break;
    }
    case ZMK_STUDIO_CORE_CMD_GET_LOCK_STATE: {
        resp[2] = 0; // OK
        resp[3] = app->unlocked ? 1 : 0;
        resp_len = 4;
        break;
    }
    case ZMK_STUDIO_CORE_CMD_UNLOCK_DEVICE: {
        app->unlocked = true;
        resp[2] = 0;
        resp[3] = 1;
        resp_len = 4;
        break;
    }
    case ZMK_STUDIO_CORE_CMD_LOCK_DEVICE: {
        app->unlocked = false;
        resp[2] = 0;
        resp[3] = 0;
        resp_len = 4;
        break;
    }
    case ZMK_STUDIO_CORE_CMD_RESET_SETTINGS: {
        if (!app->unlocked) {
            resp[2] = 1; // Locked error
            resp_len = 3;
            break;
        }
        if (app->keymap.discard_changes != NULL) {
            app->keymap.discard_changes(app->keymap.self);
        }
        app->unsaved_changes = false;
        resp[2] = 0;
        resp_len = 3;
        break;
    }
    default:
        resp[2] = 2; // Unknown command
        resp_len = 3;
        break;
    }

    return send_response(app, resp, resp_len);
}

static edge_status_t handle_keymap_request(zmk_studio_app_t *app, const uint8_t *req, size_t len) {
    if (len < 2) {
        return EDGE_EINVAL;
    }
    uint8_t cmd = req[1];
    uint8_t resp[64];
    size_t resp_len = 0;
    resp[0] = ZMK_STUDIO_SUBSYS_KEYMAP;
    resp[1] = cmd;

    switch (cmd) {
    case ZMK_STUDIO_KEYMAP_CMD_GET_KEYMAP: {
        // Request: [subsys][cmd][layer][position]
        if (len < 4) {
            return EDGE_EINVAL;
        }
        uint8_t layer = req[2];
        uint32_t pos = req[3];
        uint16_t bhv = 0;
        uint32_t p1 = 0, p2 = 0;
        if (app->keymap.get_layer_binding != NULL) {
            app->keymap.get_layer_binding(app->keymap.self, layer, pos, &bhv, &p1, &p2);
        }
        // Response: [subsys][cmd][status:0][layer][pos][bhv:2][p1:4][p2:4]
        resp[2] = 0;
        resp[3] = layer;
        resp[4] = (uint8_t)pos;
        copy_bytes(&resp[5], &bhv, 2);
        copy_bytes(&resp[7], &p1, 4);
        copy_bytes(&resp[11], &p2, 4);
        resp_len = 15;
        break;
    }
    case ZMK_STUDIO_KEYMAP_CMD_SET_LAYER_BINDING: {
        // Request: [subsys][cmd][layer][pos][bhv:2][p1:4][p2:4]
        if (!app->unlocked) {
            resp[2] = 1; // Locked error
            resp_len = 3;
            break;
        }
        if (len < 15) {
            return EDGE_EINVAL;
        }
        uint8_t layer = req[2];
        uint32_t pos = req[3];
        uint16_t bhv = 0;
        uint32_t p1 = 0, p2 = 0;
        copy_bytes(&bhv, &req[4], 2);
        copy_bytes(&p1, &req[6], 4);
        copy_bytes(&p2, &req[10], 4);

        if (app->keymap.set_layer_binding != NULL) {
            edge_status_t rc =
                app->keymap.set_layer_binding(app->keymap.self, layer, pos, bhv, p1, p2);
            resp[2] = (rc == EDGE_OK) ? 0 : 2;
            if (rc == EDGE_OK) {
                app->unsaved_changes = true;
            }
        } else {
            resp[2] = 2;
        }
        resp_len = 3;
        break;
    }
    case ZMK_STUDIO_KEYMAP_CMD_CHECK_UNSAVED: {
        resp[2] = 0;
        resp[3] = app->unsaved_changes ? 1 : 0;
        resp_len = 4;
        break;
    }
    case ZMK_STUDIO_KEYMAP_CMD_SAVE_CHANGES: {
        if (!app->unlocked) {
            resp[2] = 1;
            resp_len = 3;
            break;
        }
        if (app->keymap.save_changes != NULL) {
            app->keymap.save_changes(app->keymap.self);
        }
        app->unsaved_changes = false;
        resp[2] = 0;
        resp_len = 3;
        break;
    }
    case ZMK_STUDIO_KEYMAP_CMD_DISCARD_CHANGES: {
        if (!app->unlocked) {
            resp[2] = 1;
            resp_len = 3;
            break;
        }
        if (app->keymap.discard_changes != NULL) {
            app->keymap.discard_changes(app->keymap.self);
        }
        app->unsaved_changes = false;
        resp[2] = 0;
        resp_len = 3;
        break;
    }
    default:
        resp[2] = 2;
        resp_len = 3;
        break;
    }

    return send_response(app, resp, resp_len);
}

static edge_status_t app_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t app_power_off(edge_module_t *module) {
    zmk_studio_app_t *app = (zmk_studio_app_t *)edge_module_data(module);
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->unlocked = false;
    app->rx_state = ZMK_STUDIO_STATE_IDLE;
    app->rx_len = 0;
    return EDGE_OK;
}

void zmk_studio_construct(zmk_studio_app_t *app, uint32_t module_id, uint8_t priority,
                          const zmk_studio_transport_if_t *transport,
                          const zmk_studio_keymap_if_t *keymap) {
    if (app == NULL) {
        return;
    }
    *app = (__typeof__(*app)){0};
    app->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 10u,
        .poll = app_poll,
        .power_off = app_power_off,
        .private_data = app,
    };

    if (transport != NULL) {
        app->transport = *transport;
    }
    if (keymap != NULL) {
        app->keymap = *keymap;
    }

    const char *default_name = "ZMK Modular Keyboard";
    size_t i = 0;
    while (i + 1 < sizeof(app->device_name) && default_name[i] != '\0') {
        app->device_name[i] = default_name[i];
        i++;
    }
    app->device_name[i] = '\0';

    app->serial_number = 0x12345678u;
    app->unlocked = false;
    app->unsaved_changes = false;
    app->rx_state = ZMK_STUDIO_STATE_IDLE;
    app->rx_len = 0;
}

edge_status_t zmk_studio_init(zmk_studio_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->unlocked = false;
    app->rx_state = ZMK_STUDIO_STATE_IDLE;
    app->rx_len = 0;
    return EDGE_OK;
}

edge_status_t zmk_studio_process_request(zmk_studio_app_t *app, const uint8_t *payload,
                                         size_t len) {
    if (app == NULL || payload == NULL || len < 1) {
        return EDGE_EINVAL;
    }

    uint8_t subsys = payload[0];
    switch (subsys) {
    case ZMK_STUDIO_SUBSYS_CORE:
        return handle_core_request(app, payload, len);
    case ZMK_STUDIO_SUBSYS_KEYMAP:
        return handle_keymap_request(app, payload, len);
    default:
        return EDGE_ENOTSUP;
    }
}

edge_status_t zmk_studio_rx_byte(zmk_studio_app_t *app, uint8_t byte) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }

    uint8_t data_byte = 0;
    bool is_data = zmk_studio_framing_process_byte(&app->rx_state, byte, &data_byte);

    if (is_data) {
        if (app->rx_len < sizeof(app->rx_buf)) {
            app->rx_buf[app->rx_len++] = data_byte;
        } else {
            app->rx_state = ZMK_STUDIO_STATE_ERR;
            app->rx_len = 0;
        }
    } else if (app->rx_state == ZMK_STUDIO_STATE_EOF) {
        if (app->rx_len > 0) {
            zmk_studio_process_request(app, app->rx_buf, app->rx_len);
        }
        app->rx_len = 0;
        app->rx_state = ZMK_STUDIO_STATE_IDLE;
    }

    return EDGE_OK;
}

bool zmk_studio_is_unlocked(const zmk_studio_app_t *app) {
    return (app != NULL) ? app->unlocked : false;
}
