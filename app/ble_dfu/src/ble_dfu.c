#include "ble_dfu/ble_dfu.h"
#include "edge/events.h"

uint16_t ble_dfu_compute_crc16(const uint8_t *data, size_t size, uint16_t init_crc) {
    uint16_t crc = init_crc;
    for (size_t i = 0; i < size; i++) {
        crc = (uint8_t)(crc >> 8u) | (crc << 8u);
        crc ^= data[i];
        crc ^= (uint8_t)(crc & 0xFFu) >> 4u;
        crc ^= (crc << 8u) << 4u;
        crc ^= ((crc & 0xFFu) << 4u) << 1u;
    }
    return crc;
}

static void ble_dfu_emit_state_event(ble_dfu_t *self) {
    if (self->event_sink != NULL) {
        edge_event_t ev = {
            .id = EDGE_EVT_WATCH_DFU_STATE,
            .source = self->module.module_id,
            .arg0 = (uint32_t)self->state,
            .arg1 = self->bytes_received,
            .timestamp = 0u,
        };
        (void)edge_event_sink_push_isr((edge_event_sink_t *)self->event_sink, &ev);
    }
}

static edge_status_t ble_dfu_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t ble_dfu_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    return EDGE_OK;
}

static edge_status_t ble_dfu_power_off(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

void ble_dfu_construct(ble_dfu_t *self, uint32_t module_id, uint32_t priority,
                       const ble_dfu_flash_port_t *flash, const ble_dfu_notify_port_t *notify,
                       const ble_dfu_system_port_t *system, const edge_event_sink_t *event_sink) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->module.module_id = module_id;
    self->module.priority = priority;
    self->module.period = 1000u;
    self->module.budget = 100u;
    self->module.next_due = 0u;
    self->module.poll = ble_dfu_poll;
    self->module.on_event = ble_dfu_on_event;
    self->module.power_off = ble_dfu_power_off;
    self->module.private_data = self;

    self->flash = flash;
    self->notify = notify;
    self->system = system;
    self->event_sink = event_sink;
    self->state = BLE_DFU_STATE_IDLE;
    self->dfu_enabled = true;
}

edge_status_t ble_dfu_init(ble_dfu_t *self, const ble_dfu_flash_port_t *flash,
                           const ble_dfu_notify_port_t *notify, const ble_dfu_system_port_t *system,
                           const edge_event_sink_t *event_sink) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    ble_dfu_construct(self, 0x3600u, 10u, flash, notify, system, event_sink);
    return EDGE_OK;
}

void ble_dfu_set_enabled(ble_dfu_t *self, bool enabled) {
    if (self != NULL) {
        self->dfu_enabled = enabled;
    }
}

bool ble_dfu_is_enabled(const ble_dfu_t *self) {
    return self != NULL ? self->dfu_enabled : false;
}

ble_dfu_state_t ble_dfu_get_state(const ble_dfu_t *self) {
    return self != NULL ? self->state : BLE_DFU_STATE_IDLE;
}

