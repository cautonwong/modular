#include "zmk_boot/boot.h"

static edge_status_t app_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t app_power_off(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

void zmk_boot_construct(zmk_boot_app_t *app, uint32_t module_id, uint8_t priority,
                        const zmk_boot_hw_if_t *hw) {
    if (app == NULL) {
        return;
    }
    *app = (__typeof__(*app)){0};
    app->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 1u,
        .poll = app_poll,
        .power_off = app_power_off,
        .private_data = app,
    };

    if (hw != NULL) {
        app->hw = *hw;
    }

    app->dbl_tap_timeout_ms = ZMK_BOOT_DBL_TAP_DEFAULT_TIMEOUT_MS;
    app->last_reset_press_ms = 0;
    app->tap_count = 0;
}

edge_status_t zmk_boot_init(zmk_boot_app_t *app) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    app->tap_count = 0;
    app->last_reset_press_ms = 0;
    return EDGE_OK;
}

edge_status_t zmk_boot_reset_pressed(zmk_boot_app_t *app, uint32_t timestamp_ms) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }

    if (app->tap_count > 0 &&
        (int32_t)(timestamp_ms - app->last_reset_press_ms) <= (int32_t)app->dbl_tap_timeout_ms) {
        // Double tap confirmed! Jump to DFU Bootloader
        app->tap_count = 0;
        return zmk_boot_request_reboot(app, ZMK_BOOT_TARGET_BOOTLOADER);
    }

    app->tap_count = 1;
    app->last_reset_press_ms = timestamp_ms;
    return EDGE_OK;
}

edge_status_t zmk_boot_request_reboot(zmk_boot_app_t *app, zmk_boot_target_t target) {
    if (app == NULL) {
        return EDGE_EINVAL;
    }
    if (app->hw.reboot_to_target != NULL) {
        return app->hw.reboot_to_target(app->hw.self, target);
    }
    return EDGE_OK;
}
