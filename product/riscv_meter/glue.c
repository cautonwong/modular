#include "dlt645/dlt645.h"
#include "flash/flash.h"
#include "storage.h"

#include <stddef.h>
#include <stdint.h>

void product_riscv_meter_make_storage(dlt645_storage_if_t *out, void *flash_state) {
    product_storage_wire(out, flash_state);
}
