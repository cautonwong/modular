#ifndef BLE_DFU_PORTS_H
#define BLE_DFU_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ble_dfu_flash_port {
    void *self;
    edge_status_t (*erase_range)(void *self, uint32_t offset, uint32_t length);
    edge_status_t (*write_chunk)(void *self, uint32_t offset, const uint8_t *data, uint32_t size);
    edge_status_t (*read_chunk)(void *self, uint32_t offset, uint8_t *data, uint32_t size);
} ble_dfu_flash_port_t;

typedef struct ble_dfu_notify_port {
    void *self;
    edge_status_t (*send_response)(void *self, uint8_t req_opcode, uint8_t error_code);
    edge_status_t (*send_prn)(void *self, uint32_t bytes_received);
} ble_dfu_notify_port_t;

typedef struct ble_dfu_system_port {
    void *self;
    edge_status_t (*system_reset)(void *self);
} ble_dfu_system_port_t;

#ifdef __cplusplus
}
#endif

#endif /* BLE_DFU_PORTS_H */
