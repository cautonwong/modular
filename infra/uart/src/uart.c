#include "uart/uart.h"

/* Freestanding byte copy: keeps the transport fake free of a C library, like
 * the flash fake. */
static void copy_bytes(void *dst, const void *src, size_t len) {
    unsigned char *out = (unsigned char *)dst;
    const unsigned char *in = (const unsigned char *)src;
    for (size_t i = 0u; i < len; ++i) {
        out[i] = in[i];
    }
}

edge_status_t uart_write(void *self, const void *buf, size_t len) {
    if (self == NULL || buf == NULL)
        return EDGE_EINVAL;
    copy_bytes(self, buf, len);
    return EDGE_OK;
}

edge_status_t uart_read(void *self, void *buf, size_t len) {
    if (self == NULL || buf == NULL)
        return EDGE_EINVAL;
    copy_bytes(buf, self, len);
    return EDGE_OK;
}

void uart_ring_init(uart_ring_driver_t *drv, uint8_t *rx_buf, size_t rx_cap, uint8_t *tx_buf,
                    size_t tx_cap) {
    if (drv == NULL) {
        return;
    }
    edge_ring_buffer_init(&drv->rx_rb, rx_buf, rx_cap);
    edge_ring_buffer_init(&drv->tx_rb, tx_buf, tx_cap);
    drv->rx_bytes_total = 0u;
    drv->tx_bytes_total = 0u;
}

edge_status_t uart_ring_rx_isr(uart_ring_driver_t *drv, uint8_t byte) {
    if (drv == NULL) {
        return EDGE_EINVAL;
    }
    edge_status_t st = edge_ring_buffer_write_byte(&drv->rx_rb, byte);
    if (st == EDGE_OK) {
        drv->rx_bytes_total++;
    }
    return st;
}

size_t uart_ring_read(uart_ring_driver_t *drv, uint8_t *out, size_t max_len) {
    if (drv == NULL || out == NULL || max_len == 0u) {
        return 0u;
    }
    return edge_ring_buffer_read(&drv->rx_rb, out, max_len);
}

size_t uart_ring_write(uart_ring_driver_t *drv, const uint8_t *in, size_t len) {
    if (drv == NULL || in == NULL || len == 0u) {
        return 0u;
    }
    size_t written = edge_ring_buffer_write(&drv->tx_rb, in, len);
    drv->tx_bytes_total += (uint32_t)written;
    return written;
}
