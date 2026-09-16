#include "flash.h"
#include <string.h>

int flash_read(void *self, uint32_t off, void *buf, size_t len) {
    (void)self;
    if (!buf || off != 0u || len > 1u) return -1;
    memset(buf, 0, len);
    return 0;
}

int flash_write(void *self, uint32_t off, const void *buf, size_t len) {
    (void)self; (void)off; (void)buf; (void)len;
    return 0;
}
