#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "example/board.h"
#include "glue.h"
#include "watch/sys.h"

#include <stdio.h>

int main(void) {
    watch_host_glue_state_t glue_state;
    glue_state = (__typeof__(glue_state)){0};

    /* Initial simulated sensor state */
    glue_state.display_brightness = 100u;
    glue_state.display_sleep = false;
    glue_state.ppg_sample_val = 10000u;
    glue_state.imu_sample = (bma421_accel_t){.x = 0, .y = 0, .z = 1024};
    glue_state.rtc_ticks = 0u;

    /* Construct Ports */
    touch_input_if_t touch_port;
    watch_host_make_touch_port(&touch_port, &glue_state);

    watch_power_display_if_t display_port;
    watch_host_make_display_port(&display_port, &glue_state);

    ppg_sensor_if_t ppg_port;
    watch_host_make_ppg_port(&ppg_port, &glue_state);

    imu_sensor_if_t imu_port;
    watch_host_make_imu_port(&imu_port, &glue_state);

    rtc_clock_if_t rtc_port;
    watch_host_make_rtc_port(&rtc_port, &glue_state);

    ble_gatt_server_if_t gatt_port;
    watch_host_make_gatt_port(&gatt_port, &glue_state);

    ble_time_sink_if_t time_sink_port;
    watch_host_make_time_sink_port(&time_sink_port, &glue_state);

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
    watch_power_construct(&power_app, EDGE_MOD_WATCH_POWER, 10u, &display_port, NULL);
    if (watch_power_init(&power_app) != EDGE_OK) {
        return 12;
    }

    heart_rate_t hr_app;
    heart_rate_construct(&hr_app, EDGE_MOD_HEART_RATE, 40u, &ppg_port);
    if (heart_rate_init(&hr_app) != EDGE_OK) {
        return 13;
    }

    step_counter_t step_app;
    step_counter_construct(&step_app, EDGE_MOD_STEP_COUNTER, 30u, &imu_port);
    if (step_counter_init(&step_app) != EDGE_OK) {
        return 14;
    }

    watch_time_t time_app;
    watch_time_construct(&time_app, EDGE_MOD_WATCH_TIME, 50u, &rtc_port);
    if (watch_time_init(&time_app) != EDGE_OK) {
        return 15;
    }
    glue_state.time_app = &time_app;

    ble_services_app_t ble_app;
    ble_services_construct(&ble_app, EDGE_MOD_BLE_SERVICES, 60u, &gatt_port, &time_sink_port);
    if (ble_services_init(&ble_app) != EDGE_OK) {
        return 16;
    }

    /* Assemble App List */
    edge_module_t *apps[7];
    apps[0] = touch_gesture_module(&touch_app);
    apps[1] = button_handler_module(&button_app);
    apps[2] = watch_power_module(&power_app);
    apps[3] = &hr_app.module;
    apps[4] = &step_app.module;
    apps[5] = &time_app.module;
    apps[6] = &ble_app.module;

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
    board_example_init(&event_sink);

    edge_sys_subscription_t subs[16];
    edge_sys_t sys;

    static const uint32_t required_ids[] = {
        EDGE_MOD_TOUCH_GESTURE, EDGE_MOD_BUTTON_HANDLER, EDGE_MOD_WATCH_POWER,  EDGE_MOD_HEART_RATE,
        EDGE_MOD_STEP_COUNTER,  EDGE_MOD_WATCH_TIME,     EDGE_MOD_BLE_SERVICES,
    };

    if (sys_watch_init(&sys, apps, 7u, required_ids, sizeof(required_ids) / sizeof(required_ids[0]),
                       &event_queue, subs, 16u) != EDGE_OK) {
        return 2;
    }

    /* Subscriptions */
    (void)edge_sys_subscribe(&sys, EDGE_EVT_WATCH_TOUCH, watch_power_module(&power_app));
    (void)edge_sys_subscribe(&sys, EDGE_EVT_WATCH_BUTTON, watch_power_module(&power_app));
    (void)edge_sys_subscribe(&sys, EDGE_EVT_WATCH_WRIST_WAKE, watch_power_module(&power_app));
    (void)edge_sys_subscribe(&sys, EDGE_EVT_WATCH_HEART_RATE, &ble_app.module);
    (void)edge_sys_subscribe(&sys, EDGE_EVT_WATCH_BATTERY, &ble_app.module);
    (void)edge_sys_subscribe(&sys, EDGE_EVT_WATCH_BLE_CONNECTED, &ble_app.module);
    (void)edge_sys_subscribe(&sys, EDGE_EVT_WATCH_BLE_DISCONNECTED, &ble_app.module);

    if (edge_sys_start(&sys) != EDGE_OK) {
        return 3;
    }

    printf("Starting InfiniTime Modular watch host simulation...\n");

    /* Step 1: Run 100 scheduling steps with advancing time */
    for (int cycle = 0; cycle < 100; cycle++) {
        glue_state.rtc_ticks += 100u; /* 100ms per step */

        /* Simulate button push on cycle 10 */
        if (cycle == 10) {
            button_handler_update(&button_app, true, 1000u);
        } else if (cycle == 11) {
            button_handler_update(&button_app, false, 1100u);
        }

        /* Simulate wrist tilt raise on cycle 25 */
        if (cycle == 25) {
            glue_state.imu_sample = (bma421_accel_t){.x = 0, .y = -800, .z = 200};
        }

        /* Simulate BLE connect & CTS time sync on cycle 50 */
        if (cycle == 50) {
            edge_event_t conn_ev = {.id = EDGE_EVT_WATCH_BLE_CONNECTED};
            (void)edge_event_push_isr(&event_queue, &conn_ev);
            ble_services_set_hrs_notify(&ble_app, true);
            ble_services_set_bas_notify(&ble_app, true);

            const uint8_t cts_time[10] = {
                0xEA, 0x07, /* 2026 */
                10,         /* Month 10 */
                1,          /* Day 1 */
                16,         /* Hour 16 */
                30,         /* Minute 30 */
                0,          /* Second 0 */
                4,          /* Day of week */
                0,    0,
            };
            (void)ble_services_handle_gatt_write(&ble_app, ble_app.cts_time_val_handle, cts_time,
                                                 sizeof(cts_time));
        }

        /* Periodic scheduler step */
        const edge_status_t step_rc = edge_sys_step(&sys);
        if (step_rc != EDGE_OK) {
            printf("FAIL: scheduler step returned 0x%x at cycle %d\n", (unsigned)step_rc, cycle);
            return 30;
        }
    }

    /* Verify time was synchronized */
    watch_datetime_t cur_dt = watch_time_get(&time_app);
    char time_str[16];
    (void)watch_time_format(&time_app, false, time_str, sizeof(time_str));
    printf("Watch Time after run: %04d-%02d-%02d %s (Uptime: %u sec)\n", cur_dt.year, cur_dt.month,
           cur_dt.day, time_str, (unsigned)time_app.uptime_seconds);

    if (cur_dt.year != 2026 || cur_dt.month != 10 || cur_dt.day != 1 || cur_dt.hour != 16) {
        printf("FAIL: time sync did not persist correctly\n");
        return 31;
    }

    /* Shutdown */
    (void)edge_sys_power_off(&sys);
    (void)touch_gesture_deinit(&touch_app);
    (void)button_handler_deinit(&button_app);
    (void)watch_power_deinit(&power_app);
    (void)heart_rate_shutdown(&hr_app);
    (void)step_counter_shutdown(&step_app);
    (void)watch_time_shutdown(&time_app);
    (void)ble_services_shutdown(&ble_app);
    (void)edge_sys_deinit(&sys);

    printf("Watch host simulation completed successfully!\n");
    return 0;
}
