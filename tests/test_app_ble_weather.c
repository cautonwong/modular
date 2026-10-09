#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "ble_weather/ble_weather.h"
#include "edge/events.h"
#include "edge/modules.h"

static uint32_t s_mock_minute_of_day = 720u; /* 12:00 PM */

static uint32_t mock_get_minute_of_day(void *self) {
    (void)self;
    return s_mock_minute_of_day;
}

static uint64_t mock_get_timestamp(void *self) {
    (void)self;
    return 1690000000ULL;
}

static void test_ble_weather_current_packet(void **state) {
    (void)state;

    ble_weather_time_port_t time_port = {
        .self = NULL,
        .get_minute_of_day = mock_get_minute_of_day,
        .get_timestamp_sec = mock_get_timestamp,
    };

    edge_event_t event_storage[8];
    edge_event_queue_t queue;
    assert_int_equal(edge_event_queue_init(&queue, event_storage, 8u), EDGE_OK);

    edge_event_sink_t sink = {
        .queue = &queue,
        .clock = NULL,
        .guard = NULL,
    };

    ble_weather_t weather;
    ble_weather_init(&weather, &time_port, &sink);

    /* Construct Version 1 Current Weather packet (53 bytes) */
    uint8_t pkt[53] = {0};
    pkt[0] = 0x00; /* CurrentWeather */
    pkt[1] = 0x01; /* Version 1 */
    /* Timestamp: 1690000000 = 0x64BC1080 */
    pkt[2] = 0x80;
    pkt[3] = 0x10;
    pkt[4] = 0xBC;
    pkt[5] = 0x64;
    /* Temp: 2150 (21.50 deg C) -> 0x0866 */
    pkt[10] = 0x66;
    pkt[11] = 0x08;
    /* Min Temp: 1500 (15.00 deg C) -> 0x05DC */
    pkt[12] = 0xDC;
    pkt[13] = 0x05;
    /* Max Temp: 2500 (25.00 deg C) -> 0x09C4 */
    pkt[14] = 0xC4;
    pkt[15] = 0x09;
    /* Location: "Brussels" */
    memcpy(&pkt[16], "Brussels", 8);
    /* Icon: CloudsSun (1) */
    pkt[48] = 0x01;
    /* Sunrise: 360 (06:00) -> 0x0168 */
    pkt[49] = 0x68;
    pkt[50] = 0x01;
    /* Sunset: 1260 (21:00) -> 0x04EC */
    pkt[51] = 0xEC;
    pkt[52] = 0x04;

    assert_int_equal(ble_weather_process_packet(&weather, pkt, sizeof(pkt)), EDGE_OK);

    edge_event_t ev;
    assert_int_equal(edge_event_pop(&queue, &ev), EDGE_OK);
    assert_int_equal(ev.id, EDGE_EVT_WATCH_WEATHER_UPDATED);

    ble_weather_current_t cur;
    assert_true(ble_weather_get_current(&weather, &cur));
    assert_int_equal(cur.temp_raw, 2150);
    assert_int_equal(ble_weather_celsius(cur.temp_raw), 22); /* 21.50 -> 22 */
    assert_int_equal(ble_weather_fahrenheit(cur.temp_raw), 71);
    assert_string_equal(cur.location, "Brussels");
    assert_int_equal(cur.icon_id, BLE_WEATHER_ICON_CLOUDS_SUN);
    assert_int_equal(cur.sunrise, 360);
    assert_int_equal(cur.sunset, 1260);

    /* Day / Night check */
    s_mock_minute_of_day = 720; /* 12:00 -> daytime */
    assert_false(ble_weather_is_night(&weather));

    s_mock_minute_of_day = 200; /* 03:20 -> night */
    assert_true(ble_weather_is_night(&weather));

    s_mock_minute_of_day = 1300; /* 21:40 -> night */
    assert_true(ble_weather_is_night(&weather));
}

