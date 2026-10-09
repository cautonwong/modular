#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "ble_services/ble_services.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"

static uint16_t g_last_notified_handle = 0;
static uint8_t g_last_notified_data[16] = {0};
static size_t g_last_notified_len = 0;
static uint32_t g_notify_count = 0;

static edge_status_t mock_gatt_notify(void *self, uint16_t char_handle, const uint8_t *data,
                                      size_t len) {
    (void)self;
    g_last_notified_handle = char_handle;
    g_last_notified_len = len;
    if (len <= sizeof(g_last_notified_data) && data != NULL) {
        for (size_t i = 0; i < len; ++i) {
            g_last_notified_data[i] = data[i];
        }
    }
    g_notify_count++;
    return EDGE_OK;
}

static uint16_t g_sink_year = 0;
static uint8_t g_sink_month = 0;
static uint8_t g_sink_day = 0;
static uint8_t g_sink_hour = 0;
static uint8_t g_sink_minute = 0;
static uint8_t g_sink_second = 0;
static uint32_t g_time_sink_set_count = 0;

static edge_status_t mock_time_sink_set_time(void *self, uint16_t year, uint8_t month, uint8_t day,
                                             uint8_t hour, uint8_t minute, uint8_t second) {
    (void)self;
    g_sink_year = year;
    g_sink_month = month;
    g_sink_day = day;
    g_sink_hour = hour;
    g_sink_minute = minute;
    g_sink_second = second;
    g_time_sink_set_count++;
    return EDGE_OK;
}

static void test_ble_cts_encode_decode(void **state) {
    (void)state;

    const cts_datetime_t dt = {
        .year = 2026,
        .month = 10,
        .day_of_month = 1,
        .hours = 14,
        .minutes = 30,
        .seconds = 45,
        .day_of_week = 4,
        .fractions256 = 128,
        .adjust_reason = 1,
    };

    uint8_t buf[16] = {0};
    size_t out_len = 0;

    assert_int_equal(cts_encode_datetime(&dt, buf, sizeof(buf), &out_len), EDGE_OK);
    assert_int_equal(out_len, 10);
    assert_int_equal(buf[0], 2026 & 0xFF);
    assert_int_equal(buf[1], (2026 >> 8) & 0xFF);
    assert_int_equal(buf[2], 10);
    assert_int_equal(buf[3], 1);
    assert_int_equal(buf[4], 14);
    assert_int_equal(buf[5], 30);
    assert_int_equal(buf[6], 45);
    assert_int_equal(buf[7], 4);
    assert_int_equal(buf[8], 128);
    assert_int_equal(buf[9], 1);

    cts_datetime_t decoded;
    assert_int_equal(cts_decode_datetime(buf, out_len, &decoded), EDGE_OK);
    assert_int_equal(decoded.year, 2026);
    assert_int_equal(decoded.month, 10);
    assert_int_equal(decoded.day_of_month, 1);
    assert_int_equal(decoded.hours, 14);
    assert_int_equal(decoded.minutes, 30);
    assert_int_equal(decoded.seconds, 45);
    assert_int_equal(decoded.day_of_week, 4);

    /* Invalid buffer lengths */
    assert_int_equal(cts_decode_datetime(buf, 9, &decoded), EDGE_EINVAL);
    assert_int_equal(cts_encode_datetime(&dt, buf, 9, &out_len), EDGE_EINVAL);
}

static void test_ble_cts_local_time(void **state) {
    (void)state;

    const cts_timezone_t tz = {
        .timezone = 32,  /* UTC+8 hours = 32 * 15 min */
        .dst_offset = 0, /* standard */
    };

    uint8_t buf[4] = {0};
    size_t out_len = 0;

    assert_int_equal(cts_encode_local_time(&tz, buf, sizeof(buf), &out_len), EDGE_OK);
    assert_int_equal(out_len, 2);
    assert_int_equal(buf[0], 32);
    assert_int_equal(buf[1], 0);

    cts_timezone_t decoded;
    assert_int_equal(cts_decode_local_time(buf, out_len, &decoded), EDGE_OK);
    assert_int_equal(decoded.timezone, 32);
    assert_int_equal(decoded.dst_offset, 0);
}

static void test_ble_hrs_and_bas_encode(void **state) {
    (void)state;

    uint8_t hrs_buf[4] = {0};
    size_t hrs_len = 0;
    assert_int_equal(hrs_encode_measurement(75, hrs_buf, sizeof(hrs_buf), &hrs_len), EDGE_OK);
    assert_int_equal(hrs_len, 2);
    assert_int_equal(hrs_buf[0], 0);  /* 8-bit format */
    assert_int_equal(hrs_buf[1], 75); /* 75 BPM */

    uint8_t bas_buf[4] = {0};
    size_t bas_len = 0;
    assert_int_equal(bas_encode_battery_level(88, bas_buf, sizeof(bas_buf), &bas_len), EDGE_OK);
    assert_int_equal(bas_len, 1);
    assert_int_equal(bas_buf[0], 88); /* 88% */
}

