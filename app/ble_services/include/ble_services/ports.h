#ifndef APP_BLE_SERVICES_PORTS_H
#define APP_BLE_SERVICES_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Consumer-defined port for BLE GATT server operations.
 */
typedef struct ble_gatt_server_if {
    void *self;
    edge_status_t (*notify)(void *self, uint16_t char_handle, const uint8_t *data, size_t len);
} ble_gatt_server_if_t;

/*
 * Consumer-defined port for updating device local time when CTS write occurs.
 */
typedef struct ble_time_sink_if {
    void *self;
    edge_status_t (*set_time)(void *self, uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                              uint8_t minute, uint8_t second);
} ble_time_sink_if_t;

/*
 * Consumer-defined port for triggering immediate alert (IAS / Find My Watch).
 */
typedef struct ble_alert_sink_if {
    void *self;
    edge_status_t (*on_alert_level)(void *self, uint8_t level);
} ble_alert_sink_if_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_BLE_SERVICES_PORTS_H */
