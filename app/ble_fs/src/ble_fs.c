#include <stddef.h>
#include <stdint.h>
static void copy_bytes(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
}
#include "ble_fs/ble_fs.h"
#include "edge/events.h"

static void ble_fs_emit_event(ble_fs_t *self, uint32_t event_id, uint32_t arg0) {
    if (self->event_sink != NULL) {
        edge_event_t ev = {
            .id = event_id,
            .source = self->module.module_id,
            .arg0 = arg0,
            .arg1 = 0u,
            .timestamp = 0u,
        };
        (void)edge_event_sink_push_isr((edge_event_sink_t *)self->event_sink, &ev);
    }
}

static edge_status_t ble_fs_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t ble_fs_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    return EDGE_OK;
}

static edge_status_t ble_fs_power_off(edge_module_t *module) {
    ble_fs_t *self = (ble_fs_t *)module->private_data;
    if (self != NULL && self->storage != NULL && self->storage->file_close != NULL) {
        return self->storage->file_close(self->storage->self);
    }
    return EDGE_OK;
}

void ble_fs_construct(ble_fs_t *self, uint32_t module_id, uint32_t priority,
                      const ble_fs_storage_port_t *storage, const ble_fs_tx_port_t *tx,
                      const edge_event_sink_t *event_sink) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->module.module_id = module_id;
    self->module.priority = priority;
    self->module.period = 1000u;
    self->module.budget = 100u;
    self->module.next_due = 0u;
    self->module.poll = ble_fs_poll;
    self->module.on_event = ble_fs_on_event;
    self->module.power_off = ble_fs_power_off;
    self->module.private_data = self;

    self->storage = storage;
    self->tx = tx;
    self->event_sink = event_sink;
    self->state = BLE_FS_STATE_IDLE;
    self->fs_access_enabled = true;
}

edge_status_t ble_fs_init(ble_fs_t *self, const ble_fs_storage_port_t *storage,
                          const ble_fs_tx_port_t *tx, const edge_event_sink_t *event_sink) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    ble_fs_construct(self, 0x3500u, 10u, storage, tx, event_sink);
    return EDGE_OK;
}

void ble_fs_set_enabled(ble_fs_t *self, bool enabled) {
    if (self != NULL) {
        self->fs_access_enabled = enabled;
    }
}

bool ble_fs_is_enabled(const ble_fs_t *self) {
    return self != NULL ? self->fs_access_enabled : false;
}

