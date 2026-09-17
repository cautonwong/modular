#ifndef INFRA_UART_H
#define INFRA_UART_H

#include "edge/module.h"

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

#ifdef __cplusplus
}
#endif

#endif
