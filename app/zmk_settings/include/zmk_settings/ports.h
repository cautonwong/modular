#ifndef APP_ZMK_SETTINGS_PORTS_H
#define APP_ZMK_SETTINGS_PORTS_H

#include "edge/errors.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zmk_settings_storage_if {
    void *self;
    edge_status_t (*read)(void *self, uint32_t offset, uint8_t *buf, size_t len);
    edge_status_t (*write)(void *self, uint32_t offset, const uint8_t *buf, size_t len);
    edge_status_t (*erase)(void *self, uint32_t offset, size_t len);
} zmk_settings_storage_if_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_ZMK_SETTINGS_PORTS_H */
