#ifndef ZMK_SETTINGS_H
#define ZMK_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZMK_SETTINGS_MAGIC 0x5A4D4B31u /* "ZMK1" */
#define ZMK_SETTINGS_VERSION 1
#define ZMK_SETTINGS_MAX_KEY_OVERRIDES 16

typedef struct zmk_settings_storage_if {
    void *self;
    edge_status_t (*read)(void *self, uint32_t offset, uint8_t *buf, size_t len);
    edge_status_t (*write)(void *self, uint32_t offset, const uint8_t *buf, size_t len);
    edge_status_t (*erase)(void *self, uint32_t offset, size_t len);
} zmk_settings_storage_if_t;

typedef struct zmk_settings_record {
    uint32_t magic;
    uint16_t version;
    uint8_t selected_endpoint;
    uint8_t selected_ble_profile;
    uint8_t rgb_on;
    uint8_t rgb_effect;
    uint8_t rgb_hue;
    uint8_t rgb_sat;
    uint8_t rgb_val;
    uint8_t rgb_speed;
    uint8_t ext_power_on;
    uint8_t pointing_cpi_factor;
    uint16_t key_override_count;
    struct {
        uint32_t position;
        uint32_t layer;
        uint16_t behavior_id;
        uint32_t param1;
        uint32_t param2;
    } key_overrides[ZMK_SETTINGS_MAX_KEY_OVERRIDES];
    uint32_t checksum;
} zmk_settings_record_t;

typedef struct zmk_settings_app {
    edge_module_t module;
    zmk_settings_storage_if_t storage;
    zmk_settings_record_t data;
    bool dirty;
} zmk_settings_app_t;

void zmk_settings_construct(zmk_settings_app_t *app, uint32_t module_id, uint8_t priority,
                            const zmk_settings_storage_if_t *storage);

edge_status_t zmk_settings_init(zmk_settings_app_t *app);

edge_status_t zmk_settings_load(zmk_settings_app_t *app);

edge_status_t zmk_settings_save(zmk_settings_app_t *app);

edge_status_t zmk_settings_reset(zmk_settings_app_t *app);

bool zmk_settings_is_dirty(const zmk_settings_app_t *app);

zmk_settings_record_t *zmk_settings_get_record(zmk_settings_app_t *app);

#ifdef __cplusplus
}
#endif

#endif
