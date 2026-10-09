#ifndef BLE_FS_H
#define BLE_FS_H

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

#define BLE_FS_VERSION 0x0004u
#define BLE_FS_MAX_PATH 64u

/* FS Protocol Command Opcodes */
#define BLE_FS_CMD_INVALID 0x00u
#define BLE_FS_CMD_READ 0x10u
#define BLE_FS_CMD_READ_DATA 0x11u
#define BLE_FS_CMD_READ_PACING 0x12u
#define BLE_FS_CMD_WRITE 0x20u
#define BLE_FS_CMD_WRITE_PACING 0x21u
#define BLE_FS_CMD_WRITE_DATA 0x22u
#define BLE_FS_CMD_DELETE 0x30u
#define BLE_FS_CMD_DELETE_STATUS 0x31u
#define BLE_FS_CMD_MKDIR 0x40u
#define BLE_FS_CMD_MKDIR_STATUS 0x41u
#define BLE_FS_CMD_LISTDIR 0x50u
#define BLE_FS_CMD_LISTDIR_ENTRY 0x51u
#define BLE_FS_CMD_MOVE 0x60u
#define BLE_FS_CMD_MOVE_STATUS 0x61u

/* FS Protocol Status Opcodes */
#define BLE_FS_STATUS_OK 0x00u
#define BLE_FS_STATUS_ERR 0x01u
#define BLE_FS_STATUS_NOT_FOUND 0x02u
#define BLE_FS_STATUS_DENIED 0x03u

typedef enum ble_fs_state {
    BLE_FS_STATE_IDLE = 0,
    BLE_FS_STATE_READ = 1,
    BLE_FS_STATE_WRITE = 2,
} ble_fs_state_t;

typedef struct ble_fs {
    edge_module_t module;
    const ble_fs_storage_port_t *storage;
    const ble_fs_tx_port_t *tx;
    const edge_event_sink_t *event_sink;
    ble_fs_state_t state;
    char current_path[BLE_FS_MAX_PATH];
    uint32_t file_offset;
    uint32_t file_total_size;
    bool fs_access_enabled;
} ble_fs_t;

void ble_fs_construct(ble_fs_t *self, uint32_t module_id, uint32_t priority,
                      const ble_fs_storage_port_t *storage, const ble_fs_tx_port_t *tx,
                      const edge_event_sink_t *event_sink);

edge_status_t ble_fs_init(ble_fs_t *self, const ble_fs_storage_port_t *storage,
                          const ble_fs_tx_port_t *tx, const edge_event_sink_t *event_sink);

void ble_fs_set_enabled(ble_fs_t *self, bool enabled);
bool ble_fs_is_enabled(const ble_fs_t *self);

edge_status_t ble_fs_process_packet(ble_fs_t *self, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* BLE_FS_H */
