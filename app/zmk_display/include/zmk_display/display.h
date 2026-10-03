#ifndef ZMK_DISPLAY_H
#define ZMK_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zmk_display_hw_if {
    void *self;
    edge_status_t (*draw_screen)(void *self, const char *line1, const char *line2,
                                 const char *line3, const char *line4);
} zmk_display_hw_if_t;

typedef struct zmk_display_state {
    uint8_t battery_level;
    bool usb_powered;
    bool ble_connected;
    uint8_t ble_profile_index;
    uint8_t active_layer;
    char layer_name[16];
    uint16_t current_wpm;
    bool is_sleeping;
} zmk_display_state_t;

typedef struct zmk_display_app {
    edge_module_t module;
    zmk_display_hw_if_t hw;
    zmk_display_state_t state;
    bool screen_dirty;
} zmk_display_app_t;

void zmk_display_construct(zmk_display_app_t *app, uint32_t module_id, uint8_t priority,
                           const zmk_display_hw_if_t *hw);

edge_status_t zmk_display_init(zmk_display_app_t *app);

edge_status_t zmk_display_update(zmk_display_app_t *app);

edge_status_t zmk_display_on_battery_state(zmk_display_app_t *app, uint8_t level, bool is_usb);

edge_status_t zmk_display_on_layer_state(zmk_display_app_t *app, uint8_t layer,
                                         const char *layer_name);

edge_status_t zmk_display_on_endpoint_state(zmk_display_app_t *app, uint8_t profile_index,
                                            bool connected);

edge_status_t zmk_display_on_wpm_state(zmk_display_app_t *app, uint16_t wpm);

edge_status_t zmk_display_on_activity_state(zmk_display_app_t *app, uint8_t activity_state);

const zmk_display_state_t *zmk_display_get_state(const zmk_display_app_t *app);

#ifdef __cplusplus
}
#endif

#endif
