#ifndef PRODUCT_METER_GATEWAY_HOST_H
#define PRODUCT_METER_GATEWAY_HOST_H

#include <stdint.h>

/* Caller-owned state for the host dual-protocol product. */
typedef struct gateway_state {
    uint8_t flash[64];
    uint16_t holding[8];
    uint8_t coils[8];
    uint8_t uart_tx[64];
    uint32_t uart_writes;
} gateway_state_t;

#endif
