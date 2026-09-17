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

// cppcheck-suppress constParameterPointer ; adapted to a void* port function pointer
edge_status_t uart_read(void *self, void *buf, size_t len) {
    if (self == NULL || buf == NULL)
        return EDGE_EINVAL;
    copy_bytes(buf, self, len);
    return EDGE_OK;
}
