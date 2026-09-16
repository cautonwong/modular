#include "dlt645/dlt645.h"
#include "flash/flash.h"

void product_example_make_storage(dlt645_storage_if_t *out, void *flash_state)
{
    if (out == NULL) {
        return;
    }
    *out = (dlt645_storage_if_t){
        .read = flash_read,
        .write = flash_write,
        .self = flash_state,
    };
}
