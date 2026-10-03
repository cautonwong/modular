#ifndef ZMK_BEHAVIOR_PORTS_H
#define ZMK_BEHAVIOR_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zmk_behavior_hid_if {
    void *self;
    edge_status_t (*press_key)(void *self, uint8_t keycode, uint8_t modifiers);
    edge_status_t (*release_key)(void *self, uint8_t keycode, uint8_t modifiers);
    edge_status_t (*press_consumer_key)(void *self, uint16_t code);
    edge_status_t (*release_consumer_key)(void *self, uint16_t code);
    edge_status_t (*press_mouse_button)(void *self, uint8_t button);
    edge_status_t (*release_mouse_button)(void *self, uint8_t button);
} zmk_behavior_hid_if_t;

typedef struct zmk_behavior_keymap_if {
    void *self;
    edge_status_t (*layer_activate)(void *self, uint8_t layer);
    edge_status_t (*layer_deactivate)(void *self, uint8_t layer);
    edge_status_t (*layer_toggle)(void *self, uint8_t layer);
    edge_status_t (*layer_to)(void *self, uint8_t layer);
} zmk_behavior_keymap_if_t;

#ifdef __cplusplus
}
#endif

#endif /* ZMK_BEHAVIOR_PORTS_H */
