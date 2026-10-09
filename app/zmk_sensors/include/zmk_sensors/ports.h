#ifndef ZMK_SENSORS_PORTS_H
#define ZMK_SENSORS_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zmk_sensors_behavior_if {
    void *self;
    edge_status_t (*invoke_binding)(void *self, uint16_t behavior_id, uint32_t param1,
                                    uint32_t param2, bool pressed, uint32_t timestamp_ms);
} zmk_sensors_behavior_if_t;

#ifdef __cplusplus
}
#endif

#endif /* ZMK_SENSORS_PORTS_H */
