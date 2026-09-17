#include "modbus_slave/modbus_slave.h"

#include "edge/event.h"
#include "edge/events.h"

#define MODBUS_FC_READ_COILS 0x01u
#define MODBUS_FC_READ_HOLDING 0x03u
#define MODBUS_FC_WRITE_SINGLE_COIL 0x05u
#define MODBUS_FC_WRITE_SINGLE_REGISTER 0x06u
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS 0x10u

#define MODBUS_EXC_ILLEGAL_FUNCTION 0x01u
#define MODBUS_EXC_ILLEGAL_ADDRESS 0x02u
#define MODBUS_EXC_ILLEGAL_VALUE 0x03u
#define MODBUS_EXC_DEVICE_FAILURE 0x04u

static uint16_t read_u16(const uint8_t *frame, size_t offset) {
    return (uint16_t)(((uint16_t)frame[offset] << 8) | (uint16_t)frame[offset + 1u]);
}

static uint16_t crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0u; i < len; ++i) {
        crc ^= (uint16_t)data[i];
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            if ((crc & 1u) != 0u)
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            else
                crc >>= 1;
        }
    }
    return crc;
}

static uint8_t exception_for(edge_status_t rc) {
    return (rc == EDGE_ENOENT) ? MODBUS_EXC_ILLEGAL_ADDRESS : MODBUS_EXC_DEVICE_FAILURE;
}

static edge_status_t send_pdu(modbus_slave_t *self, const uint8_t *pdu, size_t pdu_len) {
    if (pdu_len + 3u > sizeof(self->response))
        return EDGE_ENOSPC;
    self->response[0] = self->unit_id;
    for (size_t i = 0u; i < pdu_len; ++i)
        self->response[1u + i] = pdu[i];
    const uint16_t crc = crc16(self->response, pdu_len + 1u);
    self->response[pdu_len + 1u] = (uint8_t)(crc & 0xFFu);
    self->response[pdu_len + 2u] = (uint8_t)(crc >> 8);
    ++self->responses;
    return self->transport->write(self->transport->self, self->response, pdu_len + 3u);
}

static edge_status_t send_exception(modbus_slave_t *self, uint8_t function, uint8_t code) {
    const uint8_t pdu[2] = {(uint8_t)(function | 0x80u), code};
    ++self->errors;
    return send_pdu(self, pdu, sizeof(pdu));
}

static edge_status_t handle_read_coils(modbus_slave_t *self, const uint8_t *frame, size_t len,
                                       bool broadcast) {
    if (len != 8u)
        return send_exception(self, MODBUS_FC_READ_COILS, MODBUS_EXC_ILLEGAL_VALUE);
    const uint16_t start = read_u16(frame, 2u);
    const uint16_t qty = read_u16(frame, 4u);
    if (qty < 1u || qty > MODBUS_SLAVE_MAX_QTY)
        return send_exception(self, MODBUS_FC_READ_COILS, MODBUS_EXC_ILLEGAL_VALUE);

    uint8_t pdu[2u + (MODBUS_SLAVE_MAX_QTY + 7u) / 8u];
    const uint8_t byte_count = (uint8_t)((qty + 7u) / 8u);
    pdu[0] = MODBUS_FC_READ_COILS;
    pdu[1] = byte_count;
    for (uint8_t i = 0u; i < byte_count; ++i)
        pdu[2u + i] = 0u;

    for (uint16_t i = 0u; i < qty; ++i) {
        bool value = false;
        const edge_status_t rc =
            self->store->read_coil(self->store->self, (uint16_t)(start + i), &value);
        if (rc < 0)
            return send_exception(self, MODBUS_FC_READ_COILS, exception_for(rc));
        if (value)
            pdu[2u + (i / 8u)] = (uint8_t)(pdu[2u + (i / 8u)] | (uint8_t)(1u << (i % 8u)));
    }
    if (broadcast)
        return EDGE_OK;
    return send_pdu(self, pdu, (size_t)(2u + byte_count));
}