static void test_ble_services_app_flow(void **state) {
    (void)state;

    g_last_notified_handle = 0;
    g_last_notified_len = 0;
    g_notify_count = 0;
    g_time_sink_set_count = 0;

    const ble_gatt_server_if_t gatt_if = {
        .notify = mock_gatt_notify,
        .self = NULL,
    };
    const ble_time_sink_if_t sink_if = {
        .set_time = mock_time_sink_set_time,
        .self = NULL,
    };

    ble_services_app_t app;
    ble_services_construct(&app, EDGE_MOD_BLE_SERVICES, 100u, &gatt_if, &sink_if);
    assert_int_equal(ble_services_init(&app), EDGE_OK);

    /* Initially disconnected */
    assert_false(app.is_connected);

    /* Event arrives while disconnected -> cached but no notification sent */
    edge_event_t hr_ev = {.id = EDGE_EVT_WATCH_HEART_RATE, .arg0 = 80};
    assert_int_equal(app.module.on_event(&app.module, &hr_ev), EDGE_OK);
    assert_int_equal(app.current_bpm, 80);
    assert_int_equal(g_notify_count, 0);

    /* BLE connects */
    edge_event_t conn_ev = {.id = EDGE_EVT_WATCH_BLE_CONNECTED};
    assert_int_equal(app.module.on_event(&app.module, &conn_ev), EDGE_OK);
    assert_true(app.is_connected);

    /* Enable HRS notify */
    ble_services_set_hrs_notify(&app, true);
    assert_true(app.hrs_notify_enabled);

    /* New HR event -> triggers notification */
    hr_ev.arg0 = 85;
    assert_int_equal(app.module.on_event(&app.module, &hr_ev), EDGE_OK);
    assert_int_equal(g_notify_count, 1);
    assert_int_equal(g_last_notified_handle, app.hrs_val_handle);
    assert_int_equal(g_last_notified_len, 2);
    assert_int_equal(g_last_notified_data[1], 85);

    /* Enable BAS notify and send Battery event */
    ble_services_set_bas_notify(&app, true);
    edge_event_t bat_ev = {.id = EDGE_EVT_WATCH_BATTERY, .arg0 = 92};
    assert_int_equal(app.module.on_event(&app.module, &bat_ev), EDGE_OK);
    assert_int_equal(g_notify_count, 2);
    assert_int_equal(g_last_notified_handle, app.bas_val_handle);
    assert_int_equal(g_last_notified_len, 1);
    assert_int_equal(g_last_notified_data[0], 92);

    /* GATT Read operations */
    uint8_t read_buf[16];
    size_t read_len = 0;
    assert_int_equal(ble_services_handle_gatt_read(&app, app.hrs_val_handle, read_buf,
                                                   sizeof(read_buf), &read_len),
                     EDGE_OK);
    assert_int_equal(read_len, 2);
    assert_int_equal(read_buf[1], 85);

    assert_int_equal(ble_services_handle_gatt_read(&app, app.bas_val_handle, read_buf,
                                                   sizeof(read_buf), &read_len),
                     EDGE_OK);
    assert_int_equal(read_len, 1);
    assert_int_equal(read_buf[0], 92);

    /* GATT Write operation to CTS Time characteristic */
    const uint8_t write_cts_data[10] = {
        0xEA, 0x07, /* 2026 */
        10,         /* Oct */
        1,          /* Day 1 */
        15,         /* Hour 15 */
        45,         /* Min 45 */
        30,         /* Sec 30 */
        4,          /* Thu */
        0,    0,
    };
    assert_int_equal(ble_services_handle_gatt_write(&app, app.cts_time_val_handle, write_cts_data,
                                                    sizeof(write_cts_data)),
                     EDGE_OK);
    assert_int_equal(g_time_sink_set_count, 1);
    assert_int_equal(g_sink_year, 2026);
    assert_int_equal(g_sink_month, 10);
    assert_int_equal(g_sink_day, 1);
    assert_int_equal(g_sink_hour, 15);
    assert_int_equal(g_sink_minute, 45);
    assert_int_equal(g_sink_second, 30);

    /* BLE Disconnects -> resets notifications */
    edge_event_t disconn_ev = {.id = EDGE_EVT_WATCH_BLE_DISCONNECTED};
    assert_int_equal(app.module.on_event(&app.module, &disconn_ev), EDGE_OK);
    assert_false(app.is_connected);
    assert_false(app.hrs_notify_enabled);
    assert_false(app.bas_notify_enabled);
}

static uint8_t g_last_alert_level = 0xFFu;
static edge_status_t mock_alert_sink(void *self, uint8_t level) {
    (void)self;
    g_last_alert_level = level;
    return EDGE_OK;
}

static void test_ble_dis_and_ias(void **state) {
    (void)state;
    ble_services_app_t app;
    ble_services_construct(&app, EDGE_MOD_BLE_SERVICES, 100u, NULL, NULL);

    ble_alert_sink_if_t alert_if = {
        .self = NULL,
        .on_alert_level = mock_alert_sink,
    };
    ble_services_set_alert_sink(&app, &alert_if);

    /* Test DIS GATT Reads */
    uint8_t buf[32] = {0};
    size_t len = 0;

    assert_int_equal(
        ble_services_handle_gatt_read(&app, app.dis_mfr_val_handle, buf, sizeof(buf), &len),
        EDGE_OK);
    buf[len] = '\0';
    assert_string_equal((char *)buf, "Pine64");

    assert_int_equal(
        ble_services_handle_gatt_read(&app, app.dis_model_val_handle, buf, sizeof(buf), &len),
        EDGE_OK);
    buf[len] = '\0';
    assert_string_equal((char *)buf, "PineTime");

    assert_int_equal(
        ble_services_handle_gatt_read(&app, app.dis_fw_val_handle, buf, sizeof(buf), &len),
        EDGE_OK);
    buf[len] = '\0';
    assert_string_equal((char *)buf, "1.14.0");

    /* Test IAS GATT Write */
    const uint8_t alert_high = 2u;
    assert_int_equal(
        ble_services_handle_gatt_write(&app, app.ias_alert_val_handle, &alert_high, 1u), EDGE_OK);
    assert_int_equal(g_last_alert_level, 2u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ble_cts_encode_decode),  cmocka_unit_test(test_ble_cts_local_time),
        cmocka_unit_test(test_ble_hrs_and_bas_encode), cmocka_unit_test(test_ble_services_app_flow),
        cmocka_unit_test(test_ble_dis_and_ias),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
