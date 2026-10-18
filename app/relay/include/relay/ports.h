#ifndef APP_RELAY_PORTS_H
#define APP_RELAY_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Consumer-defined port: the relay app only needs a way to drive a channel. */
typedef struct relay_out_if {
    edge_status_t (*set)(void *self, uint8_t channel, bool on);
    void *self;
} relay_out_if_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_RELAY_PORTS_H */
