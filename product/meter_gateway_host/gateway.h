#ifndef PRODUCT_METER_GATEWAY_HOST_H
#define PRODUCT_METER_GATEWAY_HOST_H

#include "meter_core/meter_core.h"

#include <stdint.h>

/* Caller-owned state for the host dual-protocol product. */
typedef struct gateway_state {
    uint8_t flash[64];
    uint8_t uart_tx[64];
    uint32_t uart_writes;
    meter_core_t meter;
} gateway_state_t;

#endif
