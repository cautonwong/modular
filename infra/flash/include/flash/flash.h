#ifndef INFRA_FLASH_H
#define INFRA_FLASH_H

#include "edge/module.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

edge_status_t flash_read(void *self, uint32_t key, void *buf, size_t len);
edge_status_t flash_write(void *self, uint32_t key, const void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif
