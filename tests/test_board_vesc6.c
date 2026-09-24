/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>

#include <cmocka.h>
/* clang-format on */

#include "edge/errors.h"
#include "soc_stm32f4/soc_stm32f4.h"
#include "vesc6/board.h"

static void test_board_vesc6_scaling(void **state) {
    (void)state;
    board_vesc6_t board;
    assert_int_equal(board_vesc6_init(&board), EDGE_OK);

    float current_scale = board_vesc6_get_current_scale(&board);
    float voltage_scale = board_vesc6_get_voltage_scale(&board);

    assert_true(current_scale > 0.0f);
    assert_true(voltage_scale > 0.0f);

    /* 100 counts current diff */
    float amps = board_vesc6_calc_current_a(&board, 100);
    assert_true(amps > 0.0f);

    /* Full 12-bit voltage: 4095 counts -> ~61.8V */
    float volts = board_vesc6_calc_voltage_v(&board, 4095);
    assert_true(volts > 60.0f && volts < 65.0f);
}

static void test_soc_stm32f4_calculations(void **state) {
    (void)state;
    /* 168MHz / (2 * 25kHz) = 3360 */
    uint32_t arr = soc_stm32f4_calc_pwm_arr(168000000u, 25000u);
    assert_int_equal(arr, 3360);

    /* Deadtime: 500ns at 168MHz -> t_dts = 5.95ns -> ~84 ticks (<128 -> register = 84) */
    uint8_t dt_reg = soc_stm32f4_calc_deadtime_reg(500.0f, 168000000u);
    assert_true(dt_reg >= 80 && dt_reg <= 90);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_board_vesc6_scaling),
        cmocka_unit_test(test_soc_stm32f4_calculations),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
