#ifndef GUARD_BLE_WEATHER_H
#define GUARD_BLE_WEATHER_H

#include "edge/event.h"

edge_status_t ble_weather_init(void *self, void *time_port, edge_event_sink_t *sink);

#endif
