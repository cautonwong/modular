/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "motor_id/motor_id.h"

typedef struct mock_hw {
    float i_alpha;
    float i_beta;
    float v_bus;
    uint8_t hall;
    float last_v_alpha;
    float last_v_beta;
    float last_angle;
    bool stopped;
} mock_hw_t;

static edge_status_t mock_get_currents(void *self, float *i_alpha, float *i_beta) {
    mock_hw_t *hw = (mock_hw_t *)self;
    *i_alpha = hw->i_alpha;
    *i_beta = hw->i_beta;
    return EDGE_OK;
}

static edge_status_t mock_get_vbus(void *self, float *v_bus) {
    mock_hw_t *hw = (mock_hw_t *)self;
    *v_bus = hw->v_bus;
    return EDGE_OK;
}

static uint8_t mock_get_hall(void *self) {
    mock_hw_t *hw = (mock_hw_t *)self;
    return hw->hall;
}

static edge_status_t mock_set_voltage(void *self, float v_alpha, float v_beta) {
    mock_hw_t *hw = (mock_hw_t *)self;
    hw->last_v_alpha = v_alpha;
    hw->last_v_beta = v_beta;
    /* Simulate current response */
    hw->i_alpha = v_alpha / 0.1f; /* simulate 0.1 ohm resistance */
    return EDGE_OK;
}

static edge_status_t mock_set_openloop(void *self, float angle, float current) {
    mock_hw_t *hw = (mock_hw_t *)self;
    hw->last_angle = angle;
    (void)current;
    hw->hall = (uint8_t)(((uint32_t)(angle * 10.0f) % 6) + 1);
    return EDGE_OK;
}

static edge_status_t mock_stop(void *self) {
    mock_hw_t *hw = (mock_hw_t *)self;
    hw->stopped = true;
    return EDGE_OK;
}

static void test_motor_id_init_null(void **state) {
    (void)state;
    motor_id_app_t app;
    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 20u, NULL, NULL, NULL);
    assert_int_equal(motor_id_init(NULL), EDGE_EINVAL);
    assert_int_equal(motor_id_init(&app), EDGE_EINVAL);
}

static void test_motor_id_r_l_detection(void **state) {
    (void)state;
    motor_id_app_t app;
    mock_hw_t hw = {
        .v_bus = 24.0f,
        .i_alpha = 0.0f,
        .i_beta = 0.0f,
        .hall = 1,
        .stopped = false,
    };

    motor_id_config_t cfg = {
        .max_current = 10.0f,
        .samples_r = 20,
        .samples_l = 20,
    };

    motor_id_measure_port_t m_port = {
        .self = &hw,
        .get_currents = mock_get_currents,
        .get_vbus = mock_get_vbus,
        .get_hall = mock_get_hall,
    };

    motor_id_control_port_t c_port = {
        .self = &hw,
        .set_voltage_alpha_beta = mock_set_voltage,
        .set_openloop_angle = mock_set_openloop,
        .stop_inverter = mock_stop,
    };

    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 20u, &cfg, &m_port, &c_port);
    assert_int_equal(motor_id_init(&app), EDGE_OK);
    assert_int_equal(motor_id_start_r_l(&app), EDGE_OK);

    /* Run steps through R and L measurement */
    for (int i = 0; i < 50; i++) {
        assert_int_equal(motor_id_step(&app, 0.001f), EDGE_OK);
    }

    const motor_id_result_t *res = motor_id_get_result(&app);
    assert_non_null(res);
    assert_true(res->valid);
    assert_true(res->r_ohm > 0.01f);
    assert_true(res->l_henry > 0.0f);
    assert_true(hw.stopped);
}

static void test_motor_id_hall_calibration(void **state) {
    (void)state;
    motor_id_app_t app;
    mock_hw_t hw = {
        .v_bus = 24.0f,
        .hall = 1,
        .stopped = false,
    };

    motor_id_config_t cfg = {
        .max_current = 5.0f,
    };

    motor_id_measure_port_t m_port = {
        .self = &hw,
        .get_currents = mock_get_currents,
        .get_hall = mock_get_hall,
    };

    motor_id_control_port_t c_port = {
        .self = &hw,
        .set_voltage_alpha_beta = mock_set_voltage,
        .set_openloop_angle = mock_set_openloop,
        .stop_inverter = mock_stop,
    };

    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 20u, &cfg, &m_port, &c_port);
    assert_int_equal(motor_id_init(&app), EDGE_OK);
    assert_int_equal(motor_id_start_hall(&app), EDGE_OK);

    for (int i = 0; i < 65; i++) {
        assert_int_equal(motor_id_step(&app, 0.01f), EDGE_OK);
    }

    const motor_id_result_t *res = motor_id_get_result(&app);
    assert_non_null(res);
    assert_true(res->valid);
    assert_true(hw.stopped);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_motor_id_init_null),
        cmocka_unit_test(test_motor_id_r_l_detection),
        cmocka_unit_test(test_motor_id_hall_calibration),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
