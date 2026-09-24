#include "soc_stm32f4/soc_stm32f4.h"

uint32_t soc_stm32f4_calc_pwm_arr(uint32_t timer_clk_hz, uint32_t switching_freq_hz) {
    if (timer_clk_hz == 0 || switching_freq_hz == 0) {
        return 0;
    }
    /* Center-aligned PWM: ARR = f_tim / (2 * f_sw) */
    return timer_clk_hz / (2u * switching_freq_hz);
}

uint8_t soc_stm32f4_calc_deadtime_reg(float deadtime_ns, uint32_t timer_clk_hz) {
    if (deadtime_ns <= 0.0f || timer_clk_hz == 0) {
        return 0;
    }

    /* Timer clock period in nanoseconds: t_dts = 1e9 / timer_clk_hz */
    float t_dts_ns = 1.0e9f / (float)timer_clk_hz;
    uint32_t dt_ticks = (uint32_t)(deadtime_ns / t_dts_ns + 0.5f);

    if (dt_ticks <= 127) {
        return (uint8_t)dt_ticks;
    } else if (dt_ticks <= 254) {
        return (uint8_t)(0x80u | ((dt_ticks / 2u) - 64u));
    } else if (dt_ticks <= 504) {
        return (uint8_t)(0xC0u | ((dt_ticks / 8u) - 32u));
    } else if (dt_ticks <= 1008) {
        return (uint8_t)(0xE0u | ((dt_ticks / 16u) - 32u));
    } else {
        return 0xFFu; /* Maximum deadtime */
    }
}
