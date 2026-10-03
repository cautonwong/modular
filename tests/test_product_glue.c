#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cmocka.h>

#include "dlt645/dlt645.h"
#include "edge/modules.h"
#include "flash/flash.h"
#include "gateway.h"
#include "gpio/gpio.h"
#include "meter_core/meter_core.h"
#include "modbus_slave/modbus_slave.h"
#include "pinetime/glue.h"
#include "relay/relay.h"
#include "uart/uart.h"
#include "watch_host/glue.h"

/*
 * The product glues are pure adapters, and the coverage gate now includes every
 * product's glue. Without this file those trampolines would only ever be
 * compiled into a product executable that the unit tests never run -- they would
 * read as 0% covered and, worse, nothing would catch a broken adapter.
 */
void product_meter_host_make_storage(dlt645_storage_if_t *out, void *flash_state);
void product_meter_host_make_relay_out(relay_out_if_t *out, void *gpio_state);
void product_meter_mps2_make_storage(dlt645_storage_if_t *out, void *flash_state);
void product_meter_mps2_make_relay_out(relay_out_if_t *out, void *gpio_state);
void product_meter_gateway_host_make_storage(dlt645_storage_if_t *out, void *flash_state);
void product_meter_gateway_host_make_modbus(modbus_store_if_t *store,
                                            modbus_transport_if_t *transport,
                                            gateway_state_t *state);

static void exercise_storage(const dlt645_storage_if_t *storage) {
    const uint8_t payload[3] = {0x11u, 0x22u, 0x33u};
    uint8_t readback[3] = {0};

    assert_non_null(storage->read);
    assert_non_null(storage->write);
    assert_int_equal(storage->write(storage->self, 0u, payload, sizeof payload), EDGE_OK);
    assert_int_equal(storage->read(storage->self, 0u, readback, sizeof readback), EDGE_OK);
    assert_memory_equal(readback, payload, sizeof payload);
}

static void exercise_relay(const relay_out_if_t *relay, uint8_t *gpio) {
    assert_non_null(relay->set);
    assert_int_equal(relay->set(relay->self, 2u, true), EDGE_OK);
    assert_int_equal(gpio[2], 1u);
    assert_int_equal(relay->set(relay->self, 2u, false), EDGE_OK);
    assert_int_equal(gpio[2], 0u);
}

static void test_meter_host_glue(void **state) {
    (void)state;
    uint8_t flash[64] = {0};
    uint8_t gpio[8] = {0};
    dlt645_storage_if_t storage;
    relay_out_if_t relay;

    product_meter_host_make_storage(&storage, flash);
    product_meter_host_make_relay_out(&relay, gpio);
    assert_ptr_equal(storage.self, flash);
    assert_ptr_equal(relay.self, gpio);
    exercise_storage(&storage);
    exercise_relay(&relay, gpio);
}

static void test_meter_mps2_glue(void **state) {
    (void)state;
    uint8_t flash[64] = {0};
    uint8_t gpio[8] = {0};
    dlt645_storage_if_t storage;
    relay_out_if_t relay;

    product_meter_mps2_make_storage(&storage, flash);
    product_meter_mps2_make_relay_out(&relay, gpio);
    exercise_storage(&storage);
    exercise_relay(&relay, gpio);
}

