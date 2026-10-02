#ifndef BLE_FS_PORTS_H
#define BLE_FS_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ble_fs_storage_port {
    void *self;
    edge_status_t (*file_open)(void *self, const char *path, uint32_t flags);
    edge_status_t (*file_close)(void *self);
    edge_status_t (*file_read)(void *self, uint32_t offset, uint8_t *buf, uint32_t size,
                               uint32_t *read_len);
    edge_status_t (*file_write)(void *self, uint32_t offset, const uint8_t *buf, uint32_t size);
    edge_status_t (*file_delete)(void *self, const char *path);
    edge_status_t (*dir_open)(void *self, const char *path);
    edge_status_t (*dir_read)(void *self, char *entry_name, uint32_t max_len, uint32_t *file_size,
                              bool *is_dir);
    edge_status_t (*dir_close)(void *self);
    edge_status_t (*dir_create)(void *self, const char *path);
    edge_status_t (*file_rename)(void *self, const char *old_path, const char *new_path);
    edge_status_t (*get_free_space)(void *self, uint32_t *free_bytes);
} ble_fs_storage_port_t;

typedef struct ble_fs_tx_port {
    void *self;
    edge_status_t (*send_response)(void *self, const uint8_t *data, size_t len);
} ble_fs_tx_port_t;

#ifdef __cplusplus
}
#endif

#endif /* BLE_FS_PORTS_H */