edge_status_t ble_dfu_control_point_handler(ble_dfu_t *self, const uint8_t *data, size_t len) {
    if (self == NULL || data == NULL || len < 1) {
        return EDGE_EINVAL;
    }

    if (!self->dfu_enabled) {
        if (self->notify != NULL && self->notify->send_response != NULL) {
            (void)self->notify->send_response(self->notify->self, data[0],
                                              BLE_DFU_ERR_NOT_SUPPORTED);
        }
        return EDGE_ENOTSUP;
    }

    uint8_t opcode = data[0];

    switch (opcode) {
    case BLE_DFU_OPCODE_START_DFU: {
        /* StartDFU: opcode(1), type(1), [sd_size(4), bl_size(4), app_size(4)] */
        if (len < 6) {
            if (self->notify != NULL && self->notify->send_response != NULL) {
                (void)self->notify->send_response(self->notify->self, opcode,
                                                  BLE_DFU_ERR_INVALID_STATE);
            }
            return EDGE_EINVAL;
        }
        uint8_t img_type = data[1];
        uint32_t app_size = 0;
        if (img_type == 0x04) {
            app_size = (uint32_t)data[2] | ((uint32_t)data[3] << 8u) | ((uint32_t)data[4] << 16u) |
                       ((uint32_t)data[5] << 24u);
        } else if (len >= 14) {
            app_size = (uint32_t)data[10] | ((uint32_t)data[11] << 8u) |
                       ((uint32_t)data[12] << 16u) | ((uint32_t)data[13] << 24u);
        }

        if (app_size == 0 || app_size > BLE_DFU_MAX_IMAGE_SIZE) {
            if (self->notify != NULL && self->notify->send_response != NULL) {
                (void)self->notify->send_response(self->notify->self, opcode,
                                                  BLE_DFU_ERR_DATA_SIZE_EXCEEDS_LIMITS);
            }
            return EDGE_EOVERFLOW;
        }

        self->total_size = app_size;
        self->bytes_received = 0;
        self->packets_received = 0;
        self->buffer_index = 0;
        self->flash_write_index = 0;
        self->state = BLE_DFU_STATE_START;
        ble_dfu_emit_state_event(self);

        if (self->notify != NULL && self->notify->send_response != NULL) {
            (void)self->notify->send_response(self->notify->self, opcode, BLE_DFU_ERR_NO_ERROR);
        }
        return EDGE_OK;
    }

    case BLE_DFU_OPCODE_INIT_DFU_PARAMS: {
        /* InitDFUParams: opcode(1), chunk_size(2) or full params */
        if (len < 3) {
            return EDGE_EINVAL;
        }
        self->chunk_size = (uint32_t)data[1] | ((uint32_t)data[2] << 8u);
        if (len >= 5) {
            self->expected_crc = (uint16_t)data[3] | ((uint16_t)data[4] << 8u);
        }
        self->state = BLE_DFU_STATE_INIT;
        ble_dfu_emit_state_event(self);

        if (self->notify != NULL && self->notify->send_response != NULL) {
            (void)self->notify->send_response(self->notify->self, opcode, BLE_DFU_ERR_NO_ERROR);
        }
        return EDGE_OK;
    }

    case BLE_DFU_OPCODE_RECEIVE_FW_IMAGE: {
        if (self->state != BLE_DFU_STATE_INIT && self->state != BLE_DFU_STATE_START) {
            if (self->notify != NULL && self->notify->send_response != NULL) {
                (void)self->notify->send_response(self->notify->self, opcode,
                                                  BLE_DFU_ERR_INVALID_STATE);
            }
            return EDGE_EINVAL;
        }

        /* Erase flash range for OTA image */
        if (self->flash != NULL && self->flash->erase_range != NULL) {
            (void)self->flash->erase_range(self->flash->self, BLE_DFU_FLASH_OFFSET,
                                           self->total_size);
        }

        self->state = BLE_DFU_STATE_DATA;
        ble_dfu_emit_state_event(self);

        if (self->notify != NULL && self->notify->send_response != NULL) {
            (void)self->notify->send_response(self->notify->self, opcode, BLE_DFU_ERR_NO_ERROR);
        }
        return EDGE_OK;
    }

    case BLE_DFU_OPCODE_VALIDATE_FW: {
        if (self->bytes_received != self->total_size) {
            if (self->notify != NULL && self->notify->send_response != NULL) {
                (void)self->notify->send_response(self->notify->self, opcode,
                                                  BLE_DFU_ERR_OPERATION_FAILED);
            }
            return EDGE_EINVAL;
        }

        /* Flush remaining buffer if any */
        if (self->buffer_index > 0 && self->flash != NULL && self->flash->write_chunk != NULL) {
            (void)self->flash->write_chunk(self->flash->self,
                                           BLE_DFU_FLASH_OFFSET + self->flash_write_index,
                                           self->buffer, self->buffer_index);
            self->flash_write_index += self->buffer_index;
            self->buffer_index = 0;
        }

        /* Verify CRC over flashed image */
        uint16_t computed_crc = 0xFFFFu;
        if (self->flash != NULL && self->flash->read_chunk != NULL) {
            uint8_t read_buf[64];
            uint32_t offset = 0;
            while (offset < self->total_size) {
                uint32_t chunk = (self->total_size - offset > sizeof(read_buf))
                                     ? sizeof(read_buf)
                                     : (self->total_size - offset);
                (void)self->flash->read_chunk(self->flash->self, BLE_DFU_FLASH_OFFSET + offset,
                                              read_buf, chunk);
                computed_crc = ble_dfu_compute_crc16(read_buf, chunk, computed_crc);
                offset += chunk;
            }
        }

        if (self->expected_crc != 0 && computed_crc != self->expected_crc) {
            self->state = BLE_DFU_STATE_IDLE;
            ble_dfu_emit_state_event(self);
            if (self->notify != NULL && self->notify->send_response != NULL) {
                (void)self->notify->send_response(self->notify->self, opcode,
                                                  BLE_DFU_ERR_CRC_ERROR);
            }
            return EDGE_EIO;
        }

        self->state = BLE_DFU_STATE_VALIDATED;
        ble_dfu_emit_state_event(self);

        if (self->notify != NULL && self->notify->send_response != NULL) {
            (void)self->notify->send_response(self->notify->self, opcode, BLE_DFU_ERR_NO_ERROR);
        }
        return EDGE_OK;
    }

    case BLE_DFU_OPCODE_ACTIVATE_AND_RESET: {
        if (self->state != BLE_DFU_STATE_VALIDATED) {
            if (self->notify != NULL && self->notify->send_response != NULL) {
                (void)self->notify->send_response(self->notify->self, opcode,
                                                  BLE_DFU_ERR_INVALID_STATE);
            }
            return EDGE_EINVAL;
        }

        if (self->system != NULL && self->system->system_reset != NULL) {
            (void)self->system->system_reset(self->system->self);
        }
        return EDGE_OK;
    }

    case BLE_DFU_OPCODE_PKT_RCPT_NOTIF_REQ: {
        if (len < 3) {
            return EDGE_EINVAL;
        }
        self->nb_packets_to_notify = data[1];
        if (self->notify != NULL && self->notify->send_response != NULL) {
            (void)self->notify->send_response(self->notify->self, opcode, BLE_DFU_ERR_NO_ERROR);
        }
        return EDGE_OK;
    }

    default:
        if (self->notify != NULL && self->notify->send_response != NULL) {
            (void)self->notify->send_response(self->notify->self, opcode,
                                              BLE_DFU_ERR_NOT_SUPPORTED);
        }
        return EDGE_ENOTSUP;
    }
}

edge_status_t ble_dfu_write_packet_handler(ble_dfu_t *self, const uint8_t *data, size_t len) {
    if (self == NULL || data == NULL || len == 0) {
        return EDGE_EINVAL;
    }
    if (self->state != BLE_DFU_STATE_DATA) {
        return EDGE_EBUSY;
    }

    for (size_t i = 0; i < len; i++) {
        self->buffer[self->buffer_index++] = data[i];
        self->bytes_received++;

        if (self->buffer_index >= BLE_DFU_BUFFER_SIZE) {
            if (self->flash != NULL && self->flash->write_chunk != NULL) {
                (void)self->flash->write_chunk(self->flash->self,
                                               BLE_DFU_FLASH_OFFSET + self->flash_write_index,
                                               self->buffer, self->buffer_index);
            }
            self->flash_write_index += self->buffer_index;
            self->buffer_index = 0;
        }
    }

    self->packets_received++;
    if (self->nb_packets_to_notify > 0 &&
        (self->packets_received % self->nb_packets_to_notify) == 0) {
        if (self->notify != NULL && self->notify->send_prn != NULL) {
            (void)self->notify->send_prn(self->notify->self, self->bytes_received);
        }
    }

    return EDGE_OK;
}