static void test_ble_weather_forecast_packet(void **state) {
    (void)state;

    ble_weather_t weather;
    ble_weather_init(&weather, NULL, NULL);

    /* Construct Forecast packet for 3 days (11 + 15 = 26 bytes) */
    uint8_t pkt[26] = {0};
    pkt[0] = 0x01; /* Forecast */
    pkt[1] = 0x00; /* Version 0 */
    /* Timestamp */
    pkt[2] = 0x80;
    pkt[3] = 0x10;
    pkt[4] = 0xBC;
    pkt[5] = 0x64;
    pkt[10] = 3; /* 3 days */

    /* Day 0: Min 1200 (12 C), Max 2000 (20 C), Icon Sun (0) */
    pkt[11] = 0xB0;
    pkt[12] = 0x04;
    pkt[13] = 0xD0;
    pkt[14] = 0x07;
    pkt[15] = 0x00;

    /* Day 1: Min 1400 (14 C), Max 2400 (24 C), Icon CloudShowerHeavy (4) */
    pkt[16] = 0x78;
    pkt[17] = 0x05;
    pkt[18] = 0x60;
    pkt[19] = 0x09;
    pkt[20] = 0x04;

    /* Day 2: Min 1000 (10 C), Max 1800 (18 C), Icon Thunderstorm (6) */
    pkt[21] = 0xE8;
    pkt[22] = 0x03;
    pkt[23] = 0x08;
    pkt[24] = 0x07;
    pkt[25] = 0x06;

    assert_int_equal(ble_weather_process_packet(&weather, pkt, sizeof(pkt)), EDGE_OK);

    ble_weather_forecast_t fc;
    assert_true(ble_weather_get_forecast(&weather, &fc));
    assert_int_equal(fc.nb_days, 3);
    assert_true(fc.days[0].valid);
    assert_int_equal(fc.days[0].min_temp_raw, 1200);
    assert_int_equal(fc.days[0].max_temp_raw, 2000);
    assert_int_equal(fc.days[0].icon_id, BLE_WEATHER_ICON_SUN);
    assert_true(fc.days[1].valid);
    assert_int_equal(fc.days[1].icon_id, BLE_WEATHER_ICON_CLOUD_SHOWER_HEAVY);
    assert_true(fc.days[2].valid);
    assert_int_equal(fc.days[2].icon_id, BLE_WEATHER_ICON_THUNDERSTORM);
    assert_false(fc.days[3].valid);
}

static void test_ble_weather_polar_and_negative_temperatures(void **state) {
    (void)state;

    ble_weather_time_port_t time_port = {
        .self = NULL,
        .get_minute_of_day = mock_get_minute_of_day,
        .get_timestamp_sec = mock_get_timestamp,
    };

    ble_weather_t weather;
    ble_weather_init(&weather, &time_port, NULL);

    // Negative temperature rounding: -550 (-5.50 C) -> -6 C
    assert_int_equal(ble_weather_celsius(-550), -6);
    assert_int_equal(ble_weather_celsius(-520), -5);
    assert_int_equal(ble_weather_celsius(0), 0);
    // Fahrenheit conversion: 0 C -> 32 F, -1000 (-10.00 C) -> 14 F
    assert_int_equal(ble_weather_fahrenheit(0), 32);
    assert_int_equal(ble_weather_fahrenheit(-1000), 14);

    // Polar Night: sunrise = -2 -> is_night is always true
    uint8_t pkt[53] = {0};
    pkt[0] = 0x00;
    pkt[1] = 0x01;
    pkt[49] = (uint8_t)-2;
    pkt[50] = (uint8_t)-1; /* -2 as int16 = 0xFFFE */
    pkt[51] = (uint8_t)-2;
    pkt[52] = (uint8_t)-1;
    assert_int_equal(ble_weather_process_packet(&weather, pkt, sizeof(pkt)), EDGE_OK);

    s_mock_minute_of_day = 720; // Midday
    assert_true(ble_weather_is_night(&weather));
    s_mock_minute_of_day = 0; // Midnight
    assert_true(ble_weather_is_night(&weather));
}

static void test_ble_weather_invalid_packets(void **state) {
    (void)state;
    ble_weather_t weather;
    ble_weather_init(&weather, NULL, NULL);

    uint8_t short_pkt[1] = {0x00};
    assert_int_equal(ble_weather_process_packet(&weather, short_pkt, sizeof(short_pkt)),
                     EDGE_EINVAL);
    assert_int_equal(ble_weather_process_packet(NULL, short_pkt, 10), EDGE_EINVAL);
    assert_int_equal(ble_weather_process_packet(&weather, NULL, 10), EDGE_EINVAL);

    // Invalid message type (e.g. 0xFF)
    uint8_t bad_type_pkt[10] = {0xFF, 0x00};
    assert_int_equal(ble_weather_process_packet(&weather, bad_type_pkt, sizeof(bad_type_pkt)),
                     EDGE_ENOTSUP);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ble_weather_current_packet),
        cmocka_unit_test(test_ble_weather_forecast_packet),
        cmocka_unit_test(test_ble_weather_polar_and_negative_temperatures),
        cmocka_unit_test(test_ble_weather_invalid_packets),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
