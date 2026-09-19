#ifndef APP_METER_CORE_H
#define APP_METER_CORE_H

#include "edge/module.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Provider app: metering core.
 *
 * It exposes a concrete API (registers/coils and a pulse accumulator). It is
 * NOT converted into events: a consumer app defines the interface it needs and
 * the product glue adapts this API to it (D78).
 */
#define METER_CORE_REGISTER_COUNT 8u
#define METER_CORE_COIL_COUNT 8u

typedef struct meter_core {
    edge_module_t module;
    uint16_t registers[METER_CORE_REGISTER_COUNT];
    uint8_t coils[METER_CORE_COIL_COUNT];
    uint32_t polls;
    uint32_t pulses;
    uint32_t last_event;
} meter_core_t;

void meter_core_construct(meter_core_t *self, uint32_t module_id, uint32_t priority);

/* D51: init/deinit are called by the composition root, not by the scheduler. */
edge_status_t meter_core_init(meter_core_t *self);
edge_status_t meter_core_deinit(meter_core_t *self);
edge_module_t *meter_core_module(meter_core_t *self);

/* Concrete provider API, consumed by other apps through product glue. */
edge_status_t meter_core_read_register(const meter_core_t *self, uint16_t addr, uint16_t *value);
edge_status_t meter_core_write_register(meter_core_t *self, uint16_t addr, uint16_t value);
edge_status_t meter_core_read_coil(const meter_core_t *self, uint16_t addr, bool *value);
edge_status_t meter_core_write_coil(meter_core_t *self, uint16_t addr, bool value);
void meter_core_pulse(meter_core_t *self, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
