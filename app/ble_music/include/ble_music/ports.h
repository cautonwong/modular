#ifndef BLE_MUSIC_PORTS_H
#define BLE_MUSIC_PORTS_H

#include "edge/errors.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ble_music_transport_port {
    void *self;
    edge_status_t (*send_event)(void *self, uint8_t event_byte);
    uint64_t (*get_tick_ms)(void *self);
} ble_music_transport_port_t;

#ifdef __cplusplus
}
#endif

#endif /* BLE_MUSIC_PORTS_H */
