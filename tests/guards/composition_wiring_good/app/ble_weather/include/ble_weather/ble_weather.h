#ifndef GUARD_BLE_WEATHER_H
#define GUARD_BLE_WEATHER_H

#include "edge/event.h"

typedef struct ble_weather_guard ble_weather_guard_t;
typedef struct ble_weather_time_guard ble_weather_time_guard_t;

edge_status_t ble_weather_init(ble_weather_guard_t *self, ble_weather_time_guard_t *time_port,
                               edge_event_sink_t *sink);

#endif
