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

/*
 * mcpwm.c:2694-3010's own table: which of the timer's three channels each commutation step drives,
 * and in which role. Every row here is read off that function's branches: a step drives one channel
 * positively, one negatively and leaves the third inactive, and flipping the direction swaps the
 * two that drive while the idle channel follows the step.
 */
static void test_board_vesc6_comm_step_channels(void **state) {
    (void)state;

    static const struct {
        int step;
        bool direction;
        board_vesc6_phase_role_t channels[3];
    } goldens[] = {
        {1,
         true,
         {BOARD_VESC6_PHASE_FLOAT, BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PHASE_NEGATIVE}},
        {1,
         false,
         {BOARD_VESC6_PHASE_FLOAT, BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PHASE_POSITIVE}},
        {2,
         true,
         {BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PHASE_FLOAT, BOARD_VESC6_PHASE_NEGATIVE}},
        {2,
         false,
         {BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PHASE_FLOAT}},
        {3,
         true,
         {BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PHASE_FLOAT}},
        {3,
         false,
         {BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PHASE_FLOAT, BOARD_VESC6_PHASE_NEGATIVE}},
        {4,
         true,
         {BOARD_VESC6_PHASE_FLOAT, BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PHASE_POSITIVE}},
        {4,
         false,
         {BOARD_VESC6_PHASE_FLOAT, BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PHASE_NEGATIVE}},
        {5,
         true,
         {BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PHASE_FLOAT, BOARD_VESC6_PHASE_POSITIVE}},
        {5,
         false,
         {BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PHASE_FLOAT}},
        {6,
         true,
         {BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PHASE_FLOAT}},
        {6,
         false,
         {BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PHASE_FLOAT, BOARD_VESC6_PHASE_POSITIVE}},
    };

    for (size_t i = 0; i < sizeof(goldens) / sizeof(goldens[0]); i++) {
        board_vesc6_phase_role_t roles[3];
        board_vesc6_comm_step_channels(goldens[i].step, goldens[i].direction, roles);
        for (int channel = 0; channel < 3; channel++) {
            assert_int_equal(roles[channel], goldens[i].channels[channel]);
        }

        /* Exactly one channel of each role: that is what makes a step a step. */
        int positive = 0;
        int negative = 0;
        int floating = 0;
        for (int channel = 0; channel < 3; channel++) {
            positive += roles[channel] == BOARD_VESC6_PHASE_POSITIVE ? 1 : 0;
            negative += roles[channel] == BOARD_VESC6_PHASE_NEGATIVE ? 1 : 0;
            floating += roles[channel] == BOARD_VESC6_PHASE_FLOAT ? 1 : 0;
        }
        assert_int_equal(positive, 1);
        assert_int_equal(negative, 1);
        assert_int_equal(floating, 1);
    }

    /* A step outside the six is the reference's own end: the first channel idle, rest untouched. */
    board_vesc6_phase_role_t none[3];
    board_vesc6_comm_step_channels(0, true, none);
    assert_int_equal(none[0], BOARD_VESC6_PHASE_FLOAT);
    board_vesc6_comm_step_channels(7, false, none);
    assert_int_equal(none[0], BOARD_VESC6_PHASE_FLOAT);

    /* Nothing to write into. */
    board_vesc6_comm_step_channels(1, true, NULL);
}

/*
 * The four locals the reference sets before its branches, and the switch inside them
 * (mcpwm.c:2718-2749): the idle channel is the same in every pulse mode, the positive one is PWM1
 * with both outputs except under the non-synchronous high-side mode, the negative one is inactive
 * except under the bipolar mode where it is PWM2 - and none of that applies while something is
 * being detected.
 */
static void test_board_vesc6_comm_step_settings(void **state) {
    (void)state;
    board_vesc6_phase_out_t out;

    for (int mode = 0; mode < 3; mode++) {
        board_vesc6_comm_step_settings(BOARD_VESC6_PHASE_FLOAT, (board_vesc6_pwm_mode_t)mode, false,
                                       &out);
        assert_int_equal(out.oc_mode, BOARD_VESC6_OC_INACTIVE);
        assert_true(out.ccx_enable);
        assert_false(out.ccxn_enable);
    }

    board_vesc6_comm_step_settings(BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PWM_SYNCHRONOUS, false,
                                   &out);
    assert_int_equal(out.oc_mode, BOARD_VESC6_OC_PWM1);
    assert_true(out.ccxn_enable);

    board_vesc6_comm_step_settings(BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PWM_NONSYNCHRONOUS_HISW,
                                   false, &out);
    assert_int_equal(out.oc_mode, BOARD_VESC6_OC_PWM1);
    assert_false(out.ccxn_enable);

    board_vesc6_comm_step_settings(BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PWM_BIPOLAR, false,
                                   &out);
    assert_true(out.ccxn_enable);

    board_vesc6_comm_step_settings(BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PWM_SYNCHRONOUS, false,
                                   &out);
    assert_int_equal(out.oc_mode, BOARD_VESC6_OC_INACTIVE);
    assert_true(out.ccx_enable);
    assert_true(out.ccxn_enable);

    board_vesc6_comm_step_settings(BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PWM_BIPOLAR, false,
                                   &out);
    assert_int_equal(out.oc_mode, BOARD_VESC6_OC_PWM2);

    /* While detecting, the pulse mode is not consulted at all. */
    board_vesc6_comm_step_settings(BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PWM_NONSYNCHRONOUS_HISW,
                                   true, &out);
    assert_true(out.ccxn_enable);
    board_vesc6_comm_step_settings(BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PWM_BIPOLAR, true, &out);
    assert_int_equal(out.oc_mode, BOARD_VESC6_OC_INACTIVE);

    board_vesc6_comm_step_settings(BOARD_VESC6_PHASE_FLOAT, BOARD_VESC6_PWM_SYNCHRONOUS, false,
                                   NULL);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_board_vesc6_scaling),
        cmocka_unit_test(test_board_vesc6_comm_step_channels),
        cmocka_unit_test(test_board_vesc6_comm_step_settings),
        cmocka_unit_test(test_soc_stm32f4_calculations),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
