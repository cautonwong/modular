#ifndef APP_HEART_RATE_PORTS_H
#define APP_HEART_RATE_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ppg_sensor_if {
    edge_status_t (*read_sample)(void *self, uint16_t *out_hrs, uint16_t *out_als);
    edge_status_t (*enable)(void *self, bool enable);
    void *self;
} ppg_sensor_if_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_HEART_RATE_PORTS_H */
