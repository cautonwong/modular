#ifndef PRODUCT_METER_GATEWAY_HOST_H
#define PRODUCT_METER_GATEWAY_HOST_H

#include "meter_core/meter_core.h"
#include "modbus_slave/modbus_slave.h"

#include <stdint.h>

/* The transmit sink holds the largest frame the protocol can build - 66 PDU bytes
 * plus unit id and CRC is 69, and the slave's own capacity covers it - and the
 * adapter refuses anything larger instead of copying past its end (#170). */
#define GATEWAY_UART_TX_CAPACITY MODBUS_SLAVE_RESPONSE_CAPACITY

/* Caller-owned state for the host dual-protocol product. */
typedef struct gateway_state {
    uint8_t flash[64];
    uint8_t uart_tx[GATEWAY_UART_TX_CAPACITY];
    uint32_t uart_writes;
    meter_core_t meter;
} gateway_state_t;

#endif
