#ifndef APP_RELAY_H
#define APP_RELAY_H

#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Consumer-defined port: the relay app only needs a way to drive a channel. */
typedef struct relay_out_if {
    edge_status_t (*set)(void *self, uint8_t channel, bool on);
    void *self;
} relay_out_if_t;

typedef struct relay {
    edge_module_t module;
    const relay_out_if_t *out;
    uint8_t state;
    uint32_t toggles;
    uint32_t last_event;
} relay_t;

void relay_construct(relay_t *self, uint32_t module_id, uint32_t priority,
                     const relay_out_if_t *out);

#ifdef __cplusplus
}
#endif

#endif
