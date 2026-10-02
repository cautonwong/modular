#ifndef BLE_WEATHER_PORTS_H
#define BLE_WEATHER_PORTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ble_weather_time_port {
    void *self;
    uint64_t (*get_timestamp_sec)(void *self);
    uint32_t (*get_minute_of_day)(void *self); /* 0..1439 */
} ble_weather_time_port_t;

#ifdef __cplusplus
}
#endif

#endif /* BLE_WEATHER_PORTS_H */
