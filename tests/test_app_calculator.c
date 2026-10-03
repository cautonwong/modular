#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "calculator/calculator.h"
#include "edge/modules.h"

static void test_calculator_basic_arithmetic(void **state) {
    (void)state;
    calculator_app_t calc;
    assert_int_equal(calculator_init(&calc), EDGE_OK);
    assert_int_equal(calc.module.module_id, EDGE_MOD_CALCULATOR);

    /* 12 + 8 = 20 */
    calculator_input_digit(&calc, 1);
    calculator_input_digit(&calc, 2);
    assert_int_equal(calculator_get_display_value(&calc), 12LL * CALCULATOR_FIXED_POINT_OFFSET);

    calculator_input_op(&calc, CALC_OP_ADD);
    calculator_input_digit(&calc, 8);
    calculator_input_equals(&calc);

    assert_int_equal(calculator_get_result(&calc), 20LL * CALCULATOR_FIXED_POINT_OFFSET);
    assert_int_equal(calculator_get_error(&calc), CALC_ERR_NONE);

    /* 20 * 3 = 60 */
    calculator_input_op(&calc, CALC_OP_MUL);
    calculator_input_digit(&calc, 3);
    calculator_input_equals(&calc);
    assert_int_equal(calculator_get_result(&calc), 60LL * CALCULATOR_FIXED_POINT_OFFSET);

    /* 60 - 15 = 45 */
    calculator_input_op(&calc, CALC_OP_SUB);
    calculator_input_digit(&calc, 1);
    calculator_input_digit(&calc, 5);
    calculator_input_equals(&calc);
    assert_int_equal(calculator_get_result(&calc), 45LL * CALCULATOR_FIXED_POINT_OFFSET);

    /* 45 / 9 = 5 */
    calculator_input_op(&calc, CALC_OP_DIV);
    calculator_input_digit(&calc, 9);
    calculator_input_equals(&calc);
    assert_int_equal(calculator_get_result(&calc), 5LL * CALCULATOR_FIXED_POINT_OFFSET);
}

static void test_calculator_division_by_zero(void **state) {
    (void)state;
    calculator_app_t calc;
    calculator_construct(&calc, EDGE_MOD_CALCULATOR, 50u);

    /* 42 / 0 = Error */
    calculator_input_digit(&calc, 4);
    calculator_input_digit(&calc, 2);
    calculator_input_op(&calc, CALC_OP_DIV);
    calculator_input_digit(&calc, 0);
    calculator_input_equals(&calc);

    assert_int_equal(calculator_get_error(&calc), CALC_ERR_DIV_BY_ZERO);
}

static void test_calculator_sign_flip_and_backspace(void **state) {
    (void)state;
    calculator_app_t calc;
    calculator_construct(&calc, EDGE_MOD_CALCULATOR, 50u);

    calculator_input_digit(&calc, 7);
    calculator_input_digit(&calc, 5);
    assert_int_equal(calculator_get_display_value(&calc), 75LL * CALCULATOR_FIXED_POINT_OFFSET);

    calculator_input_sign_flip(&calc);
    assert_int_equal(calculator_get_display_value(&calc), -75LL * CALCULATOR_FIXED_POINT_OFFSET);

    calculator_input_sign_flip(&calc);
    assert_int_equal(calculator_get_display_value(&calc), 75LL * CALCULATOR_FIXED_POINT_OFFSET);

    calculator_input_backspace(&calc);
    assert_int_equal(calculator_get_display_value(&calc), 7LL * CALCULATOR_FIXED_POINT_OFFSET);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_calculator_basic_arithmetic),
        cmocka_unit_test(test_calculator_division_by_zero),
        cmocka_unit_test(test_calculator_sign_flip_and_backspace),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
