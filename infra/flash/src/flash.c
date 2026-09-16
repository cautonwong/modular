#include "flash/flash.h"

#include <string.h>

edge_status_t flash_read(void *self, uint32_t key, void *buf, size_t len)
{
    (void)key;
    if (self == NULL || buf == NULL) {
        return EDGE_EINVAL;
    }
    memcpy(buf, self, len);
    return EDGE_OK;
}

edge_status_t flash_write(void *self, uint32_t key, const void *buf, size_t len)
{
    (void)key;
    if (self == NULL || buf == NULL) {
        return EDGE_EINVAL;
    }
    memcpy(self, buf, len);
    return EDGE_OK;
}
