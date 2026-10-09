#ifndef ZMK_STUDIO_H
#define ZMK_STUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZMK_STUDIO_FRAMING_SOF 0xABu
#define ZMK_STUDIO_FRAMING_ESC 0xACu
#define ZMK_STUDIO_FRAMING_EOF 0xADu

#define ZMK_STUDIO_MAX_FRAME_LEN 256
#define ZMK_STUDIO_MAX_LAYERS 8
#define ZMK_STUDIO_MAX_KEYS_PER_LAYER 64

typedef enum {
    ZMK_STUDIO_STATE_IDLE,
    ZMK_STUDIO_STATE_AWAITING_DATA,
    ZMK_STUDIO_STATE_ESCAPED,
    ZMK_STUDIO_STATE_ERR,
    ZMK_STUDIO_STATE_EOF,
} zmk_studio_framing_state_t;

typedef enum {
    ZMK_STUDIO_SUBSYS_CORE = 1,
    ZMK_STUDIO_SUBSYS_KEYMAP = 2,
    ZMK_STUDIO_SUBSYS_BEHAVIORS = 3,
} zmk_studio_subsystem_id_t;

typedef enum {
    ZMK_STUDIO_CORE_CMD_GET_DEVICE_INFO = 1,
    ZMK_STUDIO_CORE_CMD_GET_LOCK_STATE = 2,
    ZMK_STUDIO_CORE_CMD_UNLOCK_DEVICE = 3,
    ZMK_STUDIO_CORE_CMD_LOCK_DEVICE = 4,
    ZMK_STUDIO_CORE_CMD_RESET_SETTINGS = 5,
} zmk_studio_core_cmd_t;

typedef enum {
    ZMK_STUDIO_KEYMAP_CMD_GET_KEYMAP = 1,
    ZMK_STUDIO_KEYMAP_CMD_SET_LAYER_BINDING = 2,
    ZMK_STUDIO_KEYMAP_CMD_CHECK_UNSAVED = 3,
    ZMK_STUDIO_KEYMAP_CMD_SAVE_CHANGES = 4,
    ZMK_STUDIO_KEYMAP_CMD_DISCARD_CHANGES = 5,
} zmk_studio_keymap_cmd_t;

typedef struct zmk_studio_transport_if {
    void *self;
    edge_status_t (*send_frame)(void *self, const uint8_t *frame_data, size_t len);
} zmk_studio_transport_if_t;

typedef struct zmk_studio_keymap_if {
    void *self;
    edge_status_t (*get_layer_binding)(void *self, uint8_t layer, uint32_t pos, uint16_t *out_bhv,
                                       uint32_t *out_p1, uint32_t *out_p2);
    edge_status_t (*set_layer_binding)(void *self, uint8_t layer, uint32_t pos, uint16_t bhv,
                                       uint32_t p1, uint32_t p2);
    edge_status_t (*save_changes)(void *self);
    edge_status_t (*discard_changes)(void *self);
} zmk_studio_keymap_if_t;

typedef struct zmk_studio_app {
    edge_module_t module;
    zmk_studio_transport_if_t transport;
    zmk_studio_keymap_if_t keymap;
    zmk_studio_framing_state_t rx_state;
    uint8_t rx_buf[ZMK_STUDIO_MAX_FRAME_LEN];
    size_t rx_len;
    bool unlocked;
    bool unlock_authorized;
    bool unsaved_changes;
    char device_name[32];
    uint32_t serial_number;
} zmk_studio_app_t;

void zmk_studio_construct(zmk_studio_app_t *app, uint32_t module_id, uint8_t priority,
                          const zmk_studio_transport_if_t *transport,
                          const zmk_studio_keymap_if_t *keymap);

edge_status_t zmk_studio_init(zmk_studio_app_t *app);

void zmk_studio_authorize_unlock(zmk_studio_app_t *app, bool authorized);
void zmk_studio_lock(zmk_studio_app_t *app);
void zmk_studio_handle_disconnect(zmk_studio_app_t *app);

bool zmk_studio_framing_process_byte(zmk_studio_framing_state_t *state, uint8_t byte,
                                     uint8_t *out_data);

size_t zmk_studio_frame_encode(const uint8_t *in_data, size_t in_len, uint8_t *out_frame,
                               size_t out_max_len);

edge_status_t zmk_studio_rx_byte(zmk_studio_app_t *app, uint8_t byte);

edge_status_t zmk_studio_process_request(zmk_studio_app_t *app, const uint8_t *payload, size_t len);

bool zmk_studio_is_unlocked(const zmk_studio_app_t *app);

#ifdef __cplusplus
}
#endif

#endif