static void test_gateway_glue(void **state) {
    (void)state;
    gateway_state_t gateway = {0};
    dlt645_storage_if_t storage;
    modbus_store_if_t store;
    modbus_transport_if_t transport;
    const uint8_t frame[2] = {0xAAu, 0xBBu};
    uint16_t value = 0u;
    bool coil = false;

    product_meter_gateway_host_make_storage(&storage, gateway.flash);
    exercise_storage(&storage);

    meter_core_construct(&gateway.meter, 1u, 1u);
    assert_int_equal(meter_core_init(&gateway.meter), EDGE_OK);

    /* App-to-app adapter (D78): modbus_slave's store port over meter_core. */
    product_meter_gateway_host_make_modbus(&store, &transport, &gateway);
    assert_ptr_equal(store.self, &gateway.meter);
    assert_ptr_equal(transport.self, &gateway);
    assert_int_equal(store.write_holding(store.self, 3u, 0x1234u), EDGE_OK);
    assert_int_equal(store.read_holding(store.self, 3u, &value), EDGE_OK);
    assert_int_equal(value, 0x1234u);
    assert_int_equal(store.write_coil(store.self, 1u, true), EDGE_OK);
    assert_int_equal(store.read_coil(store.self, 1u, &coil), EDGE_OK);
    assert_true(coil);

    /* Transport over the uart fake, with its own write accounting. */
    assert_int_equal(transport.write(transport.self, frame, sizeof frame), EDGE_OK);
    assert_int_equal(gateway.uart_writes, 1u);
    assert_int_equal(gateway.uart_tx[0], 0xAAu);
    assert_int_equal(gateway.uart_tx[1], 0xBBu);

    /* #170: the largest frame the protocol can build (32 registers: 2 + 64 PDU + 3)
     * fits the sink, and one byte more is refused instead of copied past its end -
     * which is what the previous 64-byte buffer did, silently. */
    uint8_t worst_case[69] = {0};
    assert_int_equal(transport.write(transport.self, worst_case, sizeof worst_case), EDGE_OK);
    assert_int_equal(gateway.uart_writes, 2u);
    uint8_t too_big[GATEWAY_UART_TX_CAPACITY + 1u] = {0};
    assert_int_equal(transport.write(transport.self, too_big, sizeof too_big), EDGE_ENOSPC);
    assert_int_equal(gateway.uart_writes, 2u);
}

static void test_gateway_transport_rejects_missing_state(void **state) {
    (void)state;
    gateway_state_t gateway = {0};
    modbus_store_if_t store;
    modbus_transport_if_t transport;

    product_meter_gateway_host_make_modbus(&store, &transport, &gateway);
    /* A factory is null-safe by contract; so is the adapter with no state. */
    assert_int_equal(transport.write(NULL, "x", 1u), EDGE_EINVAL);
}

#include "corne_split/glue.h"
#include "hid_report/hid_report.h"
#include "keyboard_host/glue.h"
#include "zmk_behavior/behavior.h"
#include "zmk_keymap/keymap.h"

static void test_keyboard_host_glue(void **state) {
    (void)state;
    hid_report_builder_t builder;
    hid_report_builder_init(&builder);

    zmk_behavior_hid_if_t hid_if;
    product_keyboard_make_behavior_hid(&hid_if, &builder);
    assert_non_null(hid_if.press_key);
    assert_int_equal(hid_if.press_key(hid_if.self, 0x04, 0x02), EDGE_OK);
    assert_int_equal(hid_if.release_key(hid_if.self, 0x04, 0x02), EDGE_OK);
    assert_int_equal(hid_if.press_consumer_key(hid_if.self, 0x01), EDGE_OK);
    assert_int_equal(hid_if.release_consumer_key(hid_if.self, 0x01), EDGE_OK);
    assert_int_equal(hid_if.press_mouse_button(hid_if.self, 0x01), EDGE_OK);
    assert_int_equal(hid_if.release_mouse_button(hid_if.self, 0x01), EDGE_OK);

    zmk_behavior_app_t bhv;
    zmk_behavior_construct(&bhv, EDGE_MOD_ZMK_BEHAVIOR, 50, &hid_if, NULL);
    assert_int_equal(zmk_behavior_init(&bhv), EDGE_OK);

    zmk_keymap_behavior_if_t kb_if;
    product_keyboard_make_keymap_behavior(&kb_if, &bhv);
    assert_non_null(kb_if.invoke_binding);
    assert_int_equal(kb_if.invoke_binding(kb_if.self, ZMK_BHV_KEY_PRESS, 0x04, 0, true, 100),
                     EDGE_OK);

    zmk_keymap_app_t keymap;
    zmk_keymap_construct(&keymap, EDGE_MOD_ZMK_KEYMAP, 1u, &kb_if, NULL, 1u, 1u);
    assert_int_equal(zmk_keymap_init(&keymap), EDGE_OK);

    zmk_matrix_event_sink_if_t mat_sink;
    product_keyboard_make_matrix_sink(&mat_sink, &keymap);
    assert_non_null(mat_sink.post_position_event);
    assert_int_equal(mat_sink.post_position_event(mat_sink.self, 0, true, 100), EDGE_OK);
}

