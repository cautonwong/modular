#ifndef ZMK_POINTING_H
#define ZMK_POINTING_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "zmk_pointing/ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZMK_MOUSE_BTN_LEFT (1u << 0)
#define ZMK_MOUSE_BTN_RIGHT (1u << 1)
#define ZMK_MOUSE_BTN_MIDDLE (1u << 2)
#define ZMK_MOUSE_BTN_4 (1u << 3)
#define ZMK_MOUSE_BTN_5 (1u << 4)

typedef struct zmk_pointing_scaler {
    int16_t multiplier;
    int16_t divisor;
} zmk_pointing_scaler_t;

typedef struct zmk_pointing_app {
    edge_module_t module;
    const zmk_pointing_hid_if_t *hid;

    zmk_pointing_scaler_t x_scaler;
    zmk_pointing_scaler_t y_scaler;
    zmk_pointing_scaler_t scroll_scaler;

    uint8_t current_buttons;
    int32_t total_dx;
    int32_t total_dy;
} zmk_pointing_app_t;

void zmk_pointing_construct(zmk_pointing_app_t *self, uint32_t module_id, uint32_t priority,
                            const zmk_pointing_hid_if_t *hid);

edge_status_t zmk_pointing_init(zmk_pointing_app_t *self);
edge_status_t zmk_pointing_shutdown(zmk_pointing_app_t *self);

void zmk_pointing_set_scaler(zmk_pointing_app_t *self, int16_t mul, int16_t div);

edge_status_t zmk_pointing_motion(zmk_pointing_app_t *self, int16_t dx, int16_t dy, int8_t v_scroll,
                                  int8_t h_scroll);

edge_status_t zmk_pointing_button_press(zmk_pointing_app_t *self, uint8_t button_mask);
edge_status_t zmk_pointing_button_release(zmk_pointing_app_t *self, uint8_t button_mask);

#ifdef __cplusplus
}
#endif

#endif /* ZMK_POINTING_H */
