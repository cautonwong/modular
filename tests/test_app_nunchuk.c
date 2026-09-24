/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "nunchuk/nunchuk.h"

typedef struct mock_nunchuk_hw {
    uint8_t js_y;
    bool btn_c;
    bool btn_z;
    bool ok;
} mock_nunchuk_hw_t;

static edge_status_t mock_read_nunchuk(void *self, uint8_t *js_x, uint8_t *js_y, int16_t *acc_x,
                                       int16_t *acc_y, int16_t *acc_z, bool *btn_c, bool *btn_z) {
    mock_nunchuk_hw_t *hw = (mock_nunchuk_hw_t *)self;
    if (!hw->ok) {
        return EDGE_EIO;
    }
    *js_x = 128;
    *js_y = hw->js_y;
    *acc_x = 0;
    *acc_y = 0;
    *acc_z = 0;
    *btn_c = hw->btn_c;
    *btn_z = hw->btn_z;
    return EDGE_OK;
}

static void test_nunchuk_init(void **state) {
    (void)state;
    nunchuk_app_t app;
    nunchuk_construct(&app, EDGE_MOD_NUNCHUK, 20u, NULL, NULL);
    assert_int_equal(nunchuk_init(NULL), EDGE_EINVAL);
    assert_int_equal(nunchuk_init(&app), EDGE_EINVAL);
}

static void test_nunchuk_controls(void **state) {
    (void)state;
    nunchuk_app_t app;
    mock_nunchuk_hw_t hw = {.js_y = 128, .btn_c = false, .btn_z = false, .ok = true};
    nunchuk_port_t port = {.self = &hw, .read_data = mock_read_nunchuk};
    nunchuk_config_t cfg = {.deadband = 0.05f, .timeout_s = 0.1f};

    nunchuk_construct(&app, EDGE_MOD_NUNCHUK, 20u, &cfg, &port);
    assert_int_equal(nunchuk_init(&app), EDGE_OK);

    /* Neutral joystick -> 0.0 */
    assert_int_equal(nunchuk_update(&app, 0.01f), EDGE_OK);
    assert_float_equal(nunchuk_get_output(&app), 0.0f, 0.001f);
    assert_true(nunchuk_is_connected(&app));

    /* Full forward (255) -> 0.99~1.0 */
    hw.js_y = 255;
    assert_int_equal(nunchuk_update(&app, 0.01f), EDGE_OK);
    assert_true(nunchuk_get_output(&app) > 0.9f);

    /* Disconnect timeout */
    hw.ok = false;
    for (int i = 0; i < 15; i++) {
        assert_int_equal(nunchuk_update(&app, 0.01f), EDGE_OK);
    }
    assert_false(nunchuk_is_connected(&app));
    assert_float_equal(nunchuk_get_output(&app), 0.0f, 0.001f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_nunchuk_init),
        cmocka_unit_test(test_nunchuk_controls),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
