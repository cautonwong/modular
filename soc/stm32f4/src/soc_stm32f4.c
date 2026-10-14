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

void soc_stm32f4_gpio_set_mode(soc_stm32f4_gpio_regs_t *gpio, uint8_t pin,
                               soc_stm32f4_gpio_mode_t mode) {
    if (gpio == NULL || pin >= 16u) {
        return;
    }
    const uint32_t shift = (uint32_t)pin * 2u;
    const uint32_t mask = 0x3u << shift;
    gpio->moder = (gpio->moder & ~mask) | (((uint32_t)mode & 0x3u) << shift);
}

void soc_stm32f4_gpio_write(soc_stm32f4_gpio_regs_t *gpio, uint8_t pin, bool value) {
    if (gpio == NULL || pin >= 16u) {
        return;
    }
    if (value) {
        gpio->bsrr = (1u << pin);
        gpio->odr |= (1u << pin);
    } else {
        gpio->bsrr = (1u << (pin + 16u));
        gpio->odr &= ~(1u << pin);
    }
}

bool soc_stm32f4_gpio_read(const soc_stm32f4_gpio_regs_t *gpio, uint8_t pin) {
    if (gpio == NULL || pin >= 16u) {
        return false;
    }
    return (gpio->idr & (1u << pin)) != 0u;
}

void soc_stm32f4_gpio_toggle(soc_stm32f4_gpio_regs_t *gpio, uint8_t pin) {
    if (gpio == NULL || pin >= 16u) {
        return;
    }
    gpio->odr ^= (1u << pin);
}

void soc_stm32f4_iwdg_start(soc_stm32f4_iwdg_regs_t *iwdg, uint16_t reload_val) {
    if (iwdg == NULL) {
        return;
    }
    iwdg->kr = SOC_STM32F4_IWDG_KEY_ENABLE;
    iwdg->kr = SOC_STM32F4_IWDG_KEY_ACCESS;
    iwdg->pr = 0x04u; /* Prescaler /64 */
    iwdg->rlr = reload_val & 0x0FFFu;
    iwdg->kr = SOC_STM32F4_IWDG_KEY_RELOAD;
}

void soc_stm32f4_iwdg_feed(soc_stm32f4_iwdg_regs_t *iwdg) {
    if (iwdg == NULL) {
        return;
    }
    iwdg->kr = SOC_STM32F4_IWDG_KEY_RELOAD;
}

bool soc_stm32f4_spi_init(soc_stm32f4_spi_regs_t *spi, uint16_t cr1_flags) {
    if (spi == NULL) {
        return false;
    }
    spi->cr1 = cr1_flags | 0x0040u; /* Enable SPE bit */
    return true;
}

bool soc_stm32f4_spi_transfer(soc_stm32f4_spi_regs_t *spi, const uint8_t *tx_buf, uint8_t *rx_buf,
                              size_t len) {
    if (spi == NULL || (tx_buf == NULL && rx_buf == NULL) || len == 0u) {
        return false;
    }
    for (size_t i = 0u; i < len; ++i) {
        uint8_t tx_byte = tx_buf ? tx_buf[i] : 0xFFu;
        spi->dr = tx_byte;
        if (rx_buf != NULL) {
            rx_buf[i] = (uint8_t)(spi->dr & 0xFFu);
        }
    }
    return true;
}

bool soc_stm32f4_i2c_init(soc_stm32f4_i2c_regs_t *i2c, uint16_t ccr_val) {
    if (i2c == NULL) {
        return false;
    }
    i2c->cr1 = 0x0001u; /* Enable PE */
    i2c->ccr = ccr_val;
    return true;
}

bool soc_stm32f4_i2c_write(soc_stm32f4_i2c_regs_t *i2c, uint8_t addr, const uint8_t *data,
                           size_t len) {
    if (i2c == NULL || (data == NULL && len > 0u)) {
        return false;
    }
    i2c->oar1 = (uint32_t)addr << 1u;
    i2c->dr = (uint32_t)addr << 1u;
    for (size_t i = 0u; i < len; ++i) {
        i2c->dr = data[i];
    }
    return true;
}

bool soc_stm32f4_i2c_read(soc_stm32f4_i2c_regs_t *i2c, uint8_t addr, uint8_t *data, size_t len) {
    if (i2c == NULL || data == NULL || len == 0u) {
        return false;
    }
    i2c->oar1 = ((uint32_t)addr << 1u) | 1u;
    i2c->dr = ((uint32_t)addr << 1u) | 1u;
    for (size_t i = 0u; i < len; ++i) {
        data[i] = (uint8_t)(i2c->dr & 0xFFu);
    }
    return true;
}
