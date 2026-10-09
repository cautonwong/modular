#ifndef ZMK_KEYMAP_H
#define ZMK_KEYMAP_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "zmk_keymap/ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZMK_KEYMAP_MAX_LAYERS 16u
#define ZMK_KEYMAP_MAX_POSITIONS 128u
#define ZMK_KEYMAP_MAX_CONDITIONAL_LAYERS 8u

#ifndef ZMK_BEHAVIOR_ID_DEFINED
#define ZMK_BEHAVIOR_ID_DEFINED
enum zmk_behavior_id {
    ZMK_BHV_NONE = 0,
    ZMK_BHV_TRANS = 1,
    ZMK_BHV_KEY_PRESS = 2,
    ZMK_BHV_MO = 3,
    ZMK_BHV_TOG = 4,
    ZMK_BHV_TO = 5,
    ZMK_BHV_HOLD_TAP = 6,
    ZMK_BHV_STICKY_KEY = 7,
    ZMK_BHV_TAP_DANCE = 8,
    ZMK_BHV_MACRO = 9,
    ZMK_BHV_MOD_MORPH = 10,
    ZMK_BHV_CAPS_WORD = 11,
    ZMK_BHV_KEY_REPEAT = 12,
    ZMK_BHV_KEY_TOGGLE = 13,
    ZMK_BHV_MOUSE_KEY = 14,
    ZMK_BHV_RGB_UG = 15,
    ZMK_BHV_BACKLIGHT = 16,
    ZMK_BHV_EXT_POWER = 17,
    ZMK_BHV_OUTPUT = 18,
    ZMK_BHV_BT = 19,
    ZMK_BHV_RESET = 20,
    ZMK_BHV_BOOTLOADER = 21,
};
#endif

typedef struct zmk_behavior_binding {
    uint16_t behavior_id;
    uint32_t param1;
    uint32_t param2;
} zmk_behavior_binding_t;

typedef struct zmk_conditional_layer {
    uint32_t trigger_mask;
    uint8_t target_layer;
} zmk_conditional_layer_t;

typedef struct zmk_keymap_app {
    edge_module_t module;
    const zmk_keymap_behavior_if_t *behavior_port;
    const zmk_keymap_event_sink_if_t *sink;

    /* Key bindings per layer per position */
    zmk_behavior_binding_t bindings[ZMK_KEYMAP_MAX_LAYERS][ZMK_KEYMAP_MAX_POSITIONS];

    /* Layer state bitmask (bit N = 1 means layer N active) */
    uint32_t active_layers_mask;
    uint8_t default_layer;
    uint8_t layer_count;
    uint8_t position_count;

    /* Tracks which layer resolved a pressed key at position to ensure correct release */
    uint8_t pressed_layer[ZMK_KEYMAP_MAX_POSITIONS];
    zmk_behavior_binding_t pressed_binding[ZMK_KEYMAP_MAX_POSITIONS];
    bool position_active[ZMK_KEYMAP_MAX_POSITIONS];

    /* Conditional layers */
    zmk_conditional_layer_t conditional_layers[ZMK_KEYMAP_MAX_CONDITIONAL_LAYERS];
    uint8_t conditional_layer_count;
} zmk_keymap_app_t;

void zmk_keymap_construct(zmk_keymap_app_t *self, uint32_t module_id, uint32_t priority,
                          const zmk_keymap_behavior_if_t *behavior_port,
                          const zmk_keymap_event_sink_if_t *sink, uint8_t layer_count,
                          uint8_t position_count);

edge_status_t zmk_keymap_init(zmk_keymap_app_t *self);
edge_status_t zmk_keymap_shutdown(zmk_keymap_app_t *self);

edge_status_t zmk_keymap_set_binding(zmk_keymap_app_t *self, uint8_t layer, uint8_t position,
                                     zmk_behavior_binding_t binding);

edge_status_t zmk_keymap_get_binding(const zmk_keymap_app_t *self, uint8_t layer, uint8_t position,
                                     zmk_behavior_binding_t *out_binding);

edge_status_t zmk_keymap_layer_activate(zmk_keymap_app_t *self, uint8_t layer);
edge_status_t zmk_keymap_layer_deactivate(zmk_keymap_app_t *self, uint8_t layer);
edge_status_t zmk_keymap_layer_toggle(zmk_keymap_app_t *self, uint8_t layer);
edge_status_t zmk_keymap_layer_to(zmk_keymap_app_t *self, uint8_t layer);
bool zmk_keymap_layer_is_active(const zmk_keymap_app_t *self, uint8_t layer);
uint32_t zmk_keymap_get_active_layers_mask(const zmk_keymap_app_t *self);

edge_status_t zmk_keymap_add_conditional_layer(zmk_keymap_app_t *self, uint32_t trigger_mask,
                                               uint8_t target_layer);

/* Position event handler from matrix */
edge_status_t zmk_keymap_on_position_state_change(zmk_keymap_app_t *self, uint32_t position,
                                                  bool pressed, uint32_t timestamp_ms);

#ifdef __cplusplus
}
#endif

#endif /* ZMK_KEYMAP_H */