static void test_corne_split_glue(void **state) {
    (void)state;
    hid_report_builder_t builder;
    hid_report_builder_init(&builder);

    zmk_behavior_hid_if_t hid_if;
    product_corne_make_behavior_hid(&hid_if, &builder);
    assert_non_null(hid_if.press_key);
    assert_int_equal(hid_if.press_key(hid_if.self, 0x04, 0x02), EDGE_OK);
    assert_int_equal(hid_if.release_key(hid_if.self, 0x04, 0x02), EDGE_OK);
    assert_int_equal(hid_if.press_consumer_key(hid_if.self, 0x01), EDGE_OK);
    assert_int_equal(hid_if.release_consumer_key(hid_if.self, 0x01), EDGE_OK);
    assert_int_equal(hid_if.press_mouse_button(hid_if.self, 0x01), EDGE_OK);
    assert_int_equal(hid_if.release_mouse_button(hid_if.self, 0x01), EDGE_OK);

    zmk_behavior_app_t bhv;
    zmk_behavior_construct(&bhv, EDGE_MOD_ZMK_BEHAVIOR, 50, &hid_if, NULL);
    assert_int_equal(zmk_behavior_init(&bhv), EDGE_OK);

    zmk_keymap_behavior_if_t kb_if;
    product_corne_make_keymap_behavior(&kb_if, &bhv);
    assert_non_null(kb_if.invoke_binding);
    assert_int_equal(kb_if.invoke_binding(kb_if.self, ZMK_BHV_KEY_PRESS, 0x04, 0, true, 100),
                     EDGE_OK);

    zmk_keymap_app_t keymap;
    zmk_keymap_construct(&keymap, EDGE_MOD_ZMK_KEYMAP, 1u, &kb_if, NULL, 1u, 1u);
    assert_int_equal(zmk_keymap_init(&keymap), EDGE_OK);

    zmk_matrix_event_sink_if_t mat_sink;
    product_corne_make_matrix_sink(&mat_sink, &keymap);
    assert_non_null(mat_sink.post_position_event);
    assert_int_equal(mat_sink.post_position_event(mat_sink.self, 0, true, 100), EDGE_OK);

    zmk_split_receiver_if_t split_rx;
    product_corne_make_split_receiver(&split_rx, &keymap);
    assert_non_null(split_rx.on_remote_position_changed);
    assert_int_equal(split_rx.on_remote_position_changed(split_rx.self, 0, true, 100), EDGE_OK);
}

static void test_glue_factories_are_null_safe(void **state) {
    (void)state;
    uint8_t buffer[64] = {0};
    gateway_state_t gateway = {0};

    product_meter_host_make_storage(NULL, buffer);
    product_meter_host_make_relay_out(NULL, buffer);
    product_meter_mps2_make_storage(NULL, buffer);
    product_meter_mps2_make_relay_out(NULL, buffer);
    product_meter_gateway_host_make_storage(NULL, buffer);
    product_meter_gateway_host_make_modbus(NULL, NULL, &gateway);
    product_keyboard_make_behavior_hid(NULL, NULL);
    product_keyboard_make_keymap_behavior(NULL, NULL);
    product_keyboard_make_matrix_sink(NULL, NULL);
    product_corne_make_behavior_hid(NULL, NULL);
    product_corne_make_keymap_behavior(NULL, NULL);
    product_corne_make_matrix_sink(NULL, NULL);
    product_corne_make_split_receiver(NULL, NULL);
}

