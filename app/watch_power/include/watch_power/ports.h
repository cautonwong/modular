#ifndef APP_WATCH_POWER_PORTS_H
#define APP_WATCH_POWER_PORTS_H

#include "edge/module.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct watch_power_display_if {
    edge_status_t (*set_brightness)(void *self, uint8_t percent);
    edge_status_t (*sleep)(void *self, bool enable);
    void *self;
} watch_power_display_if_t;

typedef struct watch_power_battery_if {
    edge_status_t (*read_status)(void *self, uint16_t *voltage_mv, uint8_t *percent,
                                 bool *is_charging, bool *is_power_present);
    void *self;
} watch_power_battery_if_t;

#ifdef __cplusplus
}
#endif

#endif
