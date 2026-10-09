#ifndef PRODUCT_PINETIME_GLUE_H
#define PRODUCT_PINETIME_GLUE_H

#include "alarm/alarm.h"
#include "ble_dfu/ble_dfu.h"
#include "ble_fs/ble_fs.h"
#include "ble_motion/ble_motion.h"
#include "ble_music/ble_music.h"
#include "ble_nav/ble_nav.h"
#include "ble_notifications/ble_notifications.h"
#include "ble_passkey/ble_passkey.h"
#include "ble_services/ble_services.h"
#include "ble_weather/ble_weather.h"
#include "button_handler/button_handler.h"
#include "calculator/calculator.h"
#include "dice/dice.h"
#include "firmware_validator/firmware_validator.h"
#include "flashlight/flashlight.h"
#include "game_paddle/game_paddle.h"
#include "game_twos/game_twos.h"
#include "heart_rate/heart_rate.h"
#include "metronome/metronome.h"
#include "paint/paint.h"
#include "pinetime/board.h"
#include "step_counter/step_counter.h"
#include "stopwatch/stopwatch.h"
#include "timer/timer.h"
#include "touch_gesture/touch_gesture.h"
#include "watch_power/watch_power.h"
#include "watch_settings/watch_settings.h"
#include "watch_time/watch_time.h"
#include "watch_ui/watch_ui.h"

#include "battery_adc/battery_adc.h"
#include "bma421/bma421.h"
#include "cst816s/cst816s.h"
#include "flash_spi/flash_spi.h"
#include "haptic/haptic.h"
#include "hrs3300/hrs3300.h"
#include "rle/rle.h"
#include "st7789/st7789.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pinetime_glue_state {
    /* Touch & Display state */
    bool touch_pressed;
    uint16_t touch_x;
    uint16_t touch_y;
    uint8_t touch_gesture;
    uint8_t display_brightness;
    bool display_on;
    bool screen_white;
    bool wake_locked;

    /* Sensor values */
    uint16_t ppg_sample;
    bma421_accel_t imu_sample;
    uint16_t battery_mv;
    uint8_t battery_pct;
    bool battery_charging;
    bool battery_power_present;

    /* Time & RTC */
    uint32_t rtc_ticks;

    /* Storage buffers */
    alarm_settings_t alarm_data;
    watch_settings_data_t settings_data;
    uint8_t flash_storage[4096];
    uint32_t validator_word;

    /* BLE state */
    uint16_t last_gatt_handle;
    uint8_t last_gatt_data[64];
    size_t last_gatt_len;
    uint32_t gatt_notify_count;

    /* App references */
    watch_time_t *time_app;
    step_counter_t *step_app;
    heart_rate_t *hr_app;
} pinetime_glue_state_t;

void pinetime_glue_init(pinetime_glue_state_t *state);

void pinetime_make_touch_port(touch_input_if_t *out, pinetime_glue_state_t *state);
void pinetime_make_display_power_port(watch_power_display_if_t *out, pinetime_glue_state_t *state);
void pinetime_make_battery_power_port(watch_power_battery_if_t *out, pinetime_glue_state_t *state);
void pinetime_make_ppg_port(ppg_sensor_if_t *out, pinetime_glue_state_t *state);
void pinetime_make_imu_port(imu_sensor_if_t *out, pinetime_glue_state_t *state);
void pinetime_make_rtc_port(rtc_clock_if_t *out, pinetime_glue_state_t *state);
void pinetime_make_gatt_port(ble_gatt_server_if_t *out, pinetime_glue_state_t *state);
void pinetime_make_time_sink_port(ble_time_sink_if_t *out, pinetime_glue_state_t *state);

void pinetime_make_alarm_storage_port(alarm_storage_if_t *out, pinetime_glue_state_t *state);
void pinetime_make_alarm_alert_port(alarm_alert_if_t *out, pinetime_glue_state_t *state);
void pinetime_make_stopwatch_clock_port(stopwatch_clock_if_t *out, pinetime_glue_state_t *state);
void pinetime_make_timer_clock_port(timer_clock_if_t *out, pinetime_glue_state_t *state);

void pinetime_make_timer_alert_port(timer_alert_if_t *out, pinetime_glue_state_t *state);
void pinetime_make_settings_store_port(watch_settings_store_if_t *out,
                                       pinetime_glue_state_t *state);

void pinetime_make_weather_time_port(ble_weather_time_port_t *out, pinetime_glue_state_t *state);
void pinetime_make_music_transport_port(ble_music_transport_port_t *out,
                                        pinetime_glue_state_t *state);
void pinetime_make_notif_call_port(ble_notifications_call_port_t *out,
                                   pinetime_glue_state_t *state);
void pinetime_make_motion_notify_port(ble_motion_notify_port_t *out, pinetime_glue_state_t *state);

void pinetime_make_fs_tx_port(ble_fs_tx_port_t *out, pinetime_glue_state_t *state);
void pinetime_make_fs_storage_port(ble_fs_storage_port_t *out, pinetime_glue_state_t *state);

void pinetime_make_dfu_flash_port(ble_dfu_flash_port_t *out, pinetime_glue_state_t *state);
void pinetime_make_dfu_notify_port(ble_dfu_notify_port_t *out, pinetime_glue_state_t *state);
void pinetime_make_dfu_system_port(ble_dfu_system_port_t *out, pinetime_glue_state_t *state);

void pinetime_make_validator_port(firmware_validator_port_t *out, pinetime_glue_state_t *state);
void pinetime_make_ui_display_port(watch_ui_display_port_t *out, pinetime_glue_state_t *state);
void pinetime_make_ui_status_port(watch_ui_status_port_t *out, pinetime_glue_state_t *state);

void pinetime_make_metronome_motor_port(metronome_motor_if_t *out, pinetime_glue_state_t *state);
void pinetime_make_dice_entropy_port(dice_entropy_if_t *out, pinetime_glue_state_t *state);
void pinetime_make_dice_motor_port(dice_motor_if_t *out, pinetime_glue_state_t *state);

void pinetime_make_flashlight_display_port(flashlight_display_if_t *out,
                                           pinetime_glue_state_t *state);
void pinetime_make_flashlight_system_port(flashlight_system_if_t *out,
                                          pinetime_glue_state_t *state);
void pinetime_make_game_paddle_motor_port(paddle_motor_if_t *out, pinetime_glue_state_t *state);
void pinetime_make_game_paddle_entropy_port(paddle_entropy_if_t *out, pinetime_glue_state_t *state);
void pinetime_make_game_twos_entropy_port(twos_entropy_if_t *out, pinetime_glue_state_t *state);
void pinetime_make_paint_display_port(paint_display_if_t *out, pinetime_glue_state_t *state);
void pinetime_make_paint_motor_port(paint_motor_if_t *out, pinetime_glue_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* PRODUCT_PINETIME_GLUE_H */
