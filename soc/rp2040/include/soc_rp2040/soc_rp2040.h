#ifndef SOC_RP2040_H
#define SOC_RP2040_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RP2040 SIO (Single-cycle IO) Registers Layout */
typedef struct soc_rp2040_sio_regs {
    uint32_t cpuid;
    uint32_t gpio_in;
    uint32_t gpio_hi_in;
    uint32_t pad0;
    uint32_t gpio_out;
    uint32_t gpio_set;
    uint32_t gpio_clr;
    uint32_t gpio_togl;
    uint32_t gpio_oe;
    uint32_t gpio_oe_set;
    uint32_t gpio_oe_clr;
    uint32_t gpio_oe_togl;
} soc_rp2040_sio_regs_t;

/* RP2040 Watchdog Registers Layout */
typedef struct soc_rp2040_wdt_regs {
    uint32_t ctrl;
    uint32_t load;
    uint32_t reason;
    uint32_t scratch[8];
    uint32_t tick;
} soc_rp2040_wdt_regs_t;

/* RP2040 SPI Registers Layout */
typedef struct soc_rp2040_spi_regs {
    uint32_t sspcr0;
    uint32_t sspcr1;
    uint32_t sspdr;
    uint32_t sspsr;
    uint32_t sspcpsr;
    uint32_t sspimsc;
    uint32_t sspris;
    uint32_t sspmis;
    uint32_t sspicr;
} soc_rp2040_spi_regs_t;

/* RP2040 I2C (DesignWare) Registers Layout */
typedef struct soc_rp2040_i2c_regs {
    uint32_t ic_con;
    uint32_t ic_tar;
    uint32_t ic_sar;
    uint32_t ic_data_cmd;
    uint32_t ic_ss_scl_hcnt;
    uint32_t ic_ss_scl_lcnt;
    uint32_t ic_fs_scl_hcnt;
    uint32_t ic_fs_scl_lcnt;
    uint32_t ic_intr_stat;
    uint32_t ic_intr_mask;
    uint32_t ic_raw_intr_stat;
    uint32_t ic_rx_tl;
    uint32_t ic_tx_tl;
    uint32_t ic_clr_intr;
    uint32_t ic_enable;
    uint32_t ic_status;
} soc_rp2040_i2c_regs_t;

/* SIO GPIO Operations */
void soc_rp2040_gpio_init(soc_rp2040_sio_regs_t *sio, uint8_t pin, bool is_output);
void soc_rp2040_gpio_put(soc_rp2040_sio_regs_t *sio, uint8_t pin, bool value);
bool soc_rp2040_gpio_get(const soc_rp2040_sio_regs_t *sio, uint8_t pin);
void soc_rp2040_gpio_toggle(soc_rp2040_sio_regs_t *sio, uint8_t pin);

/* Watchdog Operations */
void soc_rp2040_wdt_start(soc_rp2040_wdt_regs_t *wdt, uint32_t delay_ms);
void soc_rp2040_wdt_feed(soc_rp2040_wdt_regs_t *wdt);
void soc_rp2040_wdt_reboot(soc_rp2040_wdt_regs_t *wdt, uint32_t pc, uint32_t sp, uint32_t delay_ms);

/* SPI Operations */
bool soc_rp2040_spi_init(soc_rp2040_spi_regs_t *spi, uint32_t baudrate_hz, uint8_t data_bits);
bool soc_rp2040_spi_transfer(soc_rp2040_spi_regs_t *spi, const uint8_t *tx_buf, uint8_t *rx_buf,
                             size_t len);

/* I2C Operations */
bool soc_rp2040_i2c_init(soc_rp2040_i2c_regs_t *i2c, uint32_t baudrate_hz);
bool soc_rp2040_i2c_write(soc_rp2040_i2c_regs_t *i2c, uint8_t addr, const uint8_t *data,
                          size_t len);
bool soc_rp2040_i2c_read(soc_rp2040_i2c_regs_t *i2c, uint8_t addr, uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SOC_RP2040_H */
