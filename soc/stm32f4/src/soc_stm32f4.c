#include "soc_stm32f4/soc_stm32f4.h"

uint32_t soc_stm32f4_calc_pwm_arr(uint32_t timer_clk_hz, uint32_t switching_freq_hz) {
    if (timer_clk_hz == 0 || switching_freq_hz == 0) {
        return 0;
    }
    /* Center-aligned PWM: ARR = f_tim / (2 * f_sw) */
    return timer_clk_hz / (2u * switching_freq_hz);
}

/*
 * The reference's own encoding, conf_general_calculate_deadtime (conf_general.c): a timebase in
 * nanoseconds per tick, the four ranges RM0090 18.4.13 defines, and a truncating float division
 * inside each of them. The rounding it does *not* do is the point - this function used to round to
 * the nearest tick, which is one count away from the reference wherever the division is inexact,
 * and the literals stay double because the reference's do.
 */
uint8_t soc_stm32f4_calc_deadtime_reg(float deadtime_ns, uint32_t timer_clk_hz) {
    if (deadtime_ns <= 0.0f || timer_clk_hz == 0) {
        return 0;
    }

    const double timebase = 1.0 / ((double)timer_clk_hz / 1000000.0) * 1000.0;
    if ((double)deadtime_ns <= timebase * 127.0) {
        return (uint8_t)((double)deadtime_ns / timebase);
    }
    if ((double)deadtime_ns <= (63.0 + 64.0) * 2.0 * timebase) {
        return (uint8_t)(((double)deadtime_ns / (2.0 * timebase) - 64.0) + 0x80u);
    }
    if ((double)deadtime_ns <= (31.0 + 32.0) * 8.0 * timebase) {
        return (uint8_t)(((double)deadtime_ns / (8.0 * timebase) - 32.0) + 0xC0u);
    }
    if ((double)deadtime_ns <= (31.0 + 32.0) * 16.0 * timebase) {
        return (uint8_t)(((double)deadtime_ns / (16.0 * timebase) - 32.0) + 0xE0u);
    }
    /* Longer than the longest range: the reference sets the maximum and complains; there is no
     * one to complain to here, so the value is the maximum and the caller can see it is 0xFF. */
    return 0xFFu;
}
