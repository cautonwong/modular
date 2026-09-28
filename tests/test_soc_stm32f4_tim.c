/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "soc_stm32f4/soc_stm32f4.h"

/*
 * The timer block is a block of RAM here. Its layout is the one the header asserts against the
 * manual, so what these cases pin is the sequence and the encodings - the thing a board does once
 * at bring-up and a driver then relies on, and the thing that cannot be checked by running it,
 * because there is no STM32F4 to run it on.
 */
static soc_stm32f4_tim_regs_t regs;

static const soc_stm32f4_tim_config_t vesc6_ish = {
    .timer_clk_hz = SOC_STM32F4_TIM_CLK_HZ,
    .switching_freq_hz = 25000u,
    .deadtime_ns = 660.0f,
};

static void test_soc_stm32f4_tim_center_aligned_setup(void **state) {
    (void)state;
    memset(&regs, 0, sizeof(regs));

    /* The period comes back rather than a status: at 25 kHz on a 168 MHz timer it is
     * 168e6 / (2 * 25e3) = 3360, and a refused configuration returns zero. */
    assert_int_equal(soc_stm32f4_tim_init(&regs, &vesc6_ish), 3360u);

    assert_int_equal(regs.arr, 3360u);
    assert_int_equal(regs.psc, 0u);
    assert_int_equal(regs.rcr, 0u);

    /* CR1 = CEN | CMS=01 | ARPE: center-aligned mode 1, ARR preloaded, counter running. */
    assert_int_equal(regs.cr1, 0xA1u);
    /* CCMR1/2 = OCxM=110 | OCxPE on both halves of each: PWM mode 1 with preload, the encoding
     * ChibiOS's TIMv1 PWM LLD uses for every channel it drives. */
    assert_int_equal(regs.ccmr1, 0x6868u);
    assert_int_equal(regs.ccmr2, 0x6868u);
    /* CCER = CC1E | CC1NE | CC2E | CC2NE | CC3E | CC3NE: the three phases and their complements. */
    assert_int_equal(regs.ccer, 0x555u);
    /* BDTR = the dead time the helper encodes, with both off-state selections on, and MOE clear:
     * a configured timer, and a motor that is not being driven. */
    assert_int_equal(regs.bdtr, (uint32_t)soc_stm32f4_calc_deadtime_reg(vesc6_ish.deadtime_ns,
                                                                        vesc6_ish.timer_clk_hz) |
                                    0x0C00u);
    assert_int_equal((regs.bdtr & 0x8000u), 0u);
    /* The update event happened once, to load PSC, ARR and the preloaded compares. */
    assert_int_equal(regs.egr, 0x1u);
}

static void test_soc_stm32f4_tim_duty_and_outputs(void **state) {
    (void)state;
    memset(&regs, 0, sizeof(regs));
    assert_int_equal(soc_stm32f4_tim_init(&regs, &vesc6_ish), 3360u);

    /* The three compares, with the update interrupt disabled around them and re-enabled after. */
    assert_true(soc_stm32f4_tim_set_duty_counts(&regs, 100u, 200u, 300u, false));
    assert_int_equal(regs.ccr1, 100u);
    assert_int_equal(regs.ccr2, 200u);
    assert_int_equal(regs.ccr3, 300u);
    assert_int_equal(regs.cr1, 0xA1u); /* UDIS cleared again, nothing else touched */

    /* One of the two timers takes its phases in the order 1, 3, 2. */
    assert_true(soc_stm32f4_tim_set_duty_counts(&regs, 100u, 200u, 300u, true));
    assert_int_equal(regs.ccr1, 100u);
    assert_int_equal(regs.ccr2, 300u);
    assert_int_equal(regs.ccr3, 200u);

    /* Past the period the compare would wrap, so it is refused and nothing is written. */
    assert_false(soc_stm32f4_tim_set_duty_counts(&regs, 3361u, 0u, 0u, false));
    assert_int_equal(regs.ccr1, 100u);
    assert_int_equal(regs.cr1, 0xA1u);
    assert_false(soc_stm32f4_tim_set_duty_counts(NULL, 0u, 0u, 0u, false));

    /* The period itself is a legal compare: full modulation, not an overflow. */
    assert_true(soc_stm32f4_tim_set_duty_counts(&regs, 3360u, 0u, 0u, false));
    assert_int_equal(regs.ccr1, 3360u);

    /* Enabling the outputs changes MOE and nothing else. */
    const uint32_t configured = regs.bdtr;
    soc_stm32f4_tim_enable_outputs(&regs);
    assert_int_equal(regs.bdtr, configured | 0x8000u);
    soc_stm32f4_tim_disable_outputs(&regs);
    assert_int_equal(regs.bdtr, configured);
    /* The null cases are no-ops rather than a write through nothing. */
    soc_stm32f4_tim_enable_outputs(NULL);
    soc_stm32f4_tim_disable_outputs(NULL);
}

static void test_soc_stm32f4_tim_refuses_impossible_configs(void **state) {
    (void)state;
    memset(&regs, 0, sizeof(regs));

    assert_int_equal(soc_stm32f4_tim_init(NULL, &vesc6_ish), 0u);
    assert_int_equal(soc_stm32f4_tim_init(&regs, NULL), 0u);

    soc_stm32f4_tim_config_t bad = vesc6_ish;
    bad.timer_clk_hz = 0u;
    assert_int_equal(soc_stm32f4_tim_init(&regs, &bad), 0u);

    bad = vesc6_ish;
    bad.switching_freq_hz = 0u;
    assert_int_equal(soc_stm32f4_tim_init(&regs, &bad), 0u);

    /* A frequency the clock cannot divide down to: 168e6 / (2 * 200e6) is zero counts, which
     * would leave the period at its reset value. */
    bad = vesc6_ish;
    bad.switching_freq_hz = 200000000u;
    assert_int_equal(soc_stm32f4_tim_init(&regs, &bad), 0u);
    /* Nothing was written by any of them. */
    assert_int_equal(regs.cr1, 0u);
    assert_int_equal(regs.arr, 0u);

    /* No dead time asked for is a legal configuration: the off-state selections are still set, so
     * the outputs know where to sit while MOE is clear. */
    soc_stm32f4_tim_config_t no_deadtime = vesc6_ish;
    no_deadtime.deadtime_ns = 0.0f;
    assert_int_equal(soc_stm32f4_tim_init(&regs, &no_deadtime), 3360u);
    assert_int_equal(regs.bdtr, 0x0C00u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_soc_stm32f4_tim_center_aligned_setup),
        cmocka_unit_test(test_soc_stm32f4_tim_duty_and_outputs),
        cmocka_unit_test(test_soc_stm32f4_tim_refuses_impossible_configs),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
