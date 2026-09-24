#ifndef PRODUCT_COMMON_STORAGE_H
#define PRODUCT_COMMON_STORAGE_H

/*
 * The one storage adapter every product wires: a `dlt645_storage_if_t` over the flash
 * driver. Each product still owns *that* it wires a storage backend - which is why the
 * per-product `glue.c` keeps its own entry point, and why `edge_add_product` still
 * requires the file - but the adapter itself is the same twelve lines in all ten of them,
 * so it lives here once.
 */
#include "dlt645/dlt645.h"

void product_storage_wire(dlt645_storage_if_t *out, void *flash_state);

#endif