edge_status_t ble_fs_process_packet(ble_fs_t *self, const uint8_t *data, size_t len) {
    if (self == NULL || data == NULL || len < 1) {
        return EDGE_EINVAL;
    }

    ble_fs_emit_event(self, EDGE_EVT_WATCH_FS_ACCESS, (uint32_t)data[0]);

    if (!self->fs_access_enabled) {
        if (self->tx != NULL && self->tx->send_response != NULL) {
            uint8_t resp[2] = {data[0], BLE_FS_STATUS_DENIED};
            return self->tx->send_response(self->tx->self, resp, sizeof(resp));
        }
        return EDGE_ENOTSUP;
    }

    uint8_t cmd = data[0];

    switch (cmd) {
    case BLE_FS_CMD_READ: {
        /* ReadHeader: cmd(1), pad(1), pathlen(2), chunkoff(4), chunksize(4), path(N) */
        if (len < 12) {
            return EDGE_EINVAL;
        }
        uint16_t pathlen = (uint16_t)data[2] | ((uint16_t)data[3] << 8u);
        uint32_t chunkoff = (uint32_t)data[4] | ((uint32_t)data[5] << 8u) |
                            ((uint32_t)data[6] << 16u) | ((uint32_t)data[7] << 24u);
        uint32_t chunksize = (uint32_t)data[8] | ((uint32_t)data[9] << 8u) |
                             ((uint32_t)data[10] << 16u) | ((uint32_t)data[11] << 24u);

        if (len < (size_t)(12 + pathlen) || pathlen >= BLE_FS_MAX_PATH) {
            return EDGE_EINVAL;
        }
        copy_bytes(self->current_path, &data[12], pathlen);
        self->current_path[pathlen] = '\0';

        if (self->storage != NULL && self->storage->file_open != NULL) {
            (void)self->storage->file_open(self->storage->self, self->current_path, 0);
        }

        uint8_t resp[256];
        resp[0] = BLE_FS_CMD_READ_DATA;
        resp[1] = BLE_FS_STATUS_OK;
        resp[2] = 0;
        resp[3] = 0;
        /* chunkoff (4 bytes) */
        resp[4] = (uint8_t)(chunkoff & 0xFFu);
        resp[5] = (uint8_t)((chunkoff >> 8u) & 0xFFu);
        resp[6] = (uint8_t)((chunkoff >> 16u) & 0xFFu);
        resp[7] = (uint8_t)((chunkoff >> 24u) & 0xFFu);

        uint32_t read_bytes = 0;
        if (self->storage != NULL && self->storage->file_read != NULL) {
            uint32_t max_read = chunksize > 200 ? 200 : chunksize;
            (void)self->storage->file_read(self->storage->self, chunkoff, &resp[16], max_read,
                                           &read_bytes);
        }

        /* totallen (4 bytes) */
        uint32_t totallen = chunkoff + read_bytes;
        resp[8] = (uint8_t)(totallen & 0xFFu);
        resp[9] = (uint8_t)((totallen >> 8u) & 0xFFu);
        resp[10] = (uint8_t)((totallen >> 16u) & 0xFFu);
        resp[11] = (uint8_t)((totallen >> 24u) & 0xFFu);

        /* chunklen (4 bytes) */
        resp[12] = (uint8_t)(read_bytes & 0xFFu);
        resp[13] = (uint8_t)((read_bytes >> 8u) & 0xFFu);
        resp[14] = (uint8_t)((read_bytes >> 16u) & 0xFFu);
        resp[15] = (uint8_t)((read_bytes >> 24u) & 0xFFu);

        if (self->tx != NULL && self->tx->send_response != NULL) {
            return self->tx->send_response(self->tx->self, resp, 16 + read_bytes);
        }
        return EDGE_OK;
    }

    case BLE_FS_CMD_WRITE: {
        /* WriteHeader: cmd(1), pad(1), pathlen(2), offset(4), modTime(8), totalSize(4), path(N) */
        if (len < 20) {
            return EDGE_EINVAL;
        }
        uint16_t pathlen = (uint16_t)data[2] | ((uint16_t)data[3] << 8u);
        uint32_t offset = (uint32_t)data[4] | ((uint32_t)data[5] << 8u) |
                          ((uint32_t)data[6] << 16u) | ((uint32_t)data[7] << 24u);
        uint32_t totalsize = (uint32_t)data[16] | ((uint32_t)data[17] << 8u) |
                             ((uint32_t)data[18] << 16u) | ((uint32_t)data[19] << 24u);

        if (len < (size_t)(20 + pathlen) || pathlen >= BLE_FS_MAX_PATH) {
            return EDGE_EINVAL;
        }
        copy_bytes(self->current_path, &data[20], pathlen);
        self->current_path[pathlen] = '\0';
        self->file_offset = offset;
        self->file_total_size = totalsize;
        self->state = BLE_FS_STATE_WRITE;

        if (self->storage != NULL && self->storage->file_open != NULL) {
            (void)self->storage->file_open(self->storage->self, self->current_path, 1);
        }

        uint8_t resp[20] = {0};
        resp[0] = BLE_FS_CMD_WRITE_PACING;
        resp[1] = BLE_FS_STATUS_OK;
        /* offset */
        resp[4] = (uint8_t)(offset & 0xFFu);
        resp[5] = (uint8_t)((offset >> 8u) & 0xFFu);
        resp[6] = (uint8_t)((offset >> 16u) & 0xFFu);
        resp[7] = (uint8_t)((offset >> 24u) & 0xFFu);

        uint32_t freespace = 1048576u;
        if (self->storage != NULL && self->storage->get_free_space != NULL) {
            (void)self->storage->get_free_space(self->storage->self, &freespace);
        }
        resp[16] = (uint8_t)(freespace & 0xFFu);
        resp[17] = (uint8_t)((freespace >> 8u) & 0xFFu);
        resp[18] = (uint8_t)((freespace >> 16u) & 0xFFu);
        resp[19] = (uint8_t)((freespace >> 24u) & 0xFFu);

        if (self->tx != NULL && self->tx->send_response != NULL) {
            return self->tx->send_response(self->tx->self, resp, sizeof(resp));
        }
        return EDGE_OK;
    }

    case BLE_FS_CMD_WRITE_DATA: {
        /* WritePacing: cmd(1), status(1), pad(2), offset(4), dataSize(4), data(N) */
        if (len < 12) {
            return EDGE_EINVAL;
        }
        uint32_t offset = (uint32_t)data[4] | ((uint32_t)data[5] << 8u) |
                          ((uint32_t)data[6] << 16u) | ((uint32_t)data[7] << 24u);
        uint32_t datasize = (uint32_t)data[8] | ((uint32_t)data[9] << 8u) |
                            ((uint32_t)data[10] << 16u) | ((uint32_t)data[11] << 24u);

        if (len < (size_t)(12 + datasize)) {
            return EDGE_EINVAL;
        }
        if (self->storage != NULL && self->storage->file_write != NULL) {
            (void)self->storage->file_write(self->storage->self, offset, &data[12], datasize);
        }
        self->file_offset = offset + datasize;
        return EDGE_OK;
    }

    case BLE_FS_CMD_DELETE: {
        /* DelHeader: cmd(1), pad(1), pathlen(2), path(N) */
        if (len < 4) {
            return EDGE_EINVAL;
        }
        uint16_t pathlen = (uint16_t)data[2] | ((uint16_t)data[3] << 8u);
        if (len < (size_t)(4 + pathlen) || pathlen >= BLE_FS_MAX_PATH) {
            return EDGE_EINVAL;
        }
        char path[BLE_FS_MAX_PATH];
        copy_bytes(path, &data[4], pathlen);
        path[pathlen] = '\0';

        uint8_t status = BLE_FS_STATUS_OK;
        if (self->storage != NULL && self->storage->file_delete != NULL) {
            if (self->storage->file_delete(self->storage->self, path) != EDGE_OK) {
                status = BLE_FS_STATUS_ERR;
            }
        }
        if (self->tx != NULL && self->tx->send_response != NULL) {
            uint8_t resp[2] = {BLE_FS_CMD_DELETE_STATUS, status};
            return self->tx->send_response(self->tx->self, resp, sizeof(resp));
        }
        return EDGE_OK;
    }

    case BLE_FS_CMD_MKDIR: {
        /* MKDirHeader: cmd(1), pad(1), pathlen(2), pad2(4), time(8), path(N) */
        if (len < 16) {
            return EDGE_EINVAL;
        }
        uint16_t pathlen = (uint16_t)data[2] | ((uint16_t)data[3] << 8u);
        if (len < (size_t)(16 + pathlen) || pathlen >= BLE_FS_MAX_PATH) {
            return EDGE_EINVAL;
        }
        char path[BLE_FS_MAX_PATH];
        copy_bytes(path, &data[16], pathlen);
        path[pathlen] = '\0';

        uint8_t status = BLE_FS_STATUS_OK;
        if (self->storage != NULL && self->storage->dir_create != NULL) {
            if (self->storage->dir_create(self->storage->self, path) != EDGE_OK) {
                status = BLE_FS_STATUS_ERR;
            }
        }
        if (self->tx != NULL && self->tx->send_response != NULL) {
            uint8_t resp[16] = {0};
            resp[0] = BLE_FS_CMD_MKDIR_STATUS;
            resp[1] = status;
            return self->tx->send_response(self->tx->self, resp, sizeof(resp));
        }
        return EDGE_OK;
    }

    case BLE_FS_CMD_MOVE: {
        /* MoveHeader: cmd(1), pad(1), oldlen(2), newlen(2), oldpath(N), newpath(M) */
        if (len < 6) {
            return EDGE_EINVAL;
        }
        uint16_t oldlen = (uint16_t)data[2] | ((uint16_t)data[3] << 8u);
        uint16_t newlen = (uint16_t)data[4] | ((uint16_t)data[5] << 8u);
        if (len < (size_t)(6 + oldlen + newlen) || oldlen >= BLE_FS_MAX_PATH ||
            newlen >= BLE_FS_MAX_PATH) {
            return EDGE_EINVAL;
        }
        char oldpath[BLE_FS_MAX_PATH];
        char newpath[BLE_FS_MAX_PATH];
        copy_bytes(oldpath, &data[6], oldlen);
        oldpath[oldlen] = '\0';
        copy_bytes(newpath, &data[6 + oldlen], newlen);
        newpath[newlen] = '\0';

        uint8_t status = BLE_FS_STATUS_OK;
        if (self->storage != NULL && self->storage->file_rename != NULL) {
            if (self->storage->file_rename(self->storage->self, oldpath, newpath) != EDGE_OK) {
                status = BLE_FS_STATUS_ERR;
            }
        }
        if (self->tx != NULL && self->tx->send_response != NULL) {
            uint8_t resp[2] = {BLE_FS_CMD_MOVE_STATUS, status};
            return self->tx->send_response(self->tx->self, resp, sizeof(resp));
        }
        return EDGE_OK;
    }

    default:
        return EDGE_ENOTSUP;
    }
}
