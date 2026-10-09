#ifndef BLE_MOTION_PORTS_H
#define BLE_MOTION_PORTS_H

#include "edge/errors.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ble_motion_notify_port {
    void *self;
    edge_status_t (*notify_step_count)(void *self, uint32_t step_count);
    edge_status_t (*notify_motion_values)(void *self, int16_t x, int16_t y, int16_t z);
} ble_motion_notify_port_t;

#ifdef __cplusplus
}
#endif

#endif /* BLE_MOTION_PORTS_H */
