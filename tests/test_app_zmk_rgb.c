/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_rgb/rgb.h"

typedef struct mock_rgb_hw {
    int calls;
    bool on;
    uint16_t hue;
    uint8_t sat;
    uint8_t val;
    uint8_t effect;
} mock_rgb_hw_t;

static edge_status_t mock_update_rgb(void *self, bool on, uint16_t hue, uint8_t sat, uint8_t val,
                                     uint8_t effect) {
    mock_rgb_hw_t *hw = (mock_rgb_hw_t *)self;
    hw->calls++;
    hw->on = on;
    hw->hue = hue;
    hw->sat = sat;
    hw->val = val;
    hw->effect = effect;
    return EDGE_OK;
}

static void test_rgb_controls_and_effects(void **state) {
    (void)state;
    mock_rgb_hw_t hw_ctx = {0};
    zmk_rgb_driver_if_t driver_if = {.self = &hw_ctx, .update_rgb = mock_update_rgb};

    zmk_rgb_app_t app;
    zmk_rgb_construct(&app, EDGE_MOD_ZMK_RGB, 50, &driver_if);
    assert_int_equal(zmk_rgb_init(&app), EDGE_OK);

    assert_true(app.on);
    assert_int_equal(hw_ctx.hue, 0);

    // Set hue to 180 (Cyan), sat to 80, brightness to 50
    assert_int_equal(zmk_rgb_set_hue(&app, 180), EDGE_OK);
    assert_int_equal(hw_ctx.hue, 180);

    assert_int_equal(zmk_rgb_set_saturation(&app, 80), EDGE_OK);
    assert_int_equal(hw_ctx.sat, 80);

    assert_int_equal(zmk_rgb_set_brightness(&app, 50), EDGE_OK);
    assert_int_equal(hw_ctx.val, 50);

    // Next effect
    assert_int_equal(zmk_rgb_next_effect(&app), EDGE_OK);
    assert_int_equal(app.effect, ZMK_RGB_EFFECT_BREATHE);
    assert_int_equal(hw_ctx.effect, ZMK_RGB_EFFECT_BREATHE);

    // Toggle off
    assert_int_equal(zmk_rgb_toggle(&app), EDGE_OK);
    assert_false(app.on);
    assert_false(hw_ctx.on);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_rgb_controls_and_effects),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
