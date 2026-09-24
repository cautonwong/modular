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

static void test_throttle_deadband_and_curve(void **state) {
    (void)state;
    /* Deadband 0.1: input <= 0.1 -> 0.0 */
    assert_true(fabsf(throttle_apply_curve(0.05f, 0.1f, 0.0f)) < 1e-4f);
    assert_true(fabsf(throttle_apply_curve(-0.08f, 0.1f, 0.0f)) < 1e-4f);

    /* Full throttle: 1.0 -> 1.0 */
    assert_true(fabsf(throttle_apply_curve(1.0f, 0.1f, 0.0f) - 1.0f) < 1e-4f);
    assert_true(fabsf(throttle_apply_curve(-1.0f, 0.1f, 0.0f) - (-1.0f)) < 1e-4f);
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
        .expo = 0.0f,
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
        cmocka_unit_test(test_throttle_deadband_and_curve),
        cmocka_unit_test(test_throttle_rate_limiting_ramp),
        cmocka_unit_test(test_throttle_module_step),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
