#ifndef BLE_WEATHER_H
#define BLE_WEATHER_H

#include "ble_weather/ports.h"
#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_WEATHER_MAX_FORECAST_DAYS 5u
#define BLE_WEATHER_LOCATION_MAX_LEN 32u

typedef enum ble_weather_icon {
    BLE_WEATHER_ICON_SUN = 0,
    BLE_WEATHER_ICON_CLOUDS_SUN = 1,
    BLE_WEATHER_ICON_CLOUDS = 2,
    BLE_WEATHER_ICON_BROKEN_CLOUDS = 3,
    BLE_WEATHER_ICON_CLOUD_SHOWER_HEAVY = 4,
    BLE_WEATHER_ICON_CLOUD_SUN_RAIN = 5,
    BLE_WEATHER_ICON_THUNDERSTORM = 6,
    BLE_WEATHER_ICON_SNOW = 7,
    BLE_WEATHER_ICON_SMOG = 8,
    BLE_WEATHER_ICON_UNKNOWN = 255
} ble_weather_icon_t;

typedef struct ble_weather_current {
    uint64_t timestamp;
    int16_t temp_raw;     /* in 0.01 deg C */
    int16_t min_temp_raw; /* in 0.01 deg C */
    int16_t max_temp_raw; /* in 0.01 deg C */
    uint8_t icon_id;
    char location[BLE_WEATHER_LOCATION_MAX_LEN + 1u];
    int16_t sunrise; /* minutes from midnight, -1=unknown, -2=not rising */
    int16_t sunset;  /* minutes from midnight, -1=unknown, -2=not setting */
    bool valid;
} ble_weather_current_t;

typedef struct ble_weather_forecast_day {
    int16_t min_temp_raw;
    int16_t max_temp_raw;
    uint8_t icon_id;
    bool valid;
} ble_weather_forecast_day_t;

typedef struct ble_weather_forecast {
    uint64_t timestamp;
    uint8_t nb_days;
    ble_weather_forecast_day_t days[BLE_WEATHER_MAX_FORECAST_DAYS];
    bool valid;
} ble_weather_forecast_t;

typedef struct ble_weather {
    edge_module_t module;
    ble_weather_time_port_t time_port;
    edge_event_sink_t *event_sink;
    ble_weather_current_t current;
    ble_weather_forecast_t forecast;
} ble_weather_t;

void ble_weather_init(ble_weather_t *self, const ble_weather_time_port_t *time_port,
                      edge_event_sink_t *event_sink);

void ble_weather_construct(ble_weather_t *self, uint32_t module_id, uint32_t priority,
                           const ble_weather_time_port_t *time_port, edge_event_sink_t *event_sink);

edge_status_t ble_weather_process_packet(ble_weather_t *self, const uint8_t *data, size_t len);

bool ble_weather_get_current(const ble_weather_t *self, ble_weather_current_t *out_current);
bool ble_weather_get_forecast(const ble_weather_t *self, ble_weather_forecast_t *out_forecast);

bool ble_weather_is_night(const ble_weather_t *self);

int16_t ble_weather_celsius(int16_t raw_temp);
int16_t ble_weather_fahrenheit(int16_t raw_temp);

const edge_module_t *ble_weather_module(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_WEATHER_H */
