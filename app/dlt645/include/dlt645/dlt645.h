#ifndef APP_DLT645_H
#define APP_DLT645_H

#include "edge/module.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dlt645_storage_if {
    edge_status_t (*read)(void *self, uint32_t key, void *buf, size_t len);
    edge_status_t (*write)(void *self, uint32_t key, const void *buf, size_t len);
    void *self;
} dlt645_storage_if_t;

typedef struct dlt645 {
    edge_module_t module;
    const dlt645_storage_if_t *storage;
    uint32_t poll_count;
    uint32_t last_event;
} dlt645_t;

void dlt645_construct(dlt645_t *self, uint32_t module_id, uint32_t priority,
                      const dlt645_storage_if_t *storage);

#ifdef __cplusplus
}
#endif

#endif
