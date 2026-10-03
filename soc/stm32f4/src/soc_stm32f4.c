#include "soc_stm32f4/soc_stm32f4.h"

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
    (void)addr;
    for (size_t i = 0u; i < len; ++i) {
        i2c->dr = data[i];
    }
    return true;
}

bool soc_stm32f4_i2c_read(soc_stm32f4_i2c_regs_t *i2c, uint8_t addr, uint8_t *data, size_t len) {
    if (i2c == NULL || data == NULL || len == 0u) {
        return false;
    }
    (void)addr;
    for (size_t i = 0u; i < len; ++i) {
        data[i] = (uint8_t)(i2c->dr & 0xFFu);
    }
    return true;
}
