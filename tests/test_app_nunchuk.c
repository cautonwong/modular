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

/*
 * The cruise branches and the lifecycle hooks were untested: the existing suite
 * only walked a straight stick input and the timeout. Cruise is the one piece of
 * behaviour here that latches state, so it is worth pinning.
 */
static void test_nunchuk_cruise_and_lifecycle(void **state) {
    (void)state;
    mock_nunchuk_hw_t hw = {.js_y = 128, .btn_c = false, .btn_z = false, .ok = true};
    nunchuk_port_t port = {.read_data = mock_read_nunchuk, .self = &hw};
    nunchuk_config_t cfg = {.deadband = 0.05f, .timeout_s = 0.2f};
    nunchuk_app_t app;

    nunchuk_construct(NULL, EDGE_MOD_NUNCHUK, 25u, &cfg, &port);
    nunchuk_construct(&app, EDGE_MOD_NUNCHUK, 25u, &cfg, &port);
    assert_int_equal(nunchuk_init(&app), EDGE_OK);
    assert_ptr_equal(nunchuk_module(NULL), NULL);

    /* Argument guards. */
    assert_int_equal(nunchuk_update(NULL, 0.01f), EDGE_EINVAL);
    assert_int_equal(nunchuk_update(&app, -1.0f), EDGE_EINVAL);
    assert_false(nunchuk_is_connected(NULL));
    assert_float_equal(nunchuk_get_output(NULL), 0.0f, 1e-9f);

    /* Inside the deadband the stick reads as centre. */
    hw.js_y = 130; /* (130 - 128) / 128 = 0.016 */
    assert_int_equal(nunchuk_update(&app, 0.01f), EDGE_OK);
    assert_float_equal(nunchuk_get_output(&app), 0.0f, 1e-6f);

    /* Button C latches the current deflection as a cruise setpoint ... */
    hw.js_y = 192; /* 0.5 */
    hw.btn_c = true;
    assert_int_equal(nunchuk_update(&app, 0.01f), EDGE_OK);
    assert_float_equal(nunchuk_get_output(&app), 0.5f, 1e-3f);

    /* ... and holding it keeps the output there while the stick returns to centre. */
    hw.js_y = 128;
    assert_int_equal(nunchuk_update(&app, 0.01f), EDGE_OK);
    assert_float_equal(nunchuk_get_output(&app), 0.5f, 1e-3f);

    /* Releasing C drops the cruise and the stick takes over again. */
    hw.btn_c = false;
    hw.js_y = 160; /* 0.25 */
    assert_int_equal(nunchuk_update(&app, 0.01f), EDGE_OK);
    assert_float_equal(nunchuk_get_output(&app), 0.25f, 1e-3f);

    /* power_off is the safe state, and the module hook reaches it. */
    assert_int_equal(nunchuk_module(&app)->power_off(nunchuk_module(&app)), EDGE_OK);
    assert_float_equal(nunchuk_get_output(&app), 0.0f, 1e-6f);
    assert_false(nunchuk_is_connected(&app));

    /* Unusable config values are repaired at construction, not left to make a hot
     * loop or an always-centred stick. */
    nunchuk_config_t bad = {.deadband = 0.0f, .timeout_s = 0.0f};
    nunchuk_app_t app2;
    nunchuk_construct(&app2, EDGE_MOD_NUNCHUK, 25u, &bad, &port);
    assert_float_equal(app2.config.deadband, 0.05f, 1e-6f);
    assert_float_equal(app2.config.timeout_s, 0.2f, 1e-6f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_nunchuk_init),
        cmocka_unit_test(test_nunchuk_controls),
        cmocka_unit_test(test_nunchuk_cruise_and_lifecycle),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
