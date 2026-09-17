#include "dlt645/dlt645.h"
#include "flash/flash.h"

#include <stddef.h>
#include <stdint.h>

// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t storage_read(void *self, uint32_t key, void *buf, size_t len) {
    return flash_read(self, key, buf, len);
}

static edge_status_t storage_write(void *self, uint32_t key, const void *buf, size_t len) {
    return flash_write(self, key, buf, len);
}

void product_riscv_meter_make_storage(dlt645_storage_if_t *out, void *flash_state) {
    if (out == NULL)
        return;
    *out = (dlt645_storage_if_t){
        .read = storage_read,
        .write = storage_write,
        .self = flash_state,
    };
}
