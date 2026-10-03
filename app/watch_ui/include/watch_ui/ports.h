#ifndef WATCH_UI_PORTS_H
#define WATCH_UI_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct watch_ui_display_port {
    void *self;
    edge_status_t (*set_brightness)(void *self, uint8_t level);
    edge_status_t (*clear_screen)(void *self, uint16_t color);
    edge_status_t (*draw_bitmap)(void *self, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                 const uint16_t *pixels);
    edge_status_t (*set_power_mode)(void *self, bool display_on);
} watch_ui_display_port_t;

typedef struct watch_ui_status_data {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t day_of_week;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t battery_percent;
    bool battery_charging;
    bool ble_connected;
    uint8_t heart_rate_bpm;
    uint32_t step_count;
    int16_t weather_temp_c;
} watch_ui_status_data_t;

typedef struct watch_ui_status_port {
    void *self;
    edge_status_t (*get_status_data)(void *self, watch_ui_status_data_t *out_status);
} watch_ui_status_port_t;

#ifdef __cplusplus
}
#endif

#endif /* WATCH_UI_PORTS_H */
