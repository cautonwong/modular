/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "balance/balance.h"

typedef struct mock_balance_hw {
    float pitch_deg;
    float roll_deg;
    float gyro_pitch_dps;
    float gyro_roll_dps;
    bool sw1;
    bool sw2;
    bool ok;
} mock_balance_hw_t;

static edge_status_t mock_read_att(void *self, float *pitch, float *roll, float *gp, float *gr,
                                   bool *sw1, bool *sw2) {
    mock_balance_hw_t *hw = (mock_balance_hw_t *)self;
    if (!hw->ok) {
        return EDGE_EIO;
    }
    *pitch = hw->pitch_deg;
    *roll = hw->roll_deg;
    *gp = hw->gyro_pitch_dps;
    *gr = hw->gyro_roll_dps;
    *sw1 = hw->sw1;
    *sw2 = hw->sw2;
    return EDGE_OK;
}

static void test_balance_init(void **state) {
    (void)state;
    balance_app_t app;
    balance_construct(&app, EDGE_MOD_BALANCE, 10u, NULL, NULL);
    assert_int_equal(balance_init(NULL), EDGE_EINVAL);
    assert_int_equal(balance_init(&app), EDGE_EINVAL);
}

static void test_balance_pid_and_safety(void **state) {
    (void)state;
    balance_app_t app;
    mock_balance_hw_t hw = {
        .pitch_deg = 0.0f,
        .roll_deg = 0.0f,
        .gyro_pitch_dps = 0.0f,
        .sw1 = false,
        .sw2 = false,
        .ok = true,
    };
    balance_port_t port = {.self = &hw, .read_attitude = mock_read_att};
    balance_config_t cfg = {
        .kp = 2.0f,
        .ki = 0.5f,
        .kd = 0.1f,
        .max_current_a = 30.0f,
        .fault_pitch_deg = 45.0f,
        .fault_roll_deg = 45.0f,
    };

    balance_construct(&app, EDGE_MOD_BALANCE, 10u, &cfg, &port);
    assert_int_equal(balance_init(&app), EDGE_OK);

    /* Not stepped on footpad -> 0 current */
    assert_int_equal(balance_update(&app, 0.0f, 0.01f), EDGE_OK);
    assert_float_equal(balance_get_current_demand(&app), 0.0f, 0.001f);
    assert_false(balance_is_engaged(&app));

    /* Step on footpad with forward pitch (+5 deg) -> positive torque demand */
    hw.sw1 = true;
    hw.pitch_deg = 5.0f;
    assert_int_equal(balance_update(&app, 0.0f, 0.01f), EDGE_OK);
    assert_true(balance_is_engaged(&app));
    assert_true(balance_get_current_demand(&app) > 8.0f);

    /* Extreme roll fault (+60 deg) -> disarm */
    hw.roll_deg = 60.0f;
    assert_int_equal(balance_update(&app, 0.0f, 0.01f), EDGE_OK);
    assert_float_equal(balance_get_current_demand(&app), 0.0f, 0.001f);
    assert_false(balance_is_engaged(&app));
}

/*
 * The paths the init and PID cases do not reach. The assertions are invariants rather than
 * expected values, because the control law's sign convention is not what is under test here: the
 * demand always stays inside the configured maximum, a failed attitude read faults the app and
 * drops the demand, and the three answers is_engaged can give are all reachable.
 */
static void test_balance_edges_and_guards(void **state) {
    (void)state;
    mock_balance_hw_t hw;
    memset(&hw, 0, sizeof(hw));
    hw.ok = true;
    balance_port_t port = {.self = &hw, .read_attitude = mock_read_att};
    balance_config_t cfg = {.kp = 0.5f,
                            .ki = 0.1f,
                            .kd = 0.01f,
                            .max_current_a = 8.0f,
                            .tiltback_speed_rpm = 1000.0f,
                            .tiltback_angle_deg = 10.0f};

    /* Guards: construct with nothing is a no-op, and update insists on a positive dt. */
    balance_construct(NULL, EDGE_MOD_BALANCE, 10u, &cfg, &port);
    balance_app_t app;
    balance_construct(&app, EDGE_MOD_BALANCE, 10u, &cfg, &port);
    assert_int_equal(balance_init(&app), EDGE_OK);
    assert_int_equal(balance_update(NULL, 0.0f, 0.01f), EDGE_EINVAL);
    assert_int_equal(balance_update(&app, 0.0f, 0.0f), EDGE_EINVAL);

    /* The module's own hooks, including the power-off that is the safe state. */
    assert_int_equal(app.module.on_event(&app.module, NULL), EDGE_OK);
    assert_int_equal(app.module.poll(&app.module), EDGE_OK);
    assert_int_equal(app.module.power_off(&app.module), EDGE_OK);
    assert_float_equal(app.current_demand_a, 0.0f, 1e-9f);
    assert_true(app.module.power_off(&app.module) == EDGE_OK);

    /* Over the tiltback threshold in both directions, and far past it: the demand stays inside
     * the configured maximum either way. */
    assert_int_equal(balance_update(&app, 5000.0f, 0.01f), EDGE_OK);
    assert_true(fabsf(app.current_demand_a) <= cfg.max_current_a);
    assert_int_equal(balance_update(&app, -5000.0f, 0.01f), EDGE_OK);
    assert_true(fabsf(app.current_demand_a) <= cfg.max_current_a);
    assert_int_equal(balance_update(&app, 100000.0f, 0.01f), EDGE_OK);
    assert_true(fabsf(app.current_demand_a) <= cfg.max_current_a);

    /* A failed attitude read faults the app and drops the demand, and a faulted app is not
     * engaged even with the foot on the pad. */
    app.foot_engaged = true;
    hw.ok = false;
    assert_int_equal(balance_update(&app, 0.0f, 0.01f), EDGE_EIO);
    assert_true(app.faulted);
    assert_float_equal(app.current_demand_a, 0.0f, 1e-9f);
    assert_false(balance_is_engaged(&app));

    /* Engaged with the foot on the pad and no fault, and never for a null app. */
    app.faulted = false;
    app.foot_engaged = true;
    assert_true(balance_is_engaged(&app));
    app.foot_engaged = false;
    assert_false(balance_is_engaged(&app));
    assert_false(balance_is_engaged(NULL));
    assert_ptr_equal(balance_module(&app), &app.module);
    assert_ptr_equal(balance_module(NULL), NULL);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_balance_init),
        cmocka_unit_test(test_balance_pid_and_safety),
        cmocka_unit_test(test_balance_edges_and_guards),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
