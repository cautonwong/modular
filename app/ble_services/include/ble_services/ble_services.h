#ifndef APP_BLE_SERVICES_H
#define APP_BLE_SERVICES_H

#include "ble_services/ports.h"
#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard BLE GATT UUIDs (16-bit) */
#define BLE_UUID_SVC_CURRENT_TIME 0x1805u
#define BLE_UUID_CHR_CURRENT_TIME 0x2A2Bu
#define BLE_UUID_CHR_LOCAL_TIME_INFO 0x2A0Fu

#define BLE_UUID_SVC_HEART_RATE 0x180Du
#define BLE_UUID_CHR_HEART_RATE_MEASUREMENT 0x2A37u

#define BLE_UUID_SVC_BATTERY 0x180Fu
#define BLE_UUID_CHR_BATTERY_LEVEL 0x2A19u

#define BLE_UUID_SVC_IMMEDIATE_ALERT 0x1802u
#define BLE_UUID_CHR_ALERT_LEVEL 0x2A06u

#define BLE_UUID_SVC_DEVICE_INFO 0x180Au
#define BLE_UUID_CHR_MANUFACTURER_NAME 0x2A29u
#define BLE_UUID_CHR_MODEL_NUMBER 0x2A24u
#define BLE_UUID_CHR_SERIAL_NUMBER 0x2A25u
#define BLE_UUID_CHR_FIRMWARE_REVISION 0x2A26u
#define BLE_UUID_CHR_HARDWARE_REVISION 0x2A27u
#define BLE_UUID_CHR_SOFTWARE_REVISION 0x2A28u

/* CTS Current Time Data (10 bytes packed per Bluetooth SIG) */
typedef struct cts_datetime {
    uint16_t year;
    uint8_t month;        /* 1..12 */
    uint8_t day_of_month; /* 1..31 */
    uint8_t hours;        /* 0..23 */
    uint8_t minutes;      /* 0..59 */
    uint8_t seconds;      /* 0..59 */
    uint8_t day_of_week;  /* 1 = Mon .. 7 = Sun (0 = unknown) */
    uint8_t fractions256; /* 1/256 fractions of a second */
    uint8_t adjust_reason;
} cts_datetime_t;

/* CTS Local Time Info (2 bytes packed) */
typedef struct cts_timezone {
    int8_t timezone;   /* UTC offset in 15-minute increments (-48 to +56) */
    int8_t dst_offset; /* 0 = standard, 2 = +0.5h, 4 = +1h, 8 = +2h */
} cts_timezone_t;

/*
 * Pure protocol encoders / decoders for host & target use.
 */
edge_status_t cts_decode_datetime(const uint8_t *buf, size_t len, cts_datetime_t *out_dt);
edge_status_t cts_encode_datetime(const cts_datetime_t *dt, uint8_t *out_buf, size_t max_len,
                                  size_t *out_len);

edge_status_t cts_decode_local_time(const uint8_t *buf, size_t len, cts_timezone_t *out_tz);
edge_status_t cts_encode_local_time(const cts_timezone_t *tz, uint8_t *out_buf, size_t max_len,
                                    size_t *out_len);

edge_status_t hrs_encode_measurement(uint8_t bpm, uint8_t *out_buf, size_t max_len,
                                     size_t *out_len);

edge_status_t bas_encode_battery_level(uint8_t percent, uint8_t *out_buf, size_t max_len,
                                       size_t *out_len);

/*
 * BLE Services Application struct.
 */
typedef struct ble_services_app {
    edge_module_t module;
    const ble_gatt_server_if_t *gatt_server;
    const ble_time_sink_if_t *time_sink;

    /* Handle definitions assigned at registration */
    uint16_t hrs_val_handle;
    uint16_t bas_val_handle;
    uint16_t cts_time_val_handle;
    uint16_t cts_local_time_val_handle;
    uint16_t ias_alert_val_handle;
    uint16_t dis_mfr_val_handle;
    uint16_t dis_model_val_handle;
    uint16_t dis_serial_val_handle;
    uint16_t dis_fw_val_handle;
    uint16_t dis_hw_val_handle;
    uint16_t dis_sw_val_handle;

    const ble_alert_sink_if_t *alert_sink;

    /* Connection & notification state */
    bool is_connected;
    bool hrs_notify_enabled;
    bool bas_notify_enabled;

    /* Cached sensor values */
    uint8_t current_bpm;
    uint8_t battery_percent;
    uint8_t current_alert_level;
    cts_datetime_t current_dt;
    cts_timezone_t current_tz;
} ble_services_app_t;

void ble_services_construct(ble_services_app_t *self, uint32_t module_id, uint32_t priority,
                            const ble_gatt_server_if_t *gatt_server,
                            const ble_time_sink_if_t *time_sink);

void ble_services_set_alert_sink(ble_services_app_t *self, const ble_alert_sink_if_t *alert_sink);

edge_status_t ble_services_init(ble_services_app_t *self);
edge_status_t ble_services_shutdown(ble_services_app_t *self);

void ble_services_set_connected(ble_services_app_t *self, bool connected);
void ble_services_set_hrs_notify(ble_services_app_t *self, bool enabled);
void ble_services_set_bas_notify(ble_services_app_t *self, bool enabled);

edge_status_t ble_services_handle_gatt_read(ble_services_app_t *self, uint16_t attr_handle,
                                            uint8_t *out_buf, size_t max_len, size_t *out_len);

edge_status_t ble_services_handle_gatt_write(ble_services_app_t *self, uint16_t attr_handle,
                                             const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* APP_BLE_SERVICES_H */
