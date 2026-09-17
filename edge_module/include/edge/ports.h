#ifndef EDGE_PORTS_H
#define EDGE_PORTS_H

#include "clock.h"
#include "errors.h"
#include "log.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical narrow port shapes (D22/D23/D24, ADR 20.3.4).
 *
 * These are optional conventions: an app still defines the interface it
 * consumes (D14). Using these shapes keeps ports small and interchangeable
 * across apps and products.
 */
typedef struct edge_byte_reader {
    edge_status_t (*read)(void *self, void *buf, size_t len);
    void *self;
} edge_byte_reader_t;

typedef struct edge_byte_writer {
    edge_status_t (*write)(void *self, const void *buf, size_t len);
    void *self;
} edge_byte_writer_t;

typedef struct edge_storage_kv {
    edge_status_t (*read)(void *self, uint32_t key, void *buf, size_t len);
    edge_status_t (*write)(void *self, uint32_t key, const void *buf, size_t len);
    void *self;
} edge_storage_kv_t;

/* Non-blocking byte-stream transport (e.g. UART). RX facts are delivered as
 * events through the injected sink, not through this port. */
typedef struct edge_uart_port {
    edge_status_t (*write)(void *self, const void *buf, size_t len);
    void *self;
} edge_uart_port_t;

/* Discrete digital I/O: level set and level read. IRQ-driven changes are events. */
typedef struct edge_gpio_port {
    edge_status_t (*write)(void *self, uint8_t channel, bool level);
    edge_status_t (*read)(void *self, uint8_t channel, bool *level);
    void *self;
} edge_gpio_port_t;

/* clock and log port shapes are owned by their own headers and re-exported here. */
typedef edge_clock_port_t edge_clock_t;
typedef edge_log_port_t edge_log_t;

#ifdef __cplusplus
}
#endif

#endif
