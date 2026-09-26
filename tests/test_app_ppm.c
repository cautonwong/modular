/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "ppm/ppm.h"

typedef struct mock_ppm_rcv {
    float pulse_us;
    bool signal_ok;
} mock_ppm_rcv_t;

static edge_status_t mock_read_pulse(void *self, float *pulse_us) {
    mock_ppm_rcv_t *rcv = (mock_ppm_rcv_t *)self;
    *pulse_us = rcv->pulse_us;
    return rcv->signal_ok ? EDGE_OK : EDGE_EIO;
}

static bool mock_signal_present(void *self) {
    mock_ppm_rcv_t *rcv = (mock_ppm_rcv_t *)self;
    return rcv->signal_ok;
}

static void test_ppm_init_validation(void **state) {
    (void)state;
    ppm_app_t app;
    ppm_construct(&app, EDGE_MOD_PPM, 20u, NULL, NULL);
    assert_int_equal(ppm_init(NULL), EDGE_EINVAL);
    assert_int_equal(ppm_init(&app), EDGE_EINVAL);
}

static void test_ppm_deadband_and_range(void **state) {
    (void)state;
    ppm_app_t app;
    mock_ppm_rcv_t rcv = {.pulse_us = 1500.0f, .signal_ok = true};
    ppm_receiver_port_t port = {
        .self = &rcv,
        .read_pulse_us = mock_read_pulse,
        .is_signal_present = mock_signal_present,
    };
    ppm_config_t cfg = {
        .mode = PPM_MODE_CURRENT,
        .pulse_min_us = 1000.0f,
        .pulse_max_us = 2000.0f,
        .pulse_center_us = 1500.0f,
        .pulse_deadband_us = 50.0f,
        .timeout_s = 0.1f,
        .safe_start = false,
    };

    ppm_construct(&app, EDGE_MOD_PPM, 20u, &cfg, &port);
    assert_int_equal(ppm_init(&app), EDGE_OK);

    /* Center within deadband -> output 0.0 */
    rcv.pulse_us = 1520.0f;
    assert_int_equal(ppm_update(&app, 0.01f), EDGE_OK);
    assert_float_equal(ppm_get_output(&app), 0.0f, 0.001f);

    /* Full throttle (2000us) -> output 1.0 */
    rcv.pulse_us = 2000.0f;
    assert_int_equal(ppm_update(&app, 0.01f), EDGE_OK);
    assert_float_equal(ppm_get_output(&app), 1.0f, 0.001f);

    /* Full brake (1000us) -> output -1.0 */
    rcv.pulse_us = 1000.0f;
    assert_int_equal(ppm_update(&app, 0.01f), EDGE_OK);
    assert_float_equal(ppm_get_output(&app), -1.0f, 0.001f);

    /* Timeout / signal loss */
    rcv.signal_ok = false;
    for (int i = 0; i < 15; i++) {
        assert_int_equal(ppm_update(&app, 0.01f), EDGE_OK);
    }
    assert_false(ppm_is_safe(&app));
    assert_float_equal(ppm_get_output(&app), 0.0f, 0.001f);
}

static void test_ppm_safe_start(void **state) {
    (void)state;
    ppm_app_t app;
    mock_ppm_rcv_t rcv = {.pulse_us = 1900.0f, .signal_ok = true};
    ppm_receiver_port_t port = {
        .self = &rcv,
        .read_pulse_us = mock_read_pulse,
        .is_signal_present = mock_signal_present,
    };
    ppm_config_t cfg = {
        .mode = PPM_MODE_CURRENT,
        .pulse_min_us = 1000.0f,
        .pulse_max_us = 2000.0f,
        .pulse_center_us = 1500.0f,
        .pulse_deadband_us = 50.0f,
        .timeout_s = 0.1f,
        .safe_start = true,
    };

    ppm_construct(&app, EDGE_MOD_PPM, 20u, &cfg, &port);
    assert_int_equal(ppm_init(&app), EDGE_OK);

    /* Started with high throttle -> output blocked by safe start */
    assert_int_equal(ppm_update(&app, 0.01f), EDGE_OK);
    assert_float_equal(ppm_get_output(&app), 0.0f, 0.001f);
    assert_false(ppm_is_safe(&app));

    /* Move to neutral -> safe start unlocks */
    rcv.pulse_us = 1500.0f;
    assert_int_equal(ppm_update(&app, 0.01f), EDGE_OK);
    assert_true(ppm_is_safe(&app));

    /* Now throttle applies */
    rcv.pulse_us = 2000.0f;
    assert_int_equal(ppm_update(&app, 0.01f), EDGE_OK);
    assert_float_equal(ppm_get_output(&app), 1.0f, 0.001f);
}

/*
 * The module's own hooks, the accessors' guards and the read-failure path. The hooks are what a
 * scheduler drives - a poll reads the receiver, and power-off is the safe state - and none of them
 * had a case. The failing read is checked as an invariant rather than an error code: whatever it
 * reports, a receiver that cannot be read must not leave a demand behind.
 */
static void test_ppm_hooks_guards_and_read_failure(void **state) {
    (void)state;
    mock_ppm_rcv_t rcv;
    memset(&rcv, 0, sizeof(rcv));
    rcv.pulse_us = 1500.0f;
    rcv.signal_ok = true;
    ppm_receiver_port_t port = {
        .self = &rcv, .read_pulse_us = mock_read_pulse, .is_signal_present = mock_signal_present};

    ppm_app_t app;
    ppm_construct(&app, EDGE_MOD_PPM, 20u, NULL, &port);
    assert_int_equal(ppm_init(&app), EDGE_OK);

    /* The hooks a scheduler drives. */
    assert_int_equal(app.module.poll(&app.module), EDGE_OK);
    assert_int_equal(app.module.on_event(&app.module, NULL), EDGE_OK);
    assert_int_equal(app.module.power_off(&app.module), EDGE_OK);
    assert_float_equal(ppm_get_output(&app), 0.0f, 1e-9f);
    assert_ptr_equal(ppm_module(&app), &app.module);

    /* A read that fails leaves no demand behind. */
    rcv.signal_ok = false;
    (void)ppm_update(&app, 0.01f);
    assert_float_equal(ppm_get_output(&app), 0.0f, 1e-9f);

    /* The guards. */
    ppm_construct(NULL, EDGE_MOD_PPM, 20u, NULL, &port);
    assert_int_equal(ppm_update(NULL, 0.01f), EDGE_EINVAL);
    assert_float_equal(ppm_get_output(NULL), 0.0f, 1e-9f);
    assert_float_equal(ppm_get_last_pulse_us(NULL), 0.0f, 1e-9f);
    assert_ptr_equal(ppm_module(NULL), NULL);
}

int main(void) {

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ppm_init_validation),
        cmocka_unit_test(test_ppm_hooks_guards_and_read_failure),
        cmocka_unit_test(test_ppm_deadband_and_range),
        cmocka_unit_test(test_ppm_safe_start),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
