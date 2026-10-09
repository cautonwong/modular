#ifndef ZMK_SPLIT_PORTS_H
#define ZMK_SPLIT_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zmk_split_transport_if {
    void *self;
    edge_status_t (*send_packet)(void *self, const uint8_t *data, size_t len);
} zmk_split_transport_if_t;

typedef struct zmk_split_receiver_if {
    void *self;
    edge_status_t (*on_remote_position_changed)(void *self, uint32_t position, bool pressed,
                                                uint32_t timestamp_ms);
} zmk_split_receiver_if_t;

#ifdef __cplusplus
}
#endif

#endif /* ZMK_SPLIT_PORTS_H */