static void test_watch_host_glue(void **state) {
    (void)state;
    watch_host_glue_state_t glue_state;
    memset(&glue_state, 0, sizeof(glue_state));

    /* Touch port */
    glue_state.touch_pressed = true;
    glue_state.touch_x = 120;
    glue_state.touch_y = 150;
    glue_state.touch_gesture = CST816S_GESTURE_SLIDE_LEFT;

    touch_input_if_t touch_port;
    watch_host_make_touch_port(&touch_port, &glue_state);
    assert_non_null(touch_port.read_touch);
    touch_raw_info_t pt;
    assert_int_equal(touch_port.read_touch(touch_port.self, &pt), EDGE_OK);
    assert_true(pt.touching);
    assert_int_equal(pt.x, 120);
    assert_int_equal(pt.y, 150);
    assert_int_equal(pt.hardware_gesture, CST816S_GESTURE_SLIDE_LEFT);

    /* Display port */
    watch_power_display_if_t display_port;
    watch_host_make_display_port(&display_port, &glue_state);
    assert_non_null(display_port.set_brightness);
    assert_non_null(display_port.sleep);
    assert_int_equal(display_port.set_brightness(display_port.self, 75), EDGE_OK);
    assert_int_equal(glue_state.display_brightness, 75);
    assert_int_equal(display_port.sleep(display_port.self, true), EDGE_OK);
    assert_true(glue_state.display_sleep);

    /* PPG port */
    glue_state.ppg_sample_val = 12345u;
    ppg_sensor_if_t ppg_port;
    watch_host_make_ppg_port(&ppg_port, &glue_state);
    assert_non_null(ppg_port.read_sample);
    uint16_t hrs = 0;
    uint16_t als = 0;
    assert_int_equal(ppg_port.read_sample(ppg_port.self, &hrs, &als), EDGE_OK);
    assert_int_equal(hrs, 12345u);
    assert_int_equal(glue_state.ppg_read_count, 1u);

    /* IMU port */
    glue_state.imu_sample = (bma421_accel_t){.x = 100, .y = -200, .z = 1000};
    imu_sensor_if_t imu_port;
    watch_host_make_imu_port(&imu_port, &glue_state);
    assert_non_null(imu_port.read_accel);
    int16_t x = 0, y = 0, z = 0;
    assert_int_equal(imu_port.read_accel(imu_port.self, &x, &y, &z), EDGE_OK);
    assert_int_equal(x, 100);
    assert_int_equal(y, -200);
    assert_int_equal(z, 1000);

    /* RTC port */
    glue_state.rtc_ticks = 42000u;
    rtc_clock_if_t rtc_port;
    watch_host_make_rtc_port(&rtc_port, &glue_state);
    assert_non_null(rtc_port.get_counter);
    assert_non_null(rtc_port.get_tick_frequency);
    uint32_t cnt = 0;
    assert_int_equal(rtc_port.get_counter(rtc_port.self, &cnt), EDGE_OK);
    assert_int_equal(cnt, 42000u);
    assert_int_equal(rtc_port.get_tick_frequency(rtc_port.self), 1000u);

    /* GATT server port */
    ble_gatt_server_if_t gatt_port;
    watch_host_make_gatt_port(&gatt_port, &glue_state);
    assert_non_null(gatt_port.notify);
    uint8_t notify_data[2] = {0, 80};
    assert_int_equal(gatt_port.notify(gatt_port.self, 0x10u, notify_data, 2u), EDGE_OK);
    assert_int_equal(glue_state.last_ble_handle, 0x10u);
    assert_int_equal(glue_state.last_ble_len, 2u);
    assert_int_equal(glue_state.last_ble_data[1], 80);
    assert_int_equal(glue_state.ble_notify_count, 1u);

    /* Time sink port */
    watch_time_t time_app;
    watch_time_construct(&time_app, EDGE_MOD_WATCH_TIME, 50u, &rtc_port);
    assert_int_equal(watch_time_init(&time_app), EDGE_OK);
    glue_state.time_app = &time_app;

    ble_time_sink_if_t time_sink_port;
    watch_host_make_time_sink_port(&time_sink_port, &glue_state);
    assert_non_null(time_sink_port.set_time);
    assert_int_equal(time_sink_port.set_time(time_sink_port.self, 2026, 10, 1, 14, 30, 0), EDGE_OK);
    watch_datetime_t cur_dt = watch_time_get(&time_app);
    assert_int_equal(cur_dt.year, 2026);
    assert_int_equal(cur_dt.month, 10);
    assert_int_equal(cur_dt.day, 1);
    assert_int_equal(cur_dt.hour, 14);
    assert_int_equal(cur_dt.minute, 30);
}

