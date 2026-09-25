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

/*
 * The paths the cadence test does not reach: the argument guards, a sensor that fails (the demand
 * goes to zero and active clears, but the module reports OK - a missing reading is not a fault),
 * the torque-and-cadence mode with its own scaling and both clamps, the normalisation clamp above
 * the maximum cadence, the accessors with no app, the power-off hook, and the construction that
 * repairs a zeroed configuration instead of leaving a hot loop behind.
 */
static void test_pas_edges(void **state) {
    (void)state;
    pas_app_t app;
    mock_pas_hw_t hw = {.cadence_rpm = 60.0f, .torque_nm = 2.0f, .ok = true};
    pas_port_t port = {
        .self = &hw, .read_cadence_rpm = mock_read_cadence, .read_torque_nm = mock_read_torque};
    pas_config_t cfg = {.type = PAS_SENSOR_TORQUE_AND_CADENCE,
                        .assist_ratio = 2.0f,
                        .min_cadence_rpm = 10.0f,
                        .max_cadence_rpm = 100.0f,
                        .max_motor_current_a = 20.0f};

    pas_construct(&app, EDGE_MOD_PAS, 20u, &cfg, &port);
    assert_int_equal(pas_init(&app), EDGE_OK);

    /* Argument guards. */
    assert_int_equal(pas_update(NULL, 0.01f), EDGE_EINVAL);
    assert_int_equal(pas_update(&app, -1.0f), EDGE_EINVAL);
    assert_float_equal(pas_get_current_demand(NULL), 0.0f, 1e-9f);
    assert_false(pas_is_active(NULL));
    assert_ptr_equal(pas_module(NULL), NULL);

    /* Torque mode: demand = torque * assist_ratio * (cadence / 60) = 2 * 2 * 1 = 4 A. */
    assert_int_equal(pas_update(&app, 0.01f), EDGE_OK);
    assert_float_equal(app.measured_torque_nm, 2.0f, 1e-6f);
    assert_float_equal(pas_get_current_demand(&app), 4.0f, 1e-4f);
    assert_true(pas_is_active(&app));

    /* Clamped to the configured maximum, not above it. */
    hw.torque_nm = 100.0f;
    assert_int_equal(pas_update(&app, 0.01f), EDGE_OK);
    assert_float_equal(pas_get_current_demand(&app), 20.0f, 1e-4f);

    /* A negative torque from a sensor sign slip cannot become negative current. */
    hw.torque_nm = -50.0f;
    assert_int_equal(pas_update(&app, 0.01f), EDGE_OK);
    assert_float_equal(pas_get_current_demand(&app), 0.0f, 1e-6f);

    /* A failing cadence sensor zeroes the demand and clears active, and still reports OK. */
    hw.ok = false;
    assert_int_equal(pas_update(&app, 0.01f), EDGE_OK);
    assert_float_equal(pas_get_current_demand(&app), 0.0f, 1e-9f);
    assert_false(pas_is_active(&app));

    /* The module hook's power-off is the safe state. */
    hw.ok = true;
    assert_int_equal(pas_update(&app, 0.01f), EDGE_OK);
    assert_true(pas_is_active(&app));
    assert_int_equal(pas_module(&app)->power_off(pas_module(&app)), EDGE_OK);
    assert_false(pas_is_active(&app));
    assert_float_equal(pas_get_current_demand(&app), 0.0f, 1e-9f);

    /* Cadence above the maximum clamps the normalised demand instead of overshooting. */
    pas_config_t cadence_cfg = cfg;
    cadence_cfg.type = PAS_SENSOR_CADENCE_ONLY;
    pas_app_t cadence_app;
    pas_construct(&cadence_app, EDGE_MOD_PAS, 20u, &cadence_cfg, &port);
    assert_int_equal(pas_init(&cadence_app), EDGE_OK);
    hw.cadence_rpm = 500.0f;
    assert_int_equal(pas_update(&cadence_app, 0.01f), EDGE_OK);
    assert_float_equal(pas_get_current_demand(&cadence_app), cadence_cfg.max_motor_current_a,
                       1e-4f);

    /* A zeroed configuration is repaired at construction. */
    pas_config_t zeroed = {0};
    pas_app_t repaired;
    pas_construct(&repaired, EDGE_MOD_PAS, 20u, &zeroed, &port);
    assert_true(repaired.config.min_cadence_rpm > 0.0f);
    assert_true(repaired.config.max_cadence_rpm > 0.0f);
    assert_true(repaired.config.assist_ratio > 0.0f);
    assert_true(repaired.config.max_motor_current_a > 0.0f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pas_init),
        cmocka_unit_test(test_pas_cadence_mode),
        cmocka_unit_test(test_pas_edges),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
