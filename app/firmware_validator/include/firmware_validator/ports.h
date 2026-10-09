#ifndef FIRMWARE_VALIDATOR_PORTS_H
#define FIRMWARE_VALIDATOR_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct firmware_validator_port {
    void *self;
    edge_status_t (*read_word)(void *self, uint32_t address, uint32_t *value);
    edge_status_t (*write_word)(void *self, uint32_t address, uint32_t value);
    edge_status_t (*system_reset)(void *self);
} firmware_validator_port_t;

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_VALIDATOR_PORTS_H */
