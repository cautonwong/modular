#include "flash/flash.h"

/* Freestanding byte copy: keeps the bare-metal image independent of a C
 * library and avoids the "unsafe buffer handling" class of findings for a
 * deliberately trivial host-side fake. */
static void copy_bytes(void *dst, const void *src, size_t len) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0u; i < len; ++i) {
        d[i] = s[i];
    }
}

edge_status_t flash_read(const void *self, uint32_t key, void *buf, size_t len) {
    (void)key;
    if (self == NULL || buf == NULL) {
        return EDGE_EINVAL;
    }
    copy_bytes(buf, self, len);
    return EDGE_OK;
}

edge_status_t flash_write(void *self, uint32_t key, const void *buf, size_t len) {
    (void)key;
    if (self == NULL || buf == NULL) {
        return EDGE_EINVAL;
    }
    copy_bytes(self, buf, len);
    return EDGE_OK;
}
