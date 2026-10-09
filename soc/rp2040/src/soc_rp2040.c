#include "soc_rp2040/soc_rp2040.h"

#define SIO_GPIO_MASK(pin) ((uint32_t)1u << (((uint32_t)(pin)) & 0x1Fu))

void soc_rp2040_gpio_init(soc_rp2040_sio_regs_t *sio, uint8_t pin, bool is_output) {
    if (sio == NULL || pin >= 30u) {
        return;
    }
    const uint32_t mask = SIO_GPIO_MASK(pin);
    if (is_output) {
        sio->gpio_oe_set = mask;
        sio->gpio_oe |= mask;
    } else {
        sio->gpio_oe_clr = mask;
        sio->gpio_oe &= ~mask;
    }
}

void soc_rp2040_gpio_put(soc_rp2040_sio_regs_t *sio, uint8_t pin, bool value) {
    if (sio == NULL || pin >= 30u) {
        return;
    }
    const uint32_t mask = SIO_GPIO_MASK(pin);
    if (value) {
        sio->gpio_set = mask;
        sio->gpio_out |= mask;
    } else {
        sio->gpio_clr = mask;
        sio->gpio_out &= ~mask;
    }
}

bool soc_rp2040_gpio_get(const soc_rp2040_sio_regs_t *sio, uint8_t pin) {
    if (sio == NULL || pin >= 30u) {
        return false;
    }
    const uint32_t mask = SIO_GPIO_MASK(pin);
    return (sio->gpio_in & mask) != 0u;
}

void soc_rp2040_gpio_toggle(soc_rp2040_sio_regs_t *sio, uint8_t pin) {
    if (sio == NULL || pin >= 30u) {
        return;
    }
    const uint32_t mask = SIO_GPIO_MASK(pin);
    sio->gpio_togl = mask;
    sio->gpio_out ^= mask;
}

void soc_rp2040_wdt_start(soc_rp2040_wdt_regs_t *wdt, uint32_t delay_ms) {
    if (wdt == NULL) {
        return;
    }
    /* RP2040 watchdog counts at 1MHz (1us per tick). delay_ms * 1000 * 2 for HW errata */
    wdt->load = delay_ms * 2000u;
    wdt->scratch[7] = wdt->load;
    wdt->ctrl = 0x40000000u; /* Enable watchdog bit (bit 30) */
}

void soc_rp2040_wdt_feed(soc_rp2040_wdt_regs_t *wdt) {
    if (wdt == NULL) {
        return;
    }
    if (wdt->scratch[7] > 0u) {
        wdt->load = wdt->scratch[7];
    }
    wdt->ctrl |= 0x40000000u;
}

void soc_rp2040_wdt_reboot(soc_rp2040_wdt_regs_t *wdt, uint32_t pc, uint32_t sp,
                           uint32_t delay_ms) {
    if (wdt == NULL) {
        return;
    }
    wdt->scratch[4] = pc;
    wdt->scratch[5] = sp;
    soc_rp2040_wdt_start(wdt, delay_ms);
}

bool soc_rp2040_spi_init(soc_rp2040_spi_regs_t *spi, uint32_t baudrate_hz, uint8_t data_bits) {
    if (spi == NULL || data_bits < 4u || data_bits > 16u || baudrate_hz == 0u) {
        return false;
    }
    spi->sspcr0 = (uint32_t)(data_bits - 1u); /* DSS */
    spi->sspcr1 = 0x02u;                      /* SSE (SSP Enable) */
    spi->sspcpsr = 0x02u;                     /* Clock prescale */
    return true;
}

bool soc_rp2040_spi_transfer(soc_rp2040_spi_regs_t *spi, const uint8_t *tx_buf, uint8_t *rx_buf,
                             size_t len) {
    if (spi == NULL || (tx_buf == NULL && rx_buf == NULL) || len == 0u) {
        return false;
    }
    for (size_t i = 0u; i < len; ++i) {
        uint8_t tx_byte = tx_buf ? tx_buf[i] : 0xFFu;
        spi->sspdr = tx_byte;
        if (rx_buf != NULL) {
            rx_buf[i] = (uint8_t)(spi->sspdr & 0xFFu);
        }
    }
    return true;
}

bool soc_rp2040_i2c_init(soc_rp2040_i2c_regs_t *i2c, uint32_t baudrate_hz) {
    if (i2c == NULL || baudrate_hz == 0u) {
        return false;
    }
    i2c->ic_enable = 0u;
    i2c->ic_con = 0x65u; /* Master enabled, restart enabled, fast mode */
    i2c->ic_enable = 1u;
    return true;
}

bool soc_rp2040_i2c_write(soc_rp2040_i2c_regs_t *i2c, uint8_t addr, const uint8_t *data,
                          size_t len) {
    if (i2c == NULL || (data == NULL && len > 0u)) {
        return false;
    }
    i2c->ic_tar = addr;
    for (size_t i = 0u; i < len; ++i) {
        uint32_t cmd = data[i];
        if (i == len - 1u) {
            cmd |= 0x200u; /* STOP bit */
        }
        i2c->ic_data_cmd = cmd;
    }
    return true;
}

bool soc_rp2040_i2c_read(soc_rp2040_i2c_regs_t *i2c, uint8_t addr, uint8_t *data, size_t len) {
    if (i2c == NULL || data == NULL || len == 0u) {
        return false;
    }
    i2c->ic_tar = addr;
    for (size_t i = 0u; i < len; ++i) {
        uint32_t cmd = 0x100u; /* READ command bit */
        if (i == len - 1u) {
            cmd |= 0x200u; /* STOP bit */
        }
        i2c->ic_data_cmd = cmd;
        data[i] = (uint8_t)(i2c->ic_data_cmd & 0xFFu);
    }
    return true;
}