static edge_status_t handle_read_holding(modbus_slave_t *self, const uint8_t *frame, size_t len,
                                         bool broadcast) {
    if (len != 8u)
        return send_exception(self, MODBUS_FC_READ_HOLDING, MODBUS_EXC_ILLEGAL_VALUE);
    const uint16_t start = read_u16(frame, 2u);
    const uint16_t qty = read_u16(frame, 4u);
    if (qty < 1u || qty > MODBUS_SLAVE_MAX_QTY)
        return send_exception(self, MODBUS_FC_READ_HOLDING, MODBUS_EXC_ILLEGAL_VALUE);

    uint8_t pdu[2u + MODBUS_SLAVE_MAX_QTY * 2u];
    pdu[0] = MODBUS_FC_READ_HOLDING;
    pdu[1] = (uint8_t)(qty * 2u);
    for (uint16_t i = 0u; i < qty; ++i) {
        uint16_t value = 0u;
        const edge_status_t rc =
            self->store->read_holding(self->store->self, (uint16_t)(start + i), &value);
        if (rc < 0)
            return send_exception(self, MODBUS_FC_READ_HOLDING, exception_for(rc));
        pdu[2u + (size_t)(i * 2u)] = (uint8_t)(value >> 8);
        pdu[3u + (size_t)(i * 2u)] = (uint8_t)(value & 0xFFu);
    }
    if (broadcast)
        return EDGE_OK;
    return send_pdu(self, pdu, (size_t)(2u + qty * 2u));
}

static edge_status_t handle_write_single_coil(modbus_slave_t *self, const uint8_t *frame,
                                              size_t len, bool broadcast) {
    if (len != 8u)
        return send_exception(self, MODBUS_FC_WRITE_SINGLE_COIL, MODBUS_EXC_ILLEGAL_VALUE);
    const uint16_t addr = read_u16(frame, 2u);
    const uint16_t value = read_u16(frame, 4u);
    if (value != 0x0000u && value != 0xFF00u)
        return send_exception(self, MODBUS_FC_WRITE_SINGLE_COIL, MODBUS_EXC_ILLEGAL_VALUE);

    const edge_status_t rc = self->store->write_coil(self->store->self, addr, value == 0xFF00u);
    if (rc < 0)
        return send_exception(self, MODBUS_FC_WRITE_SINGLE_COIL, exception_for(rc));
    if (broadcast)
        return EDGE_OK;

    const uint8_t pdu[5] = {MODBUS_FC_WRITE_SINGLE_COIL, (uint8_t)(addr >> 8),
                            (uint8_t)(addr & 0xFFu), (uint8_t)(value >> 8),
                            (uint8_t)(value & 0xFFu)};
    return send_pdu(self, pdu, sizeof(pdu));
}

static edge_status_t handle_write_single_register(modbus_slave_t *self, const uint8_t *frame,
                                                  size_t len, bool broadcast) {
    if (len != 8u)
        return send_exception(self, MODBUS_FC_WRITE_SINGLE_REGISTER, MODBUS_EXC_ILLEGAL_VALUE);
    const uint16_t addr = read_u16(frame, 2u);
    const uint16_t value = read_u16(frame, 4u);
    const edge_status_t rc = self->store->write_holding(self->store->self, addr, value);
    if (rc < 0)
        return send_exception(self, MODBUS_FC_WRITE_SINGLE_REGISTER, exception_for(rc));
    if (broadcast)
        return EDGE_OK;

    const uint8_t pdu[5] = {MODBUS_FC_WRITE_SINGLE_REGISTER, (uint8_t)(addr >> 8),
                            (uint8_t)(addr & 0xFFu), (uint8_t)(value >> 8),
                            (uint8_t)(value & 0xFFu)};
    return send_pdu(self, pdu, sizeof(pdu));
}

static edge_status_t handle_write_multiple_registers(modbus_slave_t *self, const uint8_t *frame,
                                                     size_t len, bool broadcast) {
    const uint8_t function = MODBUS_FC_WRITE_MULTIPLE_REGISTERS;
    if (len < 11u)
        return send_exception(self, function, MODBUS_EXC_ILLEGAL_VALUE);
    const uint16_t start = read_u16(frame, 2u);
    const uint16_t qty = read_u16(frame, 4u);
    const uint8_t byte_count = frame[6];
    if (qty < 1u || qty > MODBUS_SLAVE_MAX_QTY || byte_count != qty * 2u ||
        len != (size_t)(9u + byte_count))
        return send_exception(self, function, MODBUS_EXC_ILLEGAL_VALUE);

    for (uint16_t i = 0u; i < qty; ++i) {
        const uint16_t value = read_u16(frame, 7u + (size_t)(i * 2u));
        const edge_status_t rc =
            self->store->write_holding(self->store->self, (uint16_t)(start + i), value);
        if (rc < 0)
            return send_exception(self, function, exception_for(rc));
    }
    if (broadcast)
        return EDGE_OK;

    const uint8_t pdu[5] = {function, (uint8_t)(start >> 8), (uint8_t)(start & 0xFFu),
                            (uint8_t)(qty >> 8), (uint8_t)(qty & 0xFFu)};
    return send_pdu(self, pdu, sizeof(pdu));
}

