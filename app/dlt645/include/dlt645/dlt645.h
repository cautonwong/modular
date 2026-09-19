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

/*
 * D51: assembly-time init and shutdown-time deinit are called by the composition
 * root, not by the scheduler. `dlt645_init` validates the injected port and must
 * be called before the module is handed to sys.
 */
edge_status_t dlt645_init(dlt645_t *self);
edge_status_t dlt645_deinit(dlt645_t *self);
edge_module_t *dlt645_module(dlt645_t *self);

#ifdef __cplusplus
}
#endif

#endif
