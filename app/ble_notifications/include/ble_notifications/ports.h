#ifndef BLE_NOTIFICATIONS_PORTS_H
#define BLE_NOTIFICATIONS_PORTS_H

#include "edge/errors.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ble_notifications_call_port {
    void *self;
    edge_status_t (*send_call_response)(void *self, uint8_t response_code);
} ble_notifications_call_port_t;

#ifdef __cplusplus
}
#endif

#endif /* BLE_NOTIFICATIONS_PORTS_H */
