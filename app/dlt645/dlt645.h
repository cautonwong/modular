#ifndef APP_DLT645_H
#define APP_DLT645_H

#include "edge/module.h"
#include <stddef.h>
#include <stdint.h>

typedef struct dlt645_storage_if {
    int (*read)(void *self, uint32_t key, void *buf, size_t len);
    int (*write)(void *self, uint32_t key, const void *buf, size_t len);
    void *self;
} dlt645_storage_if;

typedef struct dlt645 {
    edge_module_t mod;
    const dlt645_storage_if *storage;
    uint32_t reads;
} dlt645_t;

dlt645_t *dlt645_new(dlt645_t *storage, const dlt645_storage_if *storage_if);

#endif
