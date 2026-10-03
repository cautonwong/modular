#ifndef BLE_DFU_H
#define BLE_DFU_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_DFU_REVISION 0x0008u
#define BLE_DFU_MAX_IMAGE_SIZE 475136u
#define BLE_DFU_FLASH_OFFSET 0x40000u
#define BLE_DFU_BUFFER_SIZE 200u

/* Opcodes */
#define BLE_DFU_OPCODE_START_DFU 0x01u
#define BLE_DFU_OPCODE_INIT_DFU_PARAMS 0x02u
#define BLE_DFU_OPCODE_RECEIVE_FW_IMAGE 0x03u
#define BLE_DFU_OPCODE_VALIDATE_FW 0x04u
#define BLE_DFU_OPCODE_ACTIVATE_AND_RESET 0x05u
#define BLE_DFU_OPCODE_PKT_RCPT_NOTIF_REQ 0x08u
#define BLE_DFU_OPCODE_RESPONSE 0x10u
#define BLE_DFU_OPCODE_PKT_RCPT_NOTIF 0x11u

/* Error Codes */
#define BLE_DFU_ERR_NO_ERROR 0x01u
#define BLE_DFU_ERR_INVALID_STATE 0x02u
#define BLE_DFU_ERR_NOT_SUPPORTED 0x03u
#define BLE_DFU_ERR_DATA_SIZE_EXCEEDS_LIMITS 0x04u
#define BLE_DFU_ERR_CRC_ERROR 0x05u
#define BLE_DFU_ERR_OPERATION_FAILED 0x06u

typedef enum ble_dfu_state {
    BLE_DFU_STATE_IDLE = 0,
    BLE_DFU_STATE_INIT = 1,
    BLE_DFU_STATE_START = 2,
    BLE_DFU_STATE_DATA = 3,
    BLE_DFU_STATE_VALIDATE = 4,
    BLE_DFU_STATE_VALIDATED = 5,
} ble_dfu_state_t;

typedef struct ble_dfu {
    edge_module_t module;
    const ble_dfu_flash_port_t *flash;
    const ble_dfu_notify_port_t *notify;
    const ble_dfu_system_port_t *system;
    const edge_event_sink_t *event_sink;

    ble_dfu_state_t state;
    uint32_t total_size;
    uint32_t chunk_size;
    uint16_t expected_crc;

    uint32_t bytes_received;
    uint32_t packets_received;
    uint8_t nb_packets_to_notify;

    uint8_t buffer[BLE_DFU_BUFFER_SIZE];
    size_t buffer_index;
    size_t flash_write_index;
    bool dfu_enabled;
} ble_dfu_t;

void ble_dfu_construct(ble_dfu_t *self, uint32_t module_id, uint32_t priority,
                       const ble_dfu_flash_port_t *flash, const ble_dfu_notify_port_t *notify,
                       const ble_dfu_system_port_t *system, const edge_event_sink_t *event_sink);

edge_status_t ble_dfu_init(ble_dfu_t *self, const ble_dfu_flash_port_t *flash,
                           const ble_dfu_notify_port_t *notify, const ble_dfu_system_port_t *system,
                           const edge_event_sink_t *event_sink);

void ble_dfu_set_enabled(ble_dfu_t *self, bool enabled);
bool ble_dfu_is_enabled(const ble_dfu_t *self);
ble_dfu_state_t ble_dfu_get_state(const ble_dfu_t *self);

edge_status_t ble_dfu_control_point_handler(ble_dfu_t *self, const uint8_t *data, size_t len);
edge_status_t ble_dfu_write_packet_handler(ble_dfu_t *self, const uint8_t *data, size_t len);

uint16_t ble_dfu_compute_crc16(const uint8_t *data, size_t size, uint16_t init_crc);

#ifdef __cplusplus
}
#endif

#endif /* BLE_DFU_H */
