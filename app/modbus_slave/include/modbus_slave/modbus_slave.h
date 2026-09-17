#ifndef APP_MODBUS_SLAVE_H
#define APP_MODBUS_SLAVE_H

#include "edge/module.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Consumer-defined ports for a Modbus RTU slave.
 *
 * The app owns these interfaces; a product adapts its infra (UART/flash/...)
 * through glue. No concrete layer is referenced here.
 */
typedef struct modbus_store_if {
    edge_status_t (*read_coil)(void *self, uint16_t addr, bool *value);
    edge_status_t (*write_coil)(void *self, uint16_t addr, bool value);
    edge_status_t (*read_holding)(void *self, uint16_t addr, uint16_t *value);
    edge_status_t (*write_holding)(void *self, uint16_t addr, uint16_t value);
    void *self;
} modbus_store_if_t;

typedef struct modbus_transport_if {
    edge_status_t (*write)(void *self, const void *buf, size_t len);
    void *self;
} modbus_transport_if_t;

/* Bounded subset: reads/writes up to 32 items per request. */
#define MODBUS_SLAVE_MAX_QTY 32u
#define MODBUS_SLAVE_RESPONSE_CAPACITY (MODBUS_SLAVE_MAX_QTY * 2u + 8u)

typedef struct modbus_slave {
    edge_module_t module;
    const modbus_store_if_t *store;
    const modbus_transport_if_t *transport;
    uint8_t unit_id;
    uint32_t requests;
    uint32_t responses;
    uint32_t errors;
    uint32_t poll_count;
    uint32_t last_event;
    uint8_t response[MODBUS_SLAVE_RESPONSE_CAPACITY];
} modbus_slave_t;

void modbus_slave_construct(modbus_slave_t *self, uint32_t module_id, uint32_t priority,
                            uint8_t unit_id, const modbus_store_if_t *store,
                            const modbus_transport_if_t *transport);

/*
 * Feed one complete RTU ADU (address + PDU + CRC).
 *
 * Returns EDGE_EINVAL on bad arguments, EDGE_EIO on CRC/format error,
 * EDGE_ENOENT when the frame targets another unit, EDGE_OK otherwise (including
 * broadcast writes and exception responses).
 */
edge_status_t modbus_slave_feed(modbus_slave_t *self, const uint8_t *frame, size_t len);

#ifdef __cplusplus
}
#endif

#endif
