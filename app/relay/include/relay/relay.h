#ifndef APP_RELAY_H
#define APP_RELAY_H

#include "edge/desc.h"
#include "edge/module.h"
#include "relay/ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef RELAY_MODULE_ID
#define RELAY_MODULE_ID 0x2000u
#endif

#ifndef EDGE_MOD_RELAY
#define EDGE_MOD_RELAY RELAY_MODULE_ID
#endif

#ifndef RELAY_EVT_CHANGED
#define RELAY_EVT_CHANGED 0x1201u
#endif
#ifndef EDGE_EVT_RELAY_CHANGED
#define EDGE_EVT_RELAY_CHANGED RELAY_EVT_CHANGED
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct relay {
    edge_module_t module;
    const relay_out_if_t *out;
    uint8_t state;
    uint32_t toggles;
    uint32_t last_event;
} relay_t;

void relay_construct(relay_t *self, uint32_t module_id, uint32_t priority,
                     const relay_out_if_t *out);

/* D51: init/deinit are called by the composition root, not by the scheduler. */
edge_status_t relay_init(relay_t *self);
const edge_module_desc_t *relay_descriptor(void);
edge_status_t relay_deinit(relay_t *self);
edge_module_t *relay_module(relay_t *self);

#ifdef __cplusplus
}
#endif

#endif
