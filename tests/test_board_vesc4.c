/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "vesc4/board.h"

static void test_vesc4_init(void **state) {
    (void)state;
    board_vesc4_t brd;
    assert_int_equal(board_vesc4_init(NULL), EDGE_EINVAL);
    assert_int_equal(board_vesc4_init(&brd), EDGE_OK);
    assert_true(brd.initialized);
    assert_true(board_vesc4_get_current_scale(&brd) > 0.0f);
    assert_true(board_vesc4_get_voltage_scale(&brd) > 0.0f);
}

static void test_vesc4_calcs(void **state) {
    (void)state;
    board_vesc4_t brd;
    assert_int_equal(board_vesc4_init(&brd), EDGE_OK);

    float current = board_vesc4_calc_current_a(&brd, 100);
    assert_true(current > 0.0f);

    float voltage = board_vesc4_calc_voltage_v(&brd, 4095);
    assert_true(voltage > 60.0f && voltage < 65.0f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_vesc4_init),
        cmocka_unit_test(test_vesc4_calcs),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
