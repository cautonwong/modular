#ifndef INFRA_UART_H
#define INFRA_UART_H

#include "edge/module.h"
#include "edge/ring_buffer.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Concrete infrastructure API for a byte-stream transport.
 *
 * Host-testable fake: `self` points at a caller-owned byte buffer, mirroring
 * `flash_read`/`flash_write`. Products adapt it to `edge_uart_port_t` (or their
 * own consumer-defined port).
 */
edge_status_t uart_write(void *self, const void *buf, size_t len);
edge_status_t uart_read(void *self, void *buf, size_t len);

/* Data plane ring-buffered UART transport (ADR-007, IRQ-DATA-002) */
typedef struct uart_ring_driver {
    edge_ring_buffer_t rx_rb;
    edge_ring_buffer_t tx_rb;
    uint32_t rx_bytes_total;
    uint32_t tx_bytes_total;
} uart_ring_driver_t;

void uart_ring_init(uart_ring_driver_t *drv, uint8_t *rx_buf, size_t rx_cap, uint8_t *tx_buf,
                    size_t tx_cap);
edge_status_t uart_ring_rx_isr(uart_ring_driver_t *drv, uint8_t byte);
size_t uart_ring_read(uart_ring_driver_t *drv, uint8_t *out, size_t max_len);
size_t uart_ring_write(uart_ring_driver_t *drv, const uint8_t *in, size_t len);

#ifdef __cplusplus
}
#endif

#endif
