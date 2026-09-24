/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include <cmocka.h>
/* clang-format on */

#include "edge/errors.h"
#include "edge/modules.h"
#include "throttle/throttle.h"

typedef struct mock_throttle_io {
    float raw_in;
    float cmd_out;
} mock_throttle_io_t;

static edge_status_t mock_throttle_read_raw(void *self, float *raw_norm) {
    mock_throttle_io_t *io = (mock_throttle_io_t *)self;
    *raw_norm = io->raw_in;
    return EDGE_OK;
}

static edge_status_t mock_throttle_set_cmd(void *self, float cmd) {
    mock_throttle_io_t *io = (mock_throttle_io_t *)self;
    io->cmd_out = cmd;
    return EDGE_OK;
}

static void test_throttle_deadband(void **state) {
    (void)state;
    /* utils_deadband(v, 0.1, 1.0): below the threshold the value is zeroed, above it
     * the band [0.1 .. 1.0] is stretched back to [0.0 .. 1.0]. */
    assert_float_equal(throttle_apply_deadband(0.05f, 0.1f), 0.0f, 1e-6f);
    assert_float_equal(throttle_apply_deadband(-0.08f, 0.1f), 0.0f, 1e-6f);
    assert_float_equal(throttle_apply_deadband(0.5f, 0.1f), (0.5f - 0.1f) / 0.9f, 1e-6f);
    assert_float_equal(throttle_apply_deadband(-0.5f, 0.1f), -((0.5f - 0.1f) / 0.9f), 1e-6f);
    assert_float_equal(throttle_apply_deadband(1.0f, 0.1f), 1.0f, 1e-6f);
    assert_float_equal(throttle_apply_deadband(-1.0f, 0.1f), -1.0f, 1e-6f);
}

/*
 * Values below are the reference firmware's util/utils_math.c utils_throttle_curve
 * output, so a rewritten curve cannot pass by agreeing with itself.
 */
static void test_throttle_curve_matches_reference(void **state) {
    (void)state;

    /* Every mode is the identity at curve = 0. */
    for (int mode = 0; mode < 4; mode++) {
        assert_float_equal(throttle_apply_curve(0.4f, 0.0f, 0.0f, mode), 0.4f, 1e-6f);
        assert_float_equal(throttle_apply_curve(-0.4f, 0.0f, 0.0f, mode), -0.4f, 1e-6f);
    }

    /* mode 0 (exponential), curve_acc = 1: 1 - (1 - x)^2 */
    assert_float_equal(throttle_apply_curve(0.5f, 1.0f, 0.0f, 0), 0.75f, 1e-6f);
    /* mode 1 (natural), curve 1: 1 - (e^(1-x) - 1)/(e - 1) */
    assert_float_equal(throttle_apply_curve(0.5f, 1.0f, 0.0f, 1),
                       1.0f - ((expf(0.5f) - 1.0f) / (expf(1.0f) - 1.0f)), 1e-6f);
    /* mode 2 (polynomial), curve 1: 1 - (1-x)/(1+x) */
    assert_float_equal(throttle_apply_curve(0.5f, 1.0f, 0.0f, 2), 1.0f - (0.5f / 1.5f), 1e-6f);
    /* mode 3 (linear): unchanged whatever the curve */
    assert_float_equal(throttle_apply_curve(0.5f, 1.0f, 1.0f, 3), 0.5f, 1e-6f);

    /* Braking side uses curve_brake, not curve_acc. */
    assert_float_equal(throttle_apply_curve(-0.5f, 0.0f, 1.0f, 0),
                       -(1.0f - (1.0f - 0.5f) * (1.0f - 0.5f)), 1e-6f);

    /* Inputs are clamped to [-1, 1]. */
    assert_float_equal(throttle_apply_curve(9.0f, 0.0f, 0.0f, 0), 1.0f, 1e-6f);
    assert_float_equal(throttle_apply_curve(-9.0f, 0.0f, 0.0f, 0), -1.0f, 1e-6f);
}

static void test_throttle_rate_limiting_ramp(void **state) {
    (void)state;
    /* Ramp up: current=0, target=1, rate=2.0/s, dt=0.1s -> step=0.2 */
    float out = throttle_apply_ramp(0.0f, 1.0f, 2.0f, 5.0f, 0.1f);
    assert_true(fabsf(out - 0.2f) < 1e-4f);

    /* Ramp down: current=1, target=0, rate=5.0/s, dt=0.1s -> step=0.5 -> out=0.5 */
    out = throttle_apply_ramp(1.0f, 0.0f, 2.0f, 5.0f, 0.1f);
    assert_true(fabsf(out - 0.5f) < 1e-4f);
}

static void test_throttle_module_step(void **state) {
    (void)state;
    mock_throttle_io_t io = {.raw_in = 0.8f, .cmd_out = 0.0f};

    throttle_input_port_t in_port = {
        .read_raw = mock_throttle_read_raw,
        .self = &io,
    };
    throttle_output_port_t out_port = {
        .set_command = mock_throttle_set_cmd,
        .self = &io,
    };

    throttle_curve_config_t cfg = {
        .deadband = 0.05f,
        .expo_acc = 0.0f,
        .expo_brake = 0.0f,
        .expo_mode = 0,
        .ramp_up_rate = 10.0f,
        .ramp_down_rate = 10.0f,
        .min_out = -1.0f,
        .max_out = 1.0f,
    };

    throttle_t th;
    throttle_construct(&th, EDGE_MOD_THROTTLE, 15, &in_port, &out_port, &cfg, 0.1f);
    assert_int_equal(throttle_init(&th), EDGE_OK);

    assert_int_equal(throttle_step(&th), EDGE_OK);
    assert_true(io.cmd_out > 0.0f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_throttle_deadband),
        cmocka_unit_test(test_throttle_curve_matches_reference),
        cmocka_unit_test(test_throttle_rate_limiting_ramp),
        cmocka_unit_test(test_throttle_module_step),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