void product_meter_rtems_make_storage(dlt645_storage_if_t *out, void *flash_state);

static void test_meter_rtems_glue(void **state) {
    (void)state;
    uint8_t flash[64] = {0};
    dlt645_storage_if_t storage;

    product_meter_rtems_make_storage(&storage, flash);
    assert_ptr_equal(storage.self, flash);
    exercise_storage(&storage);
    product_meter_rtems_make_storage(NULL, flash);
}

static void test_pinetime_glue(void **state) {
    (void)state;

    pinetime_glue_state_t glue;
    pinetime_glue_init(&glue);

    /* Touch input */
    touch_input_if_t touch_port;
    pinetime_make_touch_port(&touch_port, &glue);
    assert_non_null(touch_port.read_touch);
    touch_raw_info_t tinfo;
    glue.touch_pressed = true;
    glue.touch_x = 120;
    glue.touch_y = 120;
    assert_int_equal(touch_port.read_touch(touch_port.self, &tinfo), EDGE_OK);
    assert_true(tinfo.touching);
    assert_int_equal(tinfo.x, 120);
    assert_int_equal(touch_port.sleep(touch_port.self, true), EDGE_OK);

    /* Display and battery power */
    watch_power_display_if_t disp_port;
    pinetime_make_display_power_port(&disp_port, &glue);
    assert_non_null(disp_port.set_brightness);
    assert_int_equal(disp_port.set_brightness(disp_port.self, 80), EDGE_OK);
    assert_int_equal(glue.display_brightness, 80);
    assert_int_equal(disp_port.sleep(disp_port.self, true), EDGE_OK);

    watch_power_battery_if_t bat_port;
    pinetime_make_battery_power_port(&bat_port, &glue);
    assert_non_null(bat_port.read_status);
    uint16_t mv = 0;
    uint8_t pct = 0;
    bool chg = false;
    bool pres = false;
    assert_int_equal(bat_port.read_status(bat_port.self, &mv, &pct, &chg, &pres), EDGE_OK);
    assert_int_equal(mv, 3900u);
    assert_int_equal(pct, 80u);

    /* PPG and IMU */
    ppg_sensor_if_t ppg_port;
    pinetime_make_ppg_port(&ppg_port, &glue);
    uint16_t hrs = 0, als = 0;
    assert_int_equal(ppg_port.read_sample(ppg_port.self, &hrs, &als), EDGE_OK);
    assert_int_equal(hrs, 10000u);

    imu_sensor_if_t imu_port;
    pinetime_make_imu_port(&imu_port, &glue);
    int16_t ix = 0, iy = 0, iz = 0;
    assert_int_equal(imu_port.read_accel(imu_port.self, &ix, &iy, &iz), EDGE_OK);
    assert_int_equal(iz, 1024);

    /* RTC and GATT */
    rtc_clock_if_t rtc_port;
    pinetime_make_rtc_port(&rtc_port, &glue);
    uint32_t rtc_cnt = 0;
    assert_int_equal(rtc_port.get_counter(rtc_port.self, &rtc_cnt), EDGE_OK);
    assert_int_equal(rtc_port.get_tick_frequency(rtc_port.self), 1000u);

    ble_gatt_server_if_t gatt_port;
    pinetime_make_gatt_port(&gatt_port, &glue);
    uint8_t gatt_buf[4] = {1, 2, 3, 4};
    assert_int_equal(gatt_port.notify(gatt_port.self, 0x1234u, gatt_buf, 4u), EDGE_OK);

    /* Time sink */
    watch_time_t time_app;
    watch_time_construct(&time_app, EDGE_MOD_WATCH_TIME, 50u, &rtc_port);
    assert_int_equal(watch_time_init(&time_app), EDGE_OK);
    glue.time_app = &time_app;

    ble_time_sink_if_t time_sink;
    pinetime_make_time_sink_port(&time_sink, &glue);
    assert_int_equal(time_sink.set_time(time_sink.self, 2026, 10, 2, 20, 0, 0), EDGE_OK);

    /* Alarm storage and alert */
    alarm_storage_if_t alarm_store;
    pinetime_make_alarm_storage_port(&alarm_store, &glue);
    assert_non_null(alarm_store.load);
    alarm_settings_t alarm_data;
    assert_int_equal(alarm_store.load(alarm_store.self, &alarm_data), EDGE_OK);
    assert_int_equal(alarm_data.hours, 7);
    assert_int_equal(alarm_store.save(alarm_store.self, &alarm_data), EDGE_OK);

    alarm_alert_if_t alarm_alert;
    pinetime_make_alarm_alert_port(&alarm_alert, &glue);
    assert_non_null(alarm_alert.start_alert);
    assert_int_equal(alarm_alert.start_alert(alarm_alert.self), EDGE_OK);
    assert_int_equal(alarm_alert.stop_alert(alarm_alert.self), EDGE_OK);

    /* Clocks and alerts */
    stopwatch_clock_if_t sw_clk;
    pinetime_make_stopwatch_clock_port(&sw_clk, &glue);
    assert_int_equal(sw_clk.get_tick_ms(sw_clk.self), 0u);

    timer_clock_if_t tmr_clk;
    pinetime_make_timer_clock_port(&tmr_clk, &glue);
    assert_int_equal(tmr_clk.get_tick_ms(tmr_clk.self), 0u);

    timer_alert_if_t tmr_alt;
    pinetime_make_timer_alert_port(&tmr_alt, &glue);
    assert_int_equal(tmr_alt.start_alert(tmr_alt.self), EDGE_OK);
    assert_int_equal(tmr_alt.stop_alert(tmr_alt.self), EDGE_OK);

    /* Watch settings store */
    watch_settings_store_if_t set_store;
    pinetime_make_settings_store_port(&set_store, &glue);
    assert_non_null(set_store.load);
    watch_settings_data_t settings_data;
    assert_int_equal(set_store.load(set_store.self, &settings_data), EDGE_OK);
    assert_int_equal(settings_data.version, SETTINGS_FORMAT_VERSION);
    assert_int_equal(set_store.save(set_store.self, &settings_data), EDGE_OK);

    /* Weather, Music, Notification, Motion */
    ble_weather_time_port_t w_time;
    pinetime_make_weather_time_port(&w_time, &glue);
    assert_int_equal(w_time.get_timestamp_sec(w_time.self), 0u);
    assert_int_equal(w_time.get_minute_of_day(w_time.self), 1200u);

    ble_music_transport_port_t m_port;
    pinetime_make_music_transport_port(&m_port, &glue);
    assert_int_equal(m_port.send_event(m_port.self, 1u), EDGE_OK);
    assert_int_equal(m_port.get_tick_ms(m_port.self), 0u);

    ble_notifications_call_port_t n_port;
    pinetime_make_notif_call_port(&n_port, &glue);
    assert_int_equal(n_port.send_call_response(n_port.self, 1u), EDGE_OK);

    ble_motion_notify_port_t mot_port;
    pinetime_make_motion_notify_port(&mot_port, &glue);
    assert_int_equal(mot_port.notify_step_count(mot_port.self, 500u), EDGE_OK);
    assert_int_equal(mot_port.notify_motion_values(mot_port.self, 10, 20, 30), EDGE_OK);

    /* FS & DFU */
    ble_fs_tx_port_t fs_tx;
    pinetime_make_fs_tx_port(&fs_tx, &glue);
    assert_int_equal(fs_tx.send_response(fs_tx.self, (const uint8_t *)"ok", 2u), EDGE_OK);

    ble_fs_storage_port_t fs_st;
    pinetime_make_fs_storage_port(&fs_st, &glue);
    uint8_t fs_buf[16] = {0};
    uint32_t read_len = 0;
    assert_int_equal(fs_st.file_open(fs_st.self, "test", 1u), EDGE_OK);
    assert_int_equal(fs_st.file_read(fs_st.self, 0u, fs_buf, 4u, &read_len), EDGE_OK);
    assert_int_equal(fs_st.file_write(fs_st.self, 0u, fs_buf, 4u), EDGE_OK);
    assert_int_equal(fs_st.file_close(fs_st.self), EDGE_OK);
    assert_int_equal(fs_st.file_delete(fs_st.self, "test"), EDGE_OK);
    assert_int_equal(fs_st.dir_open(fs_st.self, "dir"), EDGE_OK);
    char entry_name[32] = {0};
    uint32_t file_size = 0;
    bool is_dir = false;
    assert_int_equal(
        fs_st.dir_read(fs_st.self, entry_name, sizeof(entry_name), &file_size, &is_dir),
        EDGE_ENOENT);
    assert_int_equal(fs_st.dir_close(fs_st.self), EDGE_OK);
    assert_int_equal(fs_st.dir_create(fs_st.self, "dir"), EDGE_OK);
    assert_int_equal(fs_st.file_rename(fs_st.self, "a", "b"), EDGE_OK);
    uint32_t free_bytes = 0;
    assert_int_equal(fs_st.get_free_space(fs_st.self, &free_bytes), EDGE_OK);
    assert_int_equal(free_bytes, 1048576u);

    ble_dfu_flash_port_t dfu_fl;
    pinetime_make_dfu_flash_port(&dfu_fl, &glue);
    assert_int_equal(dfu_fl.erase_range(dfu_fl.self, 0u, 4096u), EDGE_OK);
    assert_int_equal(dfu_fl.write_chunk(dfu_fl.self, 0u, fs_buf, 4u), EDGE_OK);
    assert_int_equal(dfu_fl.read_chunk(dfu_fl.self, 0u, fs_buf, 4u), EDGE_OK);

    ble_dfu_notify_port_t dfu_nt;
    pinetime_make_dfu_notify_port(&dfu_nt, &glue);
    assert_int_equal(dfu_nt.send_response(dfu_nt.self, 1u, 0u), EDGE_OK);
    assert_int_equal(dfu_nt.send_prn(dfu_nt.self, 100u), EDGE_OK);

    ble_dfu_system_port_t dfu_sys;
    pinetime_make_dfu_system_port(&dfu_sys, &glue);
    assert_int_equal(dfu_sys.system_reset(dfu_sys.self), EDGE_OK);

    /* Firmware validator HW */
    firmware_validator_port_t val_hw;
    pinetime_make_validator_port(&val_hw, &glue);
    assert_non_null(val_hw.write_word);
    assert_int_equal(val_hw.write_word(val_hw.self, 0x7BFE8u, 1u), EDGE_OK);
    assert_int_equal(glue.validator_word, 1u);
    uint32_t val_read = 0;
    assert_int_equal(val_hw.read_word(val_hw.self, 0x7BFE8u, &val_read), EDGE_OK);
    assert_int_equal(val_read, 1u);
    assert_int_equal(val_hw.system_reset(val_hw.self), EDGE_OK);

    /* UI ports */
    watch_ui_display_port_t ui_disp;
    pinetime_make_ui_display_port(&ui_disp, &glue);
    assert_int_equal(ui_disp.set_brightness(ui_disp.self, 90u), EDGE_OK);
    assert_int_equal(ui_disp.clear_screen(ui_disp.self, 0x0000u), EDGE_OK);
    uint16_t pix[4] = {0};
    assert_int_equal(ui_disp.draw_bitmap(ui_disp.self, 0, 0, 2, 2, pix), EDGE_OK);
    assert_int_equal(ui_disp.set_power_mode(ui_disp.self, true), EDGE_OK);

    watch_ui_status_port_t ui_stat;
    pinetime_make_ui_status_port(&ui_stat, &glue);
    assert_non_null(ui_stat.get_status_data);
    watch_ui_status_data_t ustat;
    assert_int_equal(ui_stat.get_status_data(ui_stat.self, &ustat), EDGE_OK);
    assert_int_equal(ustat.battery_percent, 80u);

    /* Mini-apps ports */
    metronome_motor_if_t metro_m;
    pinetime_make_metronome_motor_port(&metro_m, &glue);
    assert_int_equal(metro_m.run_duration_ms(metro_m.self, 50u), EDGE_OK);

    dice_entropy_if_t dice_e;
    pinetime_make_dice_entropy_port(&dice_e, &glue);
    uint32_t drand = dice_e.get_entropy_seed(dice_e.self);
    assert_int_not_equal(drand, 0u);

    dice_motor_if_t dice_m;
    pinetime_make_dice_motor_port(&dice_m, &glue);
    assert_int_equal(dice_m.vibrate(dice_m.self), EDGE_OK);

    flashlight_display_if_t fl_disp;
    pinetime_make_flashlight_display_port(&fl_disp, &glue);
    assert_int_equal(fl_disp.set_brightness(fl_disp.self, 100u), EDGE_OK);
    assert_int_equal(fl_disp.set_screen_color(fl_disp.self, true), EDGE_OK);

    flashlight_system_if_t fl_sys;
    pinetime_make_flashlight_system_port(&fl_sys, &glue);
    assert_int_equal(fl_sys.set_wake_lock(fl_sys.self, true), EDGE_OK);

    paddle_motor_if_t pad_m;
    pinetime_make_game_paddle_motor_port(&pad_m, &glue);
    assert_int_equal(pad_m.vibrate(pad_m.self, 20u), EDGE_OK);

    paddle_entropy_if_t pad_e;
    pinetime_make_game_paddle_entropy_port(&pad_e, &glue);
    uint32_t prand = pad_e.get_random(pad_e.self);
    assert_int_not_equal(prand, 0u);

    twos_entropy_if_t twos_e;
    pinetime_make_game_twos_entropy_port(&twos_e, &glue);
    uint32_t trand = twos_e.get_random(twos_e.self);
    assert_int_not_equal(trand, 0u);

    paint_display_if_t pnt_disp;
    pinetime_make_paint_display_port(&pnt_disp, &glue);
    assert_int_equal(pnt_disp.fill_rect(pnt_disp.self, 5, 5, 10, 10, 0x1234u), EDGE_OK);
    assert_int_equal(pnt_disp.clear(pnt_disp.self, 0x0000u), EDGE_OK);

    paint_motor_if_t pnt_m;
    pinetime_make_paint_motor_port(&pnt_m, &glue);
    assert_int_equal(pnt_m.vibrate(pnt_m.self, 20u), EDGE_OK);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_meter_host_glue),
        cmocka_unit_test(test_meter_mps2_glue),
        cmocka_unit_test(test_meter_rtems_glue),
        cmocka_unit_test(test_gateway_glue),
        cmocka_unit_test(test_gateway_transport_rejects_missing_state),
        cmocka_unit_test(test_glue_factories_are_null_safe),
        cmocka_unit_test(test_watch_host_glue),
        cmocka_unit_test(test_pinetime_glue),
        cmocka_unit_test(test_keyboard_host_glue),
        cmocka_unit_test(test_corne_split_glue),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
