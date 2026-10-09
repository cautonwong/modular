/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "bma421/bma421.h"

static void test_bma421_unpack_and_axis_swap(void **state) {
    (void)state;
    // raw_x = 500 (0x01F4), raw_y = -300 (0xFED4), raw_z = 1024 (0x0400)
    uint8_t raw[6] = {
        0xF4, 0x01, // raw_x = 500
        0xD4, 0xFE, // raw_y = -300
        0x00, 0x04  // raw_z = 1024
    };

    bma421_accel_t accel;
    assert_int_equal(bma421_unpack_accel(raw, 1, &accel), EDGE_OK);

    // Swap check: accel.x == raw_y (-300), accel.y == raw_x (500), accel.z == 1024
    assert_int_equal(accel.x, -300);
    assert_int_equal(accel.y, 500);
    assert_int_equal(accel.z, 1024);
}

static void test_bma421_unpack_null(void **state) {
    (void)state;
    bma421_accel_t accel;
    uint8_t raw[6] = {0};
    assert_int_equal(bma421_unpack_accel(NULL, 1, &accel), EDGE_EINVAL);
    assert_int_equal(bma421_unpack_accel(raw, 1, NULL), EDGE_EINVAL);
    assert_int_equal(bma421_unpack_accel(raw, 0, &accel), EDGE_EINVAL);
}

static void test_bma421_step_counter(void **state) {
    (void)state;
    const uint8_t raw[4] = {0x39, 0x30, 0x00, 0x00}; /* 12345 steps */
    uint32_t steps = 0;
    assert_int_equal(bma421_unpack_step_counter(raw, &steps), EDGE_OK);
    assert_int_equal(steps, 12345u);

    assert_int_equal(bma421_unpack_step_counter(NULL, &steps), EDGE_EINVAL);
    assert_int_equal(bma421_unpack_step_counter(raw, NULL), EDGE_EINVAL);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_bma421_unpack_and_axis_swap),
        cmocka_unit_test(test_bma421_unpack_null),
        cmocka_unit_test(test_bma421_step_counter),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