edge_status_t modbus_slave_feed(modbus_slave_t *self, const uint8_t *frame, size_t len) {
    if (self == NULL || frame == NULL)
        return EDGE_EINVAL;
    if (self->store == NULL || self->transport == NULL || self->transport->write == NULL)
        return EDGE_EINVAL;
    if (len < 4u || len > 256u) {
        ++self->errors;
        return EDGE_EIO;
    }

    const uint16_t expected =
        (uint16_t)((uint16_t)frame[len - 2u] | ((uint16_t)frame[len - 1u] << 8));
    if (crc16(frame, len - 2u) != expected) {
        ++self->errors;
        return EDGE_EIO;
    }

    const uint8_t unit = frame[0];
    if (unit != self->unit_id && unit != 0u)
        return EDGE_ENOENT;
    ++self->requests;

    const uint8_t function = frame[1];
    const bool broadcast = (unit == 0u);
    switch (function) {
    case MODBUS_FC_READ_COILS:
        return handle_read_coils(self, frame, len, broadcast);
    case MODBUS_FC_READ_HOLDING:
        return handle_read_holding(self, frame, len, broadcast);
    case MODBUS_FC_WRITE_SINGLE_COIL:
        return handle_write_single_coil(self, frame, len, broadcast);
    case MODBUS_FC_WRITE_SINGLE_REGISTER:
        return handle_write_single_register(self, frame, len, broadcast);
    case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
        return handle_write_multiple_registers(self, frame, len, broadcast);
    default:
        if (broadcast)
            return EDGE_OK;
        return send_exception(self, function, MODBUS_EXC_ILLEGAL_FUNCTION);
    }
}

static edge_status_t modbus_slave_init(edge_module_t *module) {
    modbus_slave_t *self = (modbus_slave_t *)edge_module_data(module);
    if (self == NULL || self->store == NULL || self->transport == NULL ||
        self->transport->write == NULL || self->store->read_holding == NULL ||
        self->store->write_holding == NULL || self->store->read_coil == NULL ||
        self->store->write_coil == NULL)
        return EDGE_EINVAL;
    self->requests = 0u;
    self->responses = 0u;
    self->errors = 0u;
    self->poll_count = 0u;
    self->last_event = 0u;
    return EDGE_OK;
}

static edge_status_t modbus_slave_poll(edge_module_t *module) {
    modbus_slave_t *self = (modbus_slave_t *)edge_module_data(module);
    if (self == NULL)
        return EDGE_EINVAL;
    ++self->poll_count;
    return EDGE_OK;
}

static edge_status_t modbus_slave_on_event(edge_module_t *module, const edge_event_t *event) {
    modbus_slave_t *self = (modbus_slave_t *)edge_module_data(module);
    if (self == NULL || event == NULL)
        return EDGE_EINVAL;
    self->last_event = event->id;
    return EDGE_OK;
}

static edge_status_t modbus_slave_power_off(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t modbus_slave_deinit(edge_module_t *module) {
    modbus_slave_t *self = (modbus_slave_t *)edge_module_data(module);
    if (self == NULL)
        return EDGE_EINVAL;
    self->store = NULL;
    self->transport = NULL;
    return EDGE_OK;
}

void modbus_slave_construct(modbus_slave_t *self, uint32_t module_id, uint32_t priority,
                            uint8_t unit_id, const modbus_store_if_t *store,
                            const modbus_transport_if_t *transport) {
    if (self == NULL)
        return;
    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 1u,
        .budget = 0u,
        .next_due = 0u,
        .init = modbus_slave_init,
        .poll = modbus_slave_poll,
        .on_event = modbus_slave_on_event,
        .power_off = modbus_slave_power_off,
        .deinit = modbus_slave_deinit,
        .private_data = self,
    };
    self->store = store;
    self->transport = transport;
    self->unit_id = unit_id;
    self->requests = 0u;
    self->responses = 0u;
    self->errors = 0u;
    self->poll_count = 0u;
    self->last_event = 0u;
}
