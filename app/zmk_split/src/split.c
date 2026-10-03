#include "zmk_split/split.h"

static edge_status_t zmk_split_poll(edge_module_t *module) {
    const zmk_split_app_t *self = (const zmk_split_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

static edge_status_t zmk_split_power_off(edge_module_t *module) {
    zmk_split_app_t *self = (zmk_split_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->connected = false;
    return EDGE_OK;
}

void zmk_split_construct(zmk_split_app_t *self, uint32_t module_id, uint32_t priority, uint8_t role,
                         uint8_t position_offset, const zmk_split_transport_if_t *transport,
                         const zmk_split_receiver_if_t *receiver) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 1u,
        .budget = 0u,
        .next_due = 0u,
        .poll = zmk_split_poll,
        .on_event = NULL,
        .power_off = zmk_split_power_off,
        .private_data = self,
    };
    self->role = role;
    self->position_offset = position_offset;
    self->transport = transport;
    self->receiver = receiver;
    self->connected = false;
}

edge_status_t zmk_split_init(zmk_split_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->connected = false;
    self->packets_sent = 0;
    self->packets_received = 0;
    self->peripheral_battery_pct = 100;
    return EDGE_OK;
}

edge_status_t zmk_split_shutdown(zmk_split_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->connected = false;
    return EDGE_OK;
}

static uint8_t zmk_split_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x07);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

edge_status_t zmk_split_forward_position(zmk_split_app_t *self, uint32_t position, bool pressed,
                                         uint32_t timestamp_ms) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    if (self->transport == NULL || self->transport->send_packet == NULL) {
        return EDGE_ENOTSUP;
    }

    zmk_split_packet_t pkt = {0};
    pkt.seq_num = ++self->tx_seq;
    pkt.msg_type = ZMK_SPLIT_MSG_POSITION_STATE;
    /* Apply peripheral position offset */
    uint32_t global_pos = position + self->position_offset;
    pkt.payload[0] = (uint8_t)(global_pos & 0xFF);
    pkt.payload[1] = pressed ? 1 : 0;
    pkt.payload[2] = (uint8_t)(timestamp_ms & 0xFF);
    pkt.payload[3] = (uint8_t)((timestamp_ms >> 8) & 0xFF);
    pkt.payload[4] = (uint8_t)((timestamp_ms >> 16) & 0xFF);
    pkt.payload[5] = (uint8_t)((timestamp_ms >> 24) & 0xFF);

    pkt.crc8 = zmk_split_crc8((const uint8_t *)&pkt, sizeof(pkt) - 1);

    self->packets_sent++;
    return self->transport->send_packet(self->transport->self, (const uint8_t *)&pkt, sizeof(pkt));
}

edge_status_t zmk_split_receive_packet(zmk_split_app_t *self, const uint8_t *data, size_t len) {
    if (self == NULL || data == NULL || len < sizeof(zmk_split_packet_t)) {
        return EDGE_EINVAL;
    }

    const zmk_split_packet_t *pkt = (const zmk_split_packet_t *)data;

    /* Verify CRC8 checksum */
    uint8_t expected_crc = zmk_split_crc8(data, sizeof(zmk_split_packet_t) - 1);
    if (pkt->crc8 != expected_crc) {
        return EDGE_EIO; /* Corrupted packet rejected */
    }

    self->rx_seq = pkt->seq_num;
    self->packets_received++;

    if (pkt->msg_type == ZMK_SPLIT_MSG_POSITION_STATE) {
        uint32_t position = pkt->payload[0];
        bool pressed = pkt->payload[1] != 0;
        uint32_t timestamp_ms = (uint32_t)pkt->payload[2] | ((uint32_t)pkt->payload[3] << 8) |
                                ((uint32_t)pkt->payload[4] << 16) |
                                ((uint32_t)pkt->payload[5] << 24);

        if (self->receiver != NULL && self->receiver->on_remote_position_changed != NULL) {
            return self->receiver->on_remote_position_changed(self->receiver->self, position,
                                                              pressed, timestamp_ms);
        }
    } else if (pkt->msg_type == ZMK_SPLIT_MSG_BATTERY_STATE) {
        self->peripheral_battery_pct = pkt->payload[0];
    } else if (pkt->msg_type == ZMK_SPLIT_MSG_LAYER_STATE) {
        self->active_layers = (uint32_t)pkt->payload[0] | ((uint32_t)pkt->payload[1] << 8) |
                              ((uint32_t)pkt->payload[2] << 16) | ((uint32_t)pkt->payload[3] << 24);
    } else if (pkt->msg_type == ZMK_SPLIT_MSG_ACTIVITY_STATE) {
        self->peripheral_active = (pkt->payload[0] != 0);
    }

    return EDGE_OK;
}

void zmk_split_set_connected(zmk_split_app_t *self, bool connected) {
    if (self != NULL) {
        self->connected = connected;
    }
}
