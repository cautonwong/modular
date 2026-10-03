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

static void test_rgb_hsv_conversion_and_rendering(void **state) {
    (void)state;
    // Pure Red: H=0, S=100, V=100 -> R=255, G=0, B=0
    uint8_t r = 0, g = 0, b = 0;
    zmk_rgb_hsv_to_rgb(0, 100, 100, &r, &g, &b);
    assert_int_equal(r, 255);
    assert_int_equal(g, 0);
    assert_int_equal(b, 0);

    // Pure Green: H=120, S=100, V=100 -> R=0, G=255, B=0
    zmk_rgb_hsv_to_rgb(120, 100, 100, &r, &g, &b);
    assert_int_equal(r, 0);
    assert_int_equal(g, 255);
    assert_int_equal(b, 0);

    // Pure Blue: H=240, S=100, V=100 -> R=0, G=0, B=255
    zmk_rgb_hsv_to_rgb(240, 100, 100, &r, &g, &b);
    assert_int_equal(r, 0);
    assert_int_equal(g, 0);
    assert_int_equal(b, 255);

    // Render frame across 4 LEDs
    zmk_rgb_app_t app;
    zmk_rgb_construct(&app, EDGE_MOD_ZMK_RGB, 50, NULL);
    zmk_rgb_init(&app);
    zmk_rgb_set_effect(&app, ZMK_RGB_EFFECT_RAINBOW);

    zmk_rgb_pixel_t pixels[4] = {0};
    zmk_rgb_render_frame(&app, 1000, 4, pixels);
    // At least one color channel in each pixel should be non-zero
    for (int i = 0; i < 4; i++) {
        assert_true(pixels[i].r > 0 || pixels[i].g > 0 || pixels[i].b > 0);
    }
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_rgb_controls_and_effects),
        cmocka_unit_test(test_rgb_hsv_conversion_and_rendering),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
