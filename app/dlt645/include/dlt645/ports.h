#ifndef APP_DLT645_PORTS_H
#define APP_DLT645_PORTS_H

#include "edge/errors.h"
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

#ifdef __cplusplus
}
#endif

#endif /* APP_DLT645_PORTS_H */
