/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_battery/battery.h"

typedef struct mock_battery_hw {
    int calls;
    uint16_t mv;
    bool charging;
} mock_battery_hw_t;

static edge_status_t mock_read_millivolts(void *self, uint16_t *out_mv) {
    mock_battery_hw_t *h = (mock_battery_hw_t *)self;
    h->calls++;
    *out_mv = h->mv;
    return EDGE_OK;
}

// cppcheck-suppress constParameterCallback ; signature fixed by consumer port
static bool mock_is_charging(void *self) {
    const mock_battery_hw_t *h = (const mock_battery_hw_t *)self;
    return h->charging;
}

typedef struct mock_sink {
    int calls;
    uint8_t last_pct;
    bool last_charging;
} mock_sink_t;

static edge_status_t mock_post_battery_changed(void *self, uint8_t percentage, bool is_charging) {
    mock_sink_t *s = (mock_sink_t *)self;
    s->calls++;
    s->last_pct = percentage;
    s->last_charging = is_charging;
    return EDGE_OK;
}

static void test_battery_voltage_curve_and_events(void **state) {
    (void)state;
    mock_battery_hw_t hw_ctx = {
        .mv = 4200,
        .charging = false,
    };
    zmk_battery_hw_if_t hw = {
        .self = &hw_ctx,
        .read_millivolts = mock_read_millivolts,
        .is_charging = mock_is_charging,
    };
    mock_sink_t sink_ctx = {0};
    zmk_battery_event_sink_if_t sink = {
        .self = &sink_ctx,
        .post_battery_changed = mock_post_battery_changed,
    };

    zmk_battery_app_t app;
    zmk_battery_construct(&app, EDGE_MOD_ZMK_BATTERY, 70, &hw, &sink);
    assert_int_equal(zmk_battery_init(&app), EDGE_OK);

    assert_int_equal(zmk_battery_get_percentage(&app), 100);
    assert_int_equal(zmk_battery_get_millivolts(&app), 4200);
    assert_false(zmk_battery_is_charging(&app));

    // Voltage drops to 3825mV (approx 50%)
    hw_ctx.mv = 3825;
    assert_int_equal(zmk_battery_sample(&app), EDGE_OK);
    assert_int_equal(zmk_battery_get_percentage(&app), 51);
    assert_int_equal(sink_ctx.last_pct, 51);
    assert_int_equal(sink_ctx.calls, 1);

    // Charger connected
    hw_ctx.charging = true;
    assert_int_equal(zmk_battery_sample(&app), EDGE_OK);
    assert_true(zmk_battery_is_charging(&app));
    assert_true(sink_ctx.last_charging);
    assert_int_equal(sink_ctx.calls, 2);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_battery_voltage_curve_and_events),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
