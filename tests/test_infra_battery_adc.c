#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "battery_adc/battery_adc.h"

static void test_raw_to_mv(void **state) {
    (void)state;
    // 0 counts -> 0 mV
    assert_int_equal(battery_adc_raw_to_mv(0), 0);
    assert_int_equal(battery_adc_raw_to_mv(-5), 0);

    // 1024 counts -> 4800 mV
    assert_int_equal(battery_adc_raw_to_mv(1024), 4800);

    // 800 counts -> 800 * 75 / 16 = 3750 mV
    assert_int_equal(battery_adc_raw_to_mv(800), 3750);
}

static void test_mv_to_percent_lut(void **state) {
    (void)state;
    // Bounds
    assert_int_equal(battery_adc_mv_to_percent(3400, false, false), 0);
    assert_int_equal(battery_adc_mv_to_percent(3500, false, false), 0);
    assert_int_equal(battery_adc_mv_to_percent(3616, false, false), 3);
    assert_int_equal(battery_adc_mv_to_percent(3723, false, false), 22);
    assert_int_equal(battery_adc_mv_to_percent(3776, false, false), 48);
    assert_int_equal(battery_adc_mv_to_percent(3979, false, false), 79);
    assert_int_equal(battery_adc_mv_to_percent(4180, false, false), 100);
    assert_int_equal(battery_adc_mv_to_percent(4250, false, false), 100);

    // Full override
    assert_int_equal(battery_adc_mv_to_percent(3700, false, true), 100);

    // Charging clamp (max 99% unless full)
    assert_int_equal(battery_adc_mv_to_percent(4200, true, false), 99);
}

static void test_battery_update_lifecycle(void **state) {
    (void)state;
    battery_adc_state_t batt;
    battery_adc_init(&batt);

    // First measurement: 800 counts = 3750 mV (~41%)
    assert_int_equal(battery_adc_update(&batt, 800, false, false), EDGE_OK);
    assert_int_equal(batt.voltage_mv, 3750);
    assert_false(batt.is_charging);
    assert_false(batt.is_power_present);
    assert_false(batt.is_full);
    assert_in_range(batt.percent_remaining, 35, 45);

    // When discharging (power not present), percentage cannot increase
    const uint8_t initial_pct = batt.percent_remaining;
    assert_int_equal(battery_adc_update(&batt, 850, false, false), EDGE_OK); // 3984 mV (~79%)
    assert_int_equal(batt.percent_remaining, initial_pct);                   // Filtered!

    // Plug in charger
    assert_int_equal(battery_adc_update(&batt, 850, true, true), EDGE_OK);
    assert_true(batt.is_charging);
    assert_true(batt.is_power_present);
    assert_false(batt.is_full);
    assert_int_equal(batt.percent_remaining, 79); // Updated because power present

    // Power present but charging finished (charging pin high = false)
    assert_int_equal(battery_adc_update(&batt, 850, false, true), EDGE_OK);
    assert_true(batt.is_full);
    assert_int_equal(batt.percent_remaining, 100);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_raw_to_mv),
        cmocka_unit_test(test_mv_to_percent_lut),
        cmocka_unit_test(test_battery_update_lifecycle),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
