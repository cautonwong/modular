#ifndef FIRMWARE_VALIDATOR_H
#define FIRMWARE_VALIDATOR_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "ports.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FIRMWARE_VALIDATOR_VALID_BIT_ADDR 0x7BFE8u
#define FIRMWARE_VALIDATOR_VALID_BIT_VAL 1u

typedef struct firmware_validator {
    edge_module_t module;
    const firmware_validator_port_t *port;
    const edge_event_sink_t *event_sink;
} firmware_validator_t;

void firmware_validator_construct(firmware_validator_t *self, uint32_t module_id, uint32_t priority,
                                  const firmware_validator_port_t *port,
                                  const edge_event_sink_t *event_sink);

edge_status_t firmware_validator_init(firmware_validator_t *self,
                                      const firmware_validator_port_t *port,
                                      const edge_event_sink_t *event_sink);

bool firmware_validator_is_validated(const firmware_validator_t *self);
edge_status_t firmware_validator_validate(firmware_validator_t *self);
edge_status_t firmware_validator_reset(firmware_validator_t *self);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_VALIDATOR_H */
