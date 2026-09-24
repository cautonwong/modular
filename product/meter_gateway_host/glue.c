#include "gateway.h"

#include "dlt645/dlt645.h"
#include "flash/flash.h"
#include "meter_core/meter_core.h"
#include "modbus_slave/modbus_slave.h"
#include "storage.h"
#include "uart/uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- DLT645 storage port over the flash fake --- */

void product_meter_gateway_host_make_storage(dlt645_storage_if_t *out, void *flash_state) {
    product_storage_wire(out, flash_state);
}

/*
 * App-to-app adapter (D78): `modbus_slave` defines `modbus_store_if`; the
 * provider app `meter_core` exposes a concrete API; the composition root adapts
 * one to the other. The apps never include each other.
 */

// cppcheck-suppress constParameterPointer ; store callback signature
static edge_status_t mb_read_holding(void *self, uint16_t addr, uint16_t *value) {
    return meter_core_read_register((const meter_core_t *)self, addr, value);
}

static edge_status_t mb_write_holding(void *self, uint16_t addr, uint16_t value) {
    return meter_core_write_register((meter_core_t *)self, addr, value);
}

// cppcheck-suppress constParameterPointer ; store callback signature
static edge_status_t mb_read_coil(void *self, uint16_t addr, bool *value) {
    return meter_core_read_coil((const meter_core_t *)self, addr, value);
}

static edge_status_t mb_write_coil(void *self, uint16_t addr, bool value) {
    return meter_core_write_coil((meter_core_t *)self, addr, value);
}

/* --- Modbus transport over the UART fake --- */

static edge_status_t mb_transport_write(void *self, const void *buf, size_t len) {
    gateway_state_t *state = (gateway_state_t *)self;
    if (state == NULL)
        return EDGE_EINVAL;
    /* `uart_write` copies `len` bytes into `self` with no notion of its size, so the
     * bound belongs here: the sink is 128 bytes and a 32-register read builds 69. */
    if (len > sizeof(state->uart_tx))
        return EDGE_ENOSPC;
    ++state->uart_writes;
    return uart_write(state->uart_tx, buf, len);
}

void product_meter_gateway_host_make_modbus(modbus_store_if_t *store,
                                            modbus_transport_if_t *transport,
                                            gateway_state_t *state) {
    if (store == NULL || transport == NULL)
        return;
    *store = (modbus_store_if_t){
        .read_coil = mb_read_coil,
        .write_coil = mb_write_coil,
        .read_holding = mb_read_holding,
        .write_holding = mb_write_holding,
        .self = (state != NULL) ? &state->meter : NULL,
    };
    *transport = (modbus_transport_if_t){
        .write = mb_transport_write,
        .self = state,
    };
}
