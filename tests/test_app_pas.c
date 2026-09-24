/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "pas/pas.h"

typedef struct mock_pas_hw {
    float cadence_rpm;
    float torque_nm;
    bool ok;
} mock_pas_hw_t;

static edge_status_t mock_read_cadence(void *self, float *rpm) {
    mock_pas_hw_t *hw = (mock_pas_hw_t *)self;
    if (!hw->ok) {
        return EDGE_EIO;
    }
    *rpm = hw->cadence_rpm;
    return EDGE_OK;
}

static edge_status_t mock_read_torque(void *self, float *nm) {
    mock_pas_hw_t *hw = (mock_pas_hw_t *)self;
    if (!hw->ok) {
        return EDGE_EIO;
    }
    *nm = hw->torque_nm;
    return EDGE_OK;
}

static void test_pas_init(void **state) {
    (void)state;
    pas_app_t app;
    pas_construct(&app, EDGE_MOD_PAS, 20u, NULL, NULL);
    assert_int_equal(pas_init(NULL), EDGE_EINVAL);
    assert_int_equal(pas_init(&app), EDGE_EINVAL);
}

static void test_pas_cadence_mode(void **state) {
    (void)state;
    pas_app_t app;
    mock_pas_hw_t hw = {.cadence_rpm = 0.0f, .torque_nm = 0.0f, .ok = true};
    pas_port_t port = {
        .self = &hw, .read_cadence_rpm = mock_read_cadence, .read_torque_nm = mock_read_torque};
    pas_config_t cfg = {
        .type = PAS_SENSOR_CADENCE_ONLY,
        .assist_ratio = 1.0f,
        .min_cadence_rpm = 10.0f,
        .max_cadence_rpm = 100.0f,
        .max_motor_current_a = 20.0f,
    };

    pas_construct(&app, EDGE_MOD_PAS, 20u, &cfg, &port);
    assert_int_equal(pas_init(&app), EDGE_OK);

    /* 0 RPM -> 0 current demand, not active */
    assert_int_equal(pas_update(&app, 0.01f), EDGE_OK);
    assert_float_equal(pas_get_current_demand(&app), 0.0f, 0.001f);
    assert_false(pas_is_active(&app));

    /* 55 RPM -> ~50% assist (~10A) */
    hw.cadence_rpm = 55.0f;
    assert_int_equal(pas_update(&app, 0.01f), EDGE_OK);
    assert_true(pas_is_active(&app));
    assert_true(pas_get_current_demand(&app) > 8.0f && pas_get_current_demand(&app) < 12.0f);

    /* 100 RPM -> full 20A */
    hw.cadence_rpm = 100.0f;
    assert_int_equal(pas_update(&app, 0.01f), EDGE_OK);
    assert_float_equal(pas_get_current_demand(&app), 20.0f, 0.001f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pas_init),
        cmocka_unit_test(test_pas_cadence_mode),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
