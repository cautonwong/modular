#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "glue.h"
#include "pinetime/board.h"
#include "watch/sys.h"

#include <stdio.h>

int main(void) {
    pinetime_glue_state_t glue;
    pinetime_glue_init(&glue);

    /* Construct Ports */
    touch_input_if_t touch_port;
    pinetime_make_touch_port(&touch_port, &glue);

    watch_power_display_if_t display_pwr_port;
    pinetime_make_display_power_port(&display_pwr_port, &glue);

    watch_power_battery_if_t battery_pwr_port;
    pinetime_make_battery_power_port(&battery_pwr_port, &glue);

    ppg_sensor_if_t ppg_port;
    pinetime_make_ppg_port(&ppg_port, &glue);

    imu_sensor_if_t imu_port;
    pinetime_make_imu_port(&imu_port, &glue);

    rtc_clock_if_t rtc_port;
    pinetime_make_rtc_port(&rtc_port, &glue);

    ble_gatt_server_if_t gatt_port;
    pinetime_make_gatt_port(&gatt_port, &glue);

    ble_time_sink_if_t time_sink_port;
    pinetime_make_time_sink_port(&time_sink_port, &glue);

    alarm_storage_if_t alarm_store_port;
    pinetime_make_alarm_storage_port(&alarm_store_port, &glue);

    alarm_alert_if_t alarm_alert_port;
    pinetime_make_alarm_alert_port(&alarm_alert_port, &glue);

    stopwatch_clock_if_t sw_clk_port;
    pinetime_make_stopwatch_clock_port(&sw_clk_port, &glue);

    timer_clock_if_t timer_clk_port;
    pinetime_make_timer_clock_port(&timer_clk_port, &glue);

    timer_alert_if_t timer_alert_port;
    pinetime_make_timer_alert_port(&timer_alert_port, &glue);

    watch_settings_store_if_t settings_store_port;
    pinetime_make_settings_store_port(&settings_store_port, &glue);

    ble_weather_time_port_t weather_time_port;
    pinetime_make_weather_time_port(&weather_time_port, &glue);

    ble_music_transport_port_t music_trans_port;
    pinetime_make_music_transport_port(&music_trans_port, &glue);

    ble_notifications_call_port_t notif_call_port;
    pinetime_make_notif_call_port(&notif_call_port, &glue);

    ble_motion_notify_port_t motion_notify_port;
    pinetime_make_motion_notify_port(&motion_notify_port, &glue);

    ble_fs_tx_port_t fs_tx_port;
    pinetime_make_fs_tx_port(&fs_tx_port, &glue);

    ble_fs_storage_port_t fs_store_port;
    pinetime_make_fs_storage_port(&fs_store_port, &glue);

    ble_dfu_flash_port_t dfu_flash_port;
    pinetime_make_dfu_flash_port(&dfu_flash_port, &glue);

    ble_dfu_notify_port_t dfu_notify_port;
    pinetime_make_dfu_notify_port(&dfu_notify_port, &glue);

    ble_dfu_system_port_t dfu_sys_port;
    pinetime_make_dfu_system_port(&dfu_sys_port, &glue);

    firmware_validator_port_t val_hw_port;
    pinetime_make_validator_port(&val_hw_port, &glue);

    watch_ui_display_port_t ui_disp_port;
    pinetime_make_ui_display_port(&ui_disp_port, &glue);

    watch_ui_status_port_t ui_status_port;
    pinetime_make_ui_status_port(&ui_status_port, &glue);

    /* Construct Apps */
    touch_gesture_t touch_app;
    touch_gesture_construct(&touch_app, EDGE_MOD_TOUCH_GESTURE, 20u, &touch_port);
    if (touch_gesture_init(&touch_app) != EDGE_OK) {
        return 10;
    }

    button_handler_t button_app;
    button_handler_construct(&button_app, EDGE_MOD_BUTTON_HANDLER, 20u, NULL);
    if (button_handler_init(&button_app) != EDGE_OK) {
        return 11;
    }

    watch_power_t power_app;
    watch_power_construct(&power_app, EDGE_MOD_WATCH_POWER, 10u, &display_pwr_port,
                          &battery_pwr_port);
    if (watch_power_init(&power_app) != EDGE_OK) {
        return 12;
    }

    heart_rate_t hr_app;
    heart_rate_construct(&hr_app, EDGE_MOD_HEART_RATE, 40u, &ppg_port);
    if (heart_rate_init(&hr_app) != EDGE_OK) {
        return 13;
    }
    glue.hr_app = &hr_app;

    step_counter_t step_app;
    step_counter_construct(&step_app, EDGE_MOD_STEP_COUNTER, 30u, &imu_port);
    if (step_counter_init(&step_app) != EDGE_OK) {
        return 14;
    }
    glue.step_app = &step_app;

    watch_time_t time_app;
    watch_time_construct(&time_app, EDGE_MOD_WATCH_TIME, 50u, &rtc_port);
    if (watch_time_init(&time_app) != EDGE_OK) {
        return 15;
    }
    glue.time_app = &time_app;

    ble_services_app_t ble_app;
    ble_services_construct(&ble_app, EDGE_MOD_BLE_SERVICES, 60u, &gatt_port, &time_sink_port);
    if (ble_services_init(&ble_app) != EDGE_OK) {
        return 16;
    }

    alarm_app_t alarm_app;
    alarm_construct(&alarm_app, EDGE_MOD_ALARM, 70u, &alarm_store_port, &alarm_alert_port);

    stopwatch_app_t stopwatch_app;
    stopwatch_construct(&stopwatch_app, EDGE_MOD_STOPWATCH, 75u, &sw_clk_port);

    watch_timer_app_t timer_app;

    timer_construct(&timer_app, EDGE_MOD_WATCH_TIMER, 80u, &timer_clk_port, &timer_alert_port);

    watch_settings_app_t settings_app;
    watch_settings_construct(&settings_app, EDGE_MOD_WATCH_SETTINGS, 85u, &settings_store_port);

    ble_weather_t weather_app;
    ble_weather_construct(&weather_app, EDGE_MOD_BLE_WEATHER, 90u, &weather_time_port, NULL);

    ble_music_t music_app;
    ble_music_construct(&music_app, EDGE_MOD_BLE_MUSIC, 95u, &music_trans_port, NULL);

    ble_nav_t nav_app;
    ble_nav_construct(&nav_app, EDGE_MOD_BLE_NAV, 100u, NULL);

    ble_notifications_t notif_app;
    ble_notifications_construct(&notif_app, EDGE_MOD_BLE_NOTIFICATIONS, 105u, &notif_call_port,
                                NULL);

    ble_motion_t motion_app;
    ble_motion_construct(&motion_app, EDGE_MOD_BLE_MOTION, 110u, &motion_notify_port, NULL);

    ble_fs_t fs_app;
    ble_fs_construct(&fs_app, EDGE_MOD_BLE_FS, 115u, &fs_store_port, &fs_tx_port, NULL);

    ble_dfu_t dfu_app;
    ble_dfu_construct(&dfu_app, EDGE_MOD_BLE_DFU, 120u, &dfu_flash_port, &dfu_notify_port,
                      &dfu_sys_port, NULL);

    firmware_validator_t val_app;
    firmware_validator_construct(&val_app, EDGE_MOD_FIRMWARE_VALIDATOR, 125u, &val_hw_port, NULL);

    metronome_motor_if_t metronome_motor_port;
    pinetime_make_metronome_motor_port(&metronome_motor_port, &glue);

    metronome_app_t metronome_app;
    metronome_construct(&metronome_app, EDGE_MOD_METRONOME, 130u, &metronome_motor_port, NULL);

    calculator_app_t calculator_app;
    calculator_construct(&calculator_app, EDGE_MOD_CALCULATOR, 135u);

    dice_entropy_if_t dice_entropy_port;
    pinetime_make_dice_entropy_port(&dice_entropy_port, &glue);

    dice_motor_if_t dice_motor_port;
    pinetime_make_dice_motor_port(&dice_motor_port, &glue);

    dice_app_t dice_app;
    dice_construct(&dice_app, EDGE_MOD_DICE, 140u, &dice_entropy_port, &dice_motor_port);

    ble_passkey_app_t ble_passkey_app;
    ble_passkey_construct(&ble_passkey_app, EDGE_MOD_BLE_PASSKEY, 145u);

    flashlight_display_if_t flashlight_disp_port;
    pinetime_make_flashlight_display_port(&flashlight_disp_port, &glue);

    flashlight_system_if_t flashlight_sys_port;
    pinetime_make_flashlight_system_port(&flashlight_sys_port, &glue);

    flashlight_app_t flashlight_app;
    flashlight_construct(&flashlight_app, EDGE_MOD_FLASHLIGHT, 150u, &flashlight_disp_port,
                         &flashlight_sys_port);

    paddle_motor_if_t paddle_motor_port;
    pinetime_make_game_paddle_motor_port(&paddle_motor_port, &glue);

    paddle_entropy_if_t paddle_entropy_port;
    pinetime_make_game_paddle_entropy_port(&paddle_entropy_port, &glue);

    game_paddle_app_t game_paddle_app;
    game_paddle_construct(&game_paddle_app, EDGE_MOD_GAME_PADDLE, 155u, &paddle_motor_port,
                          &paddle_entropy_port);

    twos_entropy_if_t twos_entropy_port;
    pinetime_make_game_twos_entropy_port(&twos_entropy_port, &glue);

    game_twos_app_t game_twos_app;
    game_twos_construct(&game_twos_app, EDGE_MOD_GAME_TWOS, 160u, &twos_entropy_port);

    paint_display_if_t paint_disp_port;
    pinetime_make_paint_display_port(&paint_disp_port, &glue);

    paint_motor_if_t paint_motor_port;
    pinetime_make_paint_motor_port(&paint_motor_port, &glue);

    paint_app_t paint_app;
    paint_construct(&paint_app, EDGE_MOD_PAINT, 165u, &paint_disp_port, &paint_motor_port);

    watch_ui_t ui_app;
    watch_ui_construct(&ui_app, EDGE_MOD_WATCH_UI, 15u, &ui_disp_port, &ui_status_port, NULL);

    /* Assemble App List */
    edge_module_t *apps[28];
    apps[0] = touch_gesture_module(&touch_app);
    apps[1] = button_handler_module(&button_app);
    apps[2] = watch_power_module(&power_app);
    apps[3] = &hr_app.module;
    apps[4] = &step_app.module;
    apps[5] = &time_app.module;
    apps[6] = &ble_app.module;
    apps[7] = &alarm_app.module;
    apps[8] = &stopwatch_app.module;
    apps[9] = &timer_app.module;
    apps[10] = &settings_app.module;
    apps[11] = &weather_app.module;
    apps[12] = &music_app.module;
    apps[13] = &nav_app.module;
    apps[14] = &notif_app.module;
    apps[15] = &motion_app.module;
    apps[16] = &fs_app.module;
    apps[17] = &dfu_app.module;
    apps[18] = &val_app.module;
    apps[19] = &metronome_app.module;
    apps[20] = &calculator_app.module;
    apps[21] = &dice_app.module;
    apps[22] = &ble_passkey_app.module;
    apps[23] = &flashlight_app.module;
    apps[24] = &game_paddle_app.module;
    apps[25] = &game_twos_app.module;
    apps[26] = &paint_app.module;
    apps[27] = &ui_app.module;

    /* System & Event Infrastructure */
    edge_event_t event_storage[32];
    edge_event_queue_t event_queue;
    if (edge_event_queue_init(&event_queue, event_storage, 32u) != EDGE_OK) {
        return 1;
    }

    edge_event_sink_t event_sink = {
        .queue = &event_queue,
        .clock = NULL,
        .guard = NULL,
    };
    board_pinetime_init(&event_sink);

    edge_sys_subscription_t subs[32];
    edge_sys_t sys;

    static const uint32_t required_ids[] = {
        EDGE_MOD_TOUCH_GESTURE, EDGE_MOD_BUTTON_HANDLER, EDGE_MOD_WATCH_POWER,
        EDGE_MOD_WATCH_TIME,    EDGE_MOD_WATCH_UI,
    };

    if (sys_watch_init(&sys, apps, 28u, required_ids,
                       sizeof(required_ids) / sizeof(required_ids[0]), &event_queue, subs,
                       32u) != EDGE_OK) {
        return 2;
    }

    /* Subscriptions */
    (void)edge_sys_subscribe(&sys, EDGE_EVT_WATCH_TOUCH, &ui_app.module);
    (void)edge_sys_subscribe(&sys, EDGE_EVT_WATCH_BUTTON, &ui_app.module);
    (void)edge_sys_subscribe(&sys, EDGE_EVT_WATCH_WRIST_WAKE, &ui_app.module);
    (void)edge_sys_subscribe(&sys, EDGE_EVT_WATCH_HEART_RATE, &ble_app.module);
    (void)edge_sys_subscribe(&sys, EDGE_EVT_WATCH_BATTERY, &ble_app.module);
    (void)edge_sys_subscribe(&sys, EDGE_EVT_WATCH_TIME_TICK, &ui_app.module);

    if (edge_sys_start(&sys) != EDGE_OK) {
        return 3;
    }

    printf("Starting PineTime Smartwatch composition root...\n");

    /* Step the scheduler for 50 cycles */
    for (int cycle = 0; cycle < 50; cycle++) {
        glue.rtc_ticks += 100u;

        const edge_status_t step_rc = edge_sys_step(&sys);
        if (step_rc != EDGE_OK) {
            printf("FAIL: scheduler step returned 0x%x at cycle %d\n", (unsigned)step_rc, cycle);
            (void)edge_sys_power_off(&sys);
            return 30;
        }
    }

    /* Clean shutdown */
    (void)edge_sys_power_off(&sys);
    (void)touch_gesture_deinit(&touch_app);
    (void)button_handler_deinit(&button_app);
    (void)watch_power_deinit(&power_app);
    (void)heart_rate_shutdown(&hr_app);
    (void)step_counter_shutdown(&step_app);
    (void)watch_time_shutdown(&time_app);
    (void)ble_services_shutdown(&ble_app);
    (void)edge_sys_deinit(&sys);

    printf("PineTime composition completed successfully!\n");
    return 0;
}
