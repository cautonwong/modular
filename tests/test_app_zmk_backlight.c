/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_backlight/backlight.h"

typedef struct mock_backlight_hw {
    int calls;
    uint8_t last_brightness;
} mock_backlight_hw_t;

static edge_status_t mock_set_brightness(void *self, uint8_t brightness_pct) {
    mock_backlight_hw_t *h = (mock_backlight_hw_t *)self;
    h->calls++;
    h->last_brightness = brightness_pct;
    return EDGE_OK;
}

static void test_backlight_toggle_adjust_and_idle(void **state) {
    (void)state;
    mock_backlight_hw_t hw_ctx = {0};
    zmk_backlight_hw_if_t hw = {.self = &hw_ctx, .set_brightness = mock_set_brightness};

    zmk_backlight_app_t app;
    zmk_backlight_construct(&app, EDGE_MOD_ZMK_BACKLIGHT, 60, &hw);
    assert_int_equal(zmk_backlight_init(&app), EDGE_OK);

    assert_true(zmk_backlight_is_on(&app));
    assert_int_equal(hw_ctx.last_brightness, 100);

    // Toggle off
    assert_int_equal(zmk_backlight_toggle(&app), EDGE_OK);
    assert_false(zmk_backlight_is_on(&app));
    assert_int_equal(hw_ctx.last_brightness, 0);

    // Toggle on
    assert_int_equal(zmk_backlight_toggle(&app), EDGE_OK);
    assert_true(zmk_backlight_is_on(&app));
    assert_int_equal(hw_ctx.last_brightness, 100);

    // Adjust step -10%
    assert_int_equal(zmk_backlight_adjust_brt(&app, -1), EDGE_OK);
    assert_int_equal(hw_ctx.last_brightness, 90);

    // Auto off on idle
    assert_int_equal(zmk_backlight_on_activity_state(&app, 1 /* IDLE */), EDGE_OK);
    assert_int_equal(hw_ctx.last_brightness, 0);

    // Wake up
    assert_int_equal(zmk_backlight_on_activity_state(&app, 0 /* ACTIVE */), EDGE_OK);
    assert_int_equal(hw_ctx.last_brightness, 90);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_backlight_toggle_adjust_and_idle),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
