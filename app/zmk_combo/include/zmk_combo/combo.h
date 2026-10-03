#ifndef ZMK_COMBO_H
#define ZMK_COMBO_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "zmk_combo/ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZMK_COMBO_MAX_COMBOS 32u
#define ZMK_COMBO_MAX_KEYS_PER_COMBO 4u
#define ZMK_COMBO_MAX_POSITIONS 128u

typedef struct zmk_combo_binding {
    uint16_t behavior_id;
    uint32_t param1;
    uint32_t param2;
} zmk_combo_binding_t;

typedef struct zmk_combo_config {
    uint8_t positions[ZMK_COMBO_MAX_KEYS_PER_COMBO];
    uint8_t position_count;
    uint32_t layers_mask; /* 0 = all layers */
    uint16_t timeout_ms;  /* Max interval between first key and all keys pressed */
    zmk_combo_binding_t binding;
} zmk_combo_config_t;

typedef struct zmk_combo_state {
    bool active;
    uint8_t pressed_mask; /* Bitmask of which keys in the combo are currently pressed */
    uint32_t start_time_ms;
} zmk_combo_state_t;

typedef struct zmk_combo_app {
    edge_module_t module;
    const zmk_combo_behavior_if_t *behavior_port;

    zmk_combo_config_t configs[ZMK_COMBO_MAX_COMBOS];
    zmk_combo_state_t states[ZMK_COMBO_MAX_COMBOS];
    uint8_t combo_count;

    /* Tracks active pressed physical keys */
    bool key_pressed[ZMK_COMBO_MAX_POSITIONS];
    uint32_t key_press_time[ZMK_COMBO_MAX_POSITIONS];
} zmk_combo_app_t;

void zmk_combo_construct(zmk_combo_app_t *self, uint32_t module_id, uint32_t priority,
                         const zmk_combo_behavior_if_t *behavior_port);

edge_status_t zmk_combo_init(zmk_combo_app_t *self);
edge_status_t zmk_combo_shutdown(zmk_combo_app_t *self);

edge_status_t zmk_combo_add_combo(zmk_combo_app_t *self, const zmk_combo_config_t *config);

/* Returns true if the key press/release was absorbed by a combo */
bool zmk_combo_process_key(zmk_combo_app_t *self, uint32_t position, bool pressed,
                           uint32_t timestamp_ms, uint32_t active_layers_mask);

#ifdef __cplusplus
}
#endif

#endif /* ZMK_COMBO_H */
