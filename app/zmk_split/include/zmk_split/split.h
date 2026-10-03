#ifndef ZMK_SPLIT_H
#define ZMK_SPLIT_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "zmk_split/ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zmk_split_role {
    ZMK_SPLIT_ROLE_CENTRAL = 0,
    ZMK_SPLIT_ROLE_PERIPHERAL = 1,
};

enum zmk_split_msg_type {
    ZMK_SPLIT_MSG_POSITION_STATE = 0x01,
    ZMK_SPLIT_MSG_BATTERY_STATE = 0x02,
    ZMK_SPLIT_MSG_PING = 0x03,
    ZMK_SPLIT_MSG_PONG = 0x04,
};

typedef struct __attribute__((packed)) zmk_split_packet {
    uint8_t msg_type;
    uint8_t payload[7];
} zmk_split_packet_t;

typedef struct zmk_split_app {
    edge_module_t module;
    const zmk_split_transport_if_t *transport;
    const zmk_split_receiver_if_t *receiver;

    uint8_t role;            /* ZMK_SPLIT_ROLE_CENTRAL or PERIPHERAL */
    uint8_t position_offset; /* e.g. 0 for left half, 36 for right half */
    bool connected;
    uint8_t peripheral_battery_pct;
    uint32_t packets_sent;
    uint32_t packets_received;
} zmk_split_app_t;

void zmk_split_construct(zmk_split_app_t *self, uint32_t module_id, uint32_t priority, uint8_t role,
                         uint8_t position_offset, const zmk_split_transport_if_t *transport,
                         const zmk_split_receiver_if_t *receiver);

edge_status_t zmk_split_init(zmk_split_app_t *self);
edge_status_t zmk_split_shutdown(zmk_split_app_t *self);

/* Forward local key event to central */
edge_status_t zmk_split_forward_position(zmk_split_app_t *self, uint32_t position, bool pressed,
                                         uint32_t timestamp_ms);

/* Process incoming packet from transport */
edge_status_t zmk_split_receive_packet(zmk_split_app_t *self, const uint8_t *data, size_t len);

void zmk_split_set_connected(zmk_split_app_t *self, bool connected);

#ifdef __cplusplus
}
#endif

#endif /* ZMK_SPLIT_H */
