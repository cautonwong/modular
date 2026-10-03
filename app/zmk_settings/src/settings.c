#include "zmk_settings/settings.h"

static uint32_t compute_checksum(const zmk_settings_record_t *rec) {
    const uint8_t *bytes = (const uint8_t *)rec;
    size_t len = offsetof(zmk_settings_record_t, checksum);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= bytes[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

static void set_defaults(zmk_settings_record_t *rec) {
    *rec = (__typeof__(*rec)){0};
    rec->magic = ZMK_SETTINGS_MAGIC;
    rec->version = ZMK_SETTINGS_VERSION;
    rec->selected_endpoint = 0; // BLE
    rec->selected_ble_profile = 0;
    rec->rgb_on = 1;
    rec->rgb_effect = 0; // Solid
    rec->rgb_hue = 0;
    rec->rgb_sat = 255;
    rec->rgb_val = 128;
    rec->rgb_speed = 1;
    rec->ext_power_on = 1;
    rec->pointing_cpi_factor = 1;
    rec->key_override_count = 0;
    rec->checksum = compute_checksum(rec);
}

static edge_status_t app_poll(edge_module_t *module) {
    zmk_settings_app_t *app = (zmk_settings_app_t *)edge_module_data(module);
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    if (app->dirty) {
        return zmk_settings_save(app);
    }
    return EDGE_OK;
}

static edge_status_t app_power_off(edge_module_t *module) {
    zmk_settings_app_t *app = (zmk_settings_app_t *)edge_module_data(module);
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    if (app->dirty) {
        return zmk_settings_save(app);
    }
    return EDGE_OK;
}

void zmk_settings_construct(zmk_settings_app_t *app, uint32_t module_id, uint8_t priority,
                            const zmk_settings_storage_if_t *storage) {
    if (app == NULL) {
        return;
    }
    *app = (__typeof__(*app)){0};
    app->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 100u,
        .poll = app_poll,
        .power_off = app_power_off,
        .private_data = app,
    };

    if (storage != NULL) {
        app->storage = *storage;
    }
    set_defaults(&app->data);
}

edge_status_t zmk_settings_init(zmk_settings_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    return zmk_settings_load(app);
}

edge_status_t zmk_settings_load(zmk_settings_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    if (app->storage.read == NULL) {
        set_defaults(&app->data);
        app->dirty = false;
        return EDGE_OK;
    }

    zmk_settings_record_t temp;
    edge_status_t rc = app->storage.read(app->storage.self, 0, (uint8_t *)&temp, sizeof(temp));
    if (rc != EDGE_OK || temp.magic != ZMK_SETTINGS_MAGIC || temp.version != ZMK_SETTINGS_VERSION ||
        compute_checksum(&temp) != temp.checksum) {
        set_defaults(&app->data);
        app->dirty = true;
        return EDGE_OK;
    }

    app->data = temp;
    app->dirty = false;
    return EDGE_OK;
}

edge_status_t zmk_settings_save(zmk_settings_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    if (app->storage.write == NULL) {
        app->dirty = false;
        return EDGE_OK;
    }

    app->data.magic = ZMK_SETTINGS_MAGIC;
    app->data.version = ZMK_SETTINGS_VERSION;
    app->data.checksum = compute_checksum(&app->data);

    if (app->storage.erase != NULL) {
        edge_status_t erase_rc = app->storage.erase(app->storage.self, 0, sizeof(app->data));
        if (erase_rc != EDGE_OK) {
            return erase_rc;
        }
    }

    edge_status_t rc =
        app->storage.write(app->storage.self, 0, (const uint8_t *)&app->data, sizeof(app->data));
    if (rc == EDGE_OK) {
        app->dirty = false;
    }
    return rc;
}

edge_status_t zmk_settings_reset(zmk_settings_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    set_defaults(&app->data);
    app->dirty = true;
    return zmk_settings_save(app);
}

bool zmk_settings_is_dirty(const zmk_settings_app_t *app) {
    return (app != NULL) ? app->dirty : false;
}

zmk_settings_record_t *zmk_settings_get_record(zmk_settings_app_t *app) {
    if (app == NULL) {
        return NULL;
    }
    app->dirty = true;
    return &app->data;
}
