#ifndef ZMK_BEHAVIOR_H
#define ZMK_BEHAVIOR_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "zmk_behavior/ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZMK_MAX_TAP_DANCE 16u
#define ZMK_MAX_HOLD_TAP 16u
#define ZMK_MAX_MOD_MORPH 16u
#define ZMK_MAX_MACROS 16u
#define ZMK_MAX_MACRO_STEPS 32u

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

enum zmk_ht_flavor {
    ZMK_HT_HOLD_PREFERRED = 0,
    ZMK_HT_BALANCED = 1,
    ZMK_HT_TAP_PREFERRED = 2,
    ZMK_HT_TAP_UNLESS_INTERRUPTED = 3,
};

typedef struct zmk_ht_config {
    uint8_t flavor;
    uint16_t tapping_term_ms;
    uint16_t quick_tap_ms;
    bool retro_tap;
} zmk_ht_config_t;

typedef struct zmk_ht_instance {
    zmk_ht_config_t config;
    uint32_t hold_behavior_id;
    uint32_t hold_param1;
    uint32_t tap_behavior_id;
    uint32_t tap_param1;

    bool active;
    bool is_held;
    bool is_tapped;
    bool interrupted;
    bool other_key_released;
    bool has_previous_tap;
    uint32_t press_time_ms;
    uint32_t last_tap_time_ms;
} zmk_ht_instance_t;

typedef struct zmk_tap_dance_binding {
    uint16_t behavior_id;
    uint32_t param1;
} zmk_tap_dance_binding_t;

typedef struct zmk_tap_dance_config {
    uint16_t tapping_term_ms;
    zmk_tap_dance_binding_t bindings[4]; /* Single tap, double tap, triple tap, etc. */
    uint8_t binding_count;
} zmk_tap_dance_config_t;

typedef struct zmk_tap_dance_instance {
    zmk_tap_dance_config_t config;
    uint8_t tap_count;
    bool active;
    uint32_t last_tap_time_ms;
} zmk_tap_dance_instance_t;

typedef struct zmk_mod_morph_config {
    uint8_t default_keycode;
    uint8_t default_mods;
    uint8_t morphed_keycode;
    uint8_t morphed_mods;
    uint8_t trigger_mods_mask; /* Morphs when these modifiers are active */
} zmk_mod_morph_config_t;

enum zmk_macro_action {
    ZMK_MACRO_ACTION_PRESS = 0,
    ZMK_MACRO_ACTION_RELEASE = 1,
    ZMK_MACRO_ACTION_TAP = 2,
    ZMK_MACRO_ACTION_WAIT = 3,
};

typedef struct zmk_macro_step {
    uint8_t action;
    uint8_t keycode;
    uint8_t modifiers;
    uint16_t wait_ms;
} zmk_macro_step_t;

typedef struct zmk_macro_config {
    zmk_macro_step_t steps[ZMK_MAX_MACRO_STEPS];
    uint8_t step_count;
    uint16_t default_wait_ms;
} zmk_macro_config_t;

typedef struct zmk_sticky_key_state {
    bool active;
    uint8_t keycode;
    uint8_t modifiers;
    uint8_t layer;
    bool is_layer;
    uint32_t activate_time_ms;
    uint16_t timeout_ms;
} zmk_sticky_key_state_t;

typedef struct zmk_caps_word_state {
    bool active;
    uint8_t shifted_mods;
} zmk_caps_word_state_t;

typedef struct zmk_behavior_app {
    edge_module_t module;
    const zmk_behavior_hid_if_t *hid;
    const zmk_behavior_keymap_if_t *keymap;

    /* Behavior subsystems */
    zmk_ht_instance_t hold_taps[ZMK_MAX_HOLD_TAP];
    uint8_t hold_tap_count;

    zmk_tap_dance_instance_t tap_dances[ZMK_MAX_TAP_DANCE];
    uint8_t tap_dance_count;

    zmk_mod_morph_config_t mod_morphs[ZMK_MAX_MOD_MORPH];
    uint8_t mod_morph_count;

    zmk_macro_config_t macros[ZMK_MAX_MACROS];
    uint8_t macro_count;

    zmk_sticky_key_state_t sticky_key;
    zmk_caps_word_state_t caps_word;

    /* Last pressed key for &key_repeat */
    uint8_t last_keycode;
    uint8_t last_modifiers;

    /* Key toggles */
    bool key_toggle_states[256];

    /* Active modifiers tracked globally */
    uint8_t active_modifiers;
    uint32_t current_time_ms;
} zmk_behavior_app_t;

void zmk_behavior_construct(zmk_behavior_app_t *self, uint32_t module_id, uint32_t priority,
                            const zmk_behavior_hid_if_t *hid,
                            const zmk_behavior_keymap_if_t *keymap);

edge_status_t zmk_behavior_init(zmk_behavior_app_t *self);
edge_status_t zmk_behavior_shutdown(zmk_behavior_app_t *self);

/* Add configurations */
edge_status_t zmk_behavior_add_hold_tap(zmk_behavior_app_t *self, const zmk_ht_config_t *config,
                                        uint32_t hold_behavior_id, uint32_t hold_param,
                                        uint32_t tap_behavior_id, uint32_t tap_param,
                                        uint8_t *out_index);

edge_status_t zmk_behavior_add_tap_dance(zmk_behavior_app_t *self,
                                         const zmk_tap_dance_config_t *config, uint8_t *out_index);

edge_status_t zmk_behavior_add_mod_morph(zmk_behavior_app_t *self,
                                         const zmk_mod_morph_config_t *config, uint8_t *out_index);

edge_status_t zmk_behavior_add_macro(zmk_behavior_app_t *self, const zmk_macro_config_t *config,
                                     uint8_t *out_index);

/* Main invocation entry point */
edge_status_t zmk_behavior_invoke(zmk_behavior_app_t *self, uint16_t behavior_id, uint32_t param1,
                                  uint32_t param2, bool pressed, uint32_t timestamp_ms);

/* Timer/tick update for deferred resolutions */
edge_status_t zmk_behavior_tick(zmk_behavior_app_t *self, uint32_t timestamp_ms);

#ifdef __cplusplus
}
#endif

#endif /* ZMK_BEHAVIOR_H */
