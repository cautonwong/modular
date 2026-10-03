#ifndef ZMK_BOOT_H
#define ZMK_BOOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZMK_BOOT_DBL_TAP_DEFAULT_TIMEOUT_MS 500

typedef enum {
    ZMK_BOOT_TARGET_NORMAL = 0,
    ZMK_BOOT_TARGET_BOOTLOADER = 1,
    ZMK_BOOT_TARGET_UF2 = 2,
    ZMK_BOOT_TARGET_SETTINGS_RESET = 3,
} zmk_boot_target_t;

typedef struct zmk_boot_hw_if {
    void *self;
    edge_status_t (*reboot_to_target)(void *self, zmk_boot_target_t target);
} zmk_boot_hw_if_t;

typedef struct zmk_boot_app {
    edge_module_t module;
    zmk_boot_hw_if_t hw;
    uint32_t dbl_tap_timeout_ms;
    uint32_t last_reset_press_ms;
    uint8_t tap_count;
} zmk_boot_app_t;

void zmk_boot_construct(zmk_boot_app_t *app, uint32_t module_id, uint8_t priority,
                        const zmk_boot_hw_if_t *hw);

edge_status_t zmk_boot_init(zmk_boot_app_t *app);

edge_status_t zmk_boot_reset_pressed(zmk_boot_app_t *app, uint32_t timestamp_ms);

edge_status_t zmk_boot_request_reboot(zmk_boot_app_t *app, zmk_boot_target_t target);

#ifdef __cplusplus
}
#endif

#endif
