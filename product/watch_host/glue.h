#ifndef PRODUCT_WATCH_HOST_GLUE_H
#define PRODUCT_WATCH_HOST_GLUE_H

#include "ble_services/ble_services.h"
#include "button_handler/button_handler.h"
#include "heart_rate/heart_rate.h"
#include "step_counter/step_counter.h"
#include "touch_gesture/touch_gesture.h"
#include "watch_power/watch_power.h"
#include "watch_time/watch_time.h"

#include "battery_adc/battery_adc.h"
#include "bma421/bma421.h"
#include "cst816s/cst816s.h"
#include "flash_spi/flash_spi.h"
#include "haptic/haptic.h"
#include "hrs3300/hrs3300.h"
#include "st7789/st7789.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct watch_host_glue_state {
    /* Touch simulation state */
    bool touch_pressed;
    uint16_t touch_x;
    uint16_t touch_y;
    uint8_t touch_gesture;

    /* Display state */
    uint8_t display_brightness;
    bool display_sleep;
    uint16_t display_window_x0;
    uint16_t display_window_y0;
    uint16_t display_window_x1;
    uint16_t display_window_y1;

    /* PPG / Heart rate simulation state */
    uint16_t ppg_sample_val;
    uint8_t ppg_read_count;

    /* IMU simulation state */
    bma421_accel_t imu_sample;

    /* RTC state */
    uint32_t rtc_ticks;

    /* Haptic motor state */
    haptic_pattern_t last_haptic_pattern;
    uint32_t haptic_trigger_count;

    /* BLE GATT state */
    uint16_t last_ble_handle;
    uint8_t last_ble_data[16];
    size_t last_ble_len;
    uint32_t ble_notify_count;

    /* References to apps */
    watch_time_t *time_app;
} watch_host_glue_state_t;

void watch_host_make_touch_port(touch_input_if_t *out, watch_host_glue_state_t *state);
void watch_host_make_display_port(watch_power_display_if_t *out, watch_host_glue_state_t *state);
void watch_host_make_ppg_port(ppg_sensor_if_t *out, watch_host_glue_state_t *state);
void watch_host_make_imu_port(imu_sensor_if_t *out, watch_host_glue_state_t *state);
void watch_host_make_rtc_port(rtc_clock_if_t *out, watch_host_glue_state_t *state);
void watch_host_make_gatt_port(ble_gatt_server_if_t *out, watch_host_glue_state_t *state);
void watch_host_make_time_sink_port(ble_time_sink_if_t *out, watch_host_glue_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* PRODUCT_WATCH_HOST_GLUE_H */
