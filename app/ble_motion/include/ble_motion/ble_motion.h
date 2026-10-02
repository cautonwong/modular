#ifndef BLE_MOTION_H
#define BLE_MOTION_H

#include "ble_motion/ports.h"
#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ble_motion {
    edge_module_t module;
    ble_motion_notify_port_t notify_port;
    edge_event_sink_t *event_sink;
    bool step_count_notify_enabled;
    bool motion_values_notify_enabled;
    uint32_t last_step_count;
    int16_t last_x;
    int16_t last_y;
    int16_t last_z;
} ble_motion_t;

void ble_motion_init(ble_motion_t *self, const ble_motion_notify_port_t *notify_port,
                     edge_event_sink_t *event_sink);

void ble_motion_construct(ble_motion_t *self, uint32_t module_id, uint32_t priority,
                          const ble_motion_notify_port_t *notify_port,
                          edge_event_sink_t *event_sink);

void ble_motion_set_step_notify_enabled(ble_motion_t *self, bool enabled);
void ble_motion_set_motion_notify_enabled(ble_motion_t *self, bool enabled);

edge_status_t ble_motion_on_step_count(ble_motion_t *self, uint32_t step_count);
edge_status_t ble_motion_on_motion_values(ble_motion_t *self, int16_t x, int16_t y, int16_t z);

edge_status_t ble_motion_encode_steps(uint32_t step_count, uint8_t *out_buf, size_t buf_len);
edge_status_t ble_motion_encode_values(int16_t x, int16_t y, int16_t z, uint8_t *out_buf,
                                       size_t buf_len);

const edge_module_t *ble_motion_module(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_MOTION_H */
