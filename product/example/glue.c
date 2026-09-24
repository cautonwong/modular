#include "dlt645/dlt645.h"
#include "flash/flash.h"
#include "storage.h"

/*
 * Adapter layer (D14): the app owns its consumer-defined port, infra exposes
 * its own concrete API, and the composition root bridges the two here. Keeping
 * dedicated trampolines avoids relying on function-pointer compatibility that
 * the C type system does not actually guarantee (e.g. `const void *` vs
 * `void *` self parameters).
 */
void product_example_make_storage(dlt645_storage_if_t *out, void *flash_state) {
    product_storage_wire(out, flash_state);
}
