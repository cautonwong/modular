/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_leds/leds.h"

typedef struct mock_led_hw {
    int calls;
    uint8_t brightness[8];
} mock_led_hw_t;

static edge_status_t mock_set_brightness(void *self, uint8_t led_index, uint8_t brightness_pct) {
    mock_led_hw_t *h = (mock_led_hw_t *)self;
    h->calls++;
    if (led_index < 8) {
        h->brightness[led_index] = brightness_pct;
    }
    return EDGE_OK;
}

static void test_leds_caps_lock_and_sleep(void **state) {
    (void)state;
    mock_led_hw_t hw_ctx = {0};
    zmk_leds_hw_if_t hw = {.self = &hw_ctx, .set_brightness = mock_set_brightness};

    zmk_leds_app_t app;
    zmk_leds_construct(&app, EDGE_MOD_ZMK_LEDS, 50, &hw);
    assert_int_equal(zmk_leds_init(&app), EDGE_OK);

    // Add Caps Lock LED on LED 0: active 100%, inactive 0%
    zmk_led_config_t caps_cfg = {
        .led_index = 0,
        .indicator_mask = ZMK_LED_IND_CAPS_LOCK,
        .active_brightness = 100,
        .inactive_brightness = 0,
        .disconnected_brightness = 10,
        .on_while_idle = false,
    };
    assert_int_equal(zmk_leds_add_indicator(&app, &caps_cfg), EDGE_OK);
    assert_int_equal(hw_ctx.brightness[0], 0);

    // Turn Caps Lock on (bit 1)
    assert_int_equal(zmk_leds_on_hid_indicators(&app, ZMK_LED_IND_CAPS_LOCK), EDGE_OK);
    assert_int_equal(hw_ctx.brightness[0], 100);

    // Go to Idle (on_while_idle is false, battery powered)
    zmk_leds_on_endpoint_status(&app, true, false /* battery */);
    assert_int_equal(zmk_leds_on_activity_state(&app, 1 /* IDLE */), EDGE_OK);
    assert_int_equal(hw_ctx.brightness[0], 0);

    // Active again
    assert_int_equal(zmk_leds_on_activity_state(&app, 0 /* ACTIVE */), EDGE_OK);
    assert_int_equal(hw_ctx.brightness[0], 100);

    // Disconnected
    assert_int_equal(zmk_leds_on_endpoint_status(&app, false, false), EDGE_OK);
    assert_int_equal(hw_ctx.brightness[0], 10);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_leds_caps_lock_and_sleep),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
