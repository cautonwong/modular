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

/*
 * The curve's negative-curve branches and the app's own guards. The existing tests sweep only
 * non-negative curves, so the half of each mode that applies below zero never ran; these assert
 * the properties that must hold either way - 0 maps to 0, 1 to 1, the magnitude stays inside the
 * unit interval and never decreases - rather than guessing each formula's value.
 */
static void test_throttle_negative_curve_and_app_guards(void **state) {
    (void)state;

    for (int mode = 0; mode < 4; mode++) {
        float previous = 0.0f;
        for (int i = 0; i <= 10; i++) {
            const float in = (float)i / 10.0f;
            const float out = throttle_apply_curve(in, -0.5f, -0.5f, mode);
            assert_true(out >= -1e-5f);
            assert_true(out <= 1.0f + 1e-5f);
            assert_true(out >= previous - 1e-5f);
            previous = out;
        }
        assert_float_equal(throttle_apply_curve(0.0f, -0.5f, -0.5f, mode), 0.0f, 1e-5f);
        assert_float_equal(throttle_apply_curve(1.0f, -0.5f, -0.5f, mode), 1.0f, 1e-4f);
    }

    /* The app's guards, which the function-level tests never touch. */
    assert_int_equal(throttle_init(NULL), EDGE_EINVAL);
    assert_int_equal(throttle_deinit(NULL), EDGE_EINVAL);
    assert_int_equal(throttle_step(NULL), EDGE_EINVAL);
    assert_float_equal(throttle_get_output(NULL), 0.0f, 1e-9f);
    assert_ptr_equal(throttle_module(NULL), NULL);
}

/* A port whose read fails, for the step's error path, and an output the tests do not care about. */
static edge_status_t failing_read(void *self, float *raw) {
    (void)self;
    *raw = 0.0f;
    return EDGE_EIO;
}

static edge_status_t sink_command(void *self, float cmd) {
    (void)self;
    (void)cmd;
    return EDGE_OK;
}

/*
 * The module's own hooks, the ramp's degenerate rates and the step's guards. The hooks are what a
 * scheduler calls and none of them had a case; a ramp rate of zero is the "go there now instead of
 * rate limiting" branch, and a non-positive dt is the "no time passed, nothing moves" one.
 */
static void test_throttle_hooks_and_ramp_edges(void **state) {
    (void)state;
    mock_throttle_io_t io;
    memset(&io, 0, sizeof(io));
    throttle_input_port_t in_port = {.self = &io, .read_raw = mock_throttle_read_raw};
    throttle_output_port_t out_port = {.self = &io, .set_command = sink_command};

    throttle_t thr_app;
    throttle_construct(&thr_app, EDGE_MOD_THROTTLE, 20u, &in_port, &out_port, NULL, 0.01f);
    assert_int_equal(throttle_init(&thr_app), EDGE_OK);

    /* The hooks a scheduler drives. */
    assert_int_equal(thr_app.module.poll(&thr_app.module), EDGE_OK);
    /* A null event is refused rather than treated as an event with nothing in it. */
    assert_int_equal(thr_app.module.on_event(&thr_app.module, NULL), EDGE_EINVAL);
    assert_int_equal(thr_app.module.power_off(&thr_app.module), EDGE_OK);
    assert_float_equal(throttle_get_output(&thr_app), 0.0f, 1e-6f);
    assert_int_equal(throttle_deinit(&thr_app), EDGE_OK);

    /* A ramp rate of zero reaches the target in one step, in both directions. */
    assert_float_equal(throttle_apply_ramp(0.0f, 1.0f, 0.0f, 0.0f, 0.01f), 1.0f, 1e-6f);
    assert_float_equal(throttle_apply_ramp(1.0f, 0.0f, 0.0f, 0.0f, 0.01f), 0.0f, 1e-6f);
    /* A non-positive dt moves nothing, and an unchanged target is left alone. */
    assert_float_equal(throttle_apply_ramp(0.25f, 1.0f, 10.0f, 10.0f, 0.0f), 0.25f, 1e-6f);
    assert_float_equal(throttle_apply_ramp(0.25f, 1.0f, 10.0f, 10.0f, -1.0f), 0.25f, 1e-6f);
    assert_float_equal(throttle_apply_ramp(0.5f, 0.5f, 10.0f, 10.0f, 0.01f), 0.5f, 1e-6f);

    /* The step guards and its error path. */
    assert_int_equal(throttle_step(NULL), EDGE_EINVAL);
    throttle_t bad;
    throttle_input_port_t bad_in = {.self = NULL, .read_raw = failing_read};
    throttle_construct(&bad, EDGE_MOD_THROTTLE, 20u, &bad_in, &out_port, NULL, 0.01f);
    assert_int_equal(throttle_init(&bad), EDGE_OK);
    assert_int_equal(throttle_step(&bad), EDGE_EIO);
}

int main(void) {

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_throttle_deadband),
        cmocka_unit_test(test_throttle_hooks_and_ramp_edges),
        cmocka_unit_test(test_throttle_curve_matches_reference),
        cmocka_unit_test(test_throttle_rate_limiting_ramp),
        cmocka_unit_test(test_throttle_negative_curve_and_app_guards),
        cmocka_unit_test(test_throttle_module_step),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
