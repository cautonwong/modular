#ifndef SOC_STM32F4_H
#define SOC_STM32F4_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Base Addresses for STM32F401 / STM32F411 */
#define SOC_STM32F4_GPIOA_BASE ((uintptr_t)0x40020000u)
#define SOC_STM32F4_GPIOB_BASE ((uintptr_t)0x40020400u)
#define SOC_STM32F4_GPIOC_BASE ((uintptr_t)0x40020800u)
#define SOC_STM32F4_GPIOD_BASE ((uintptr_t)0x40020C00u)
#define SOC_STM32F4_GPIOE_BASE ((uintptr_t)0x40021000u)
#define SOC_STM32F4_GPIOH_BASE ((uintptr_t)0x40021C00u)
#define SOC_STM32F4_RCC_BASE ((uintptr_t)0x40023800u)
#define SOC_STM32F4_SPI1_BASE ((uintptr_t)0x40013000u)
#define SOC_STM32F4_SPI2_BASE ((uintptr_t)0x40003800u)
#define SOC_STM32F4_I2C1_BASE ((uintptr_t)0x40005400u)
#define SOC_STM32F4_I2C2_BASE ((uintptr_t)0x40005800u)
#define SOC_STM32F4_IWDG_BASE ((uintptr_t)0x40003000u)

/* GPIO Register Map */
typedef struct soc_stm32f4_gpio_regs {
    volatile uint32_t moder;   /* 0x00 */
    volatile uint32_t otyper;  /* 0x04 */
    volatile uint32_t ospeedr; /* 0x08 */
    volatile uint32_t pupdr;   /* 0x0C */
    volatile uint32_t idr;     /* 0x10 */
    volatile uint32_t odr;     /* 0x14 */
    volatile uint32_t bsrr;    /* 0x18 */
    volatile uint32_t lckr;    /* 0x1C */
    volatile uint32_t afr[2];  /* 0x20..0x24 */
} soc_stm32f4_gpio_regs_t;

_Static_assert(offsetof(soc_stm32f4_gpio_regs_t, moder) == 0x00u, "GPIO MODER is at 0x00");
_Static_assert(offsetof(soc_stm32f4_gpio_regs_t, idr) == 0x10u, "GPIO IDR is at 0x10");
_Static_assert(offsetof(soc_stm32f4_gpio_regs_t, bsrr) == 0x18u, "GPIO BSRR is at 0x18");

/* RCC Register Map */
typedef struct soc_stm32f4_rcc_regs {
    volatile uint32_t cr;       /* 0x00 */
    volatile uint32_t pllcfgr;  /* 0x04 */
    volatile uint32_t cfgr;     /* 0x08 */
    volatile uint32_t cir;      /* 0x0C */
    volatile uint32_t ahb1rstr; /* 0x10 */
    volatile uint32_t ahb2rstr; /* 0x14 */
    volatile uint32_t ahb3rstr; /* 0x18 */
    uint32_t reserved0;         /* 0x1C */
    volatile uint32_t apb1rstr; /* 0x20 */
    volatile uint32_t apb2rstr; /* 0x24 */
    uint32_t reserved1[2];      /* 0x28..0x2C */
    volatile uint32_t ahb1enr;  /* 0x30 */
    volatile uint32_t ahb2enr;  /* 0x34 */
    volatile uint32_t ahb3enr;  /* 0x38 */
    uint32_t reserved2;         /* 0x3C */
    volatile uint32_t apb1enr;  /* 0x40 */
    volatile uint32_t apb2enr;  /* 0x44 */
} soc_stm32f4_rcc_regs_t;

_Static_assert(offsetof(soc_stm32f4_rcc_regs_t, ahb1enr) == 0x30u, "RCC AHB1ENR is at 0x30");
_Static_assert(offsetof(soc_stm32f4_rcc_regs_t, apb1enr) == 0x40u, "RCC APB1ENR is at 0x40");

/* SPI Register Map */
typedef struct soc_stm32f4_spi_regs {
    volatile uint32_t cr1;     /* 0x00 */
    volatile uint32_t cr2;     /* 0x04 */
    volatile uint32_t sr;      /* 0x08 */
    volatile uint32_t dr;      /* 0x0C */
    volatile uint32_t crcpr;   /* 0x10 */
    volatile uint32_t rxcrcr;  /* 0x14 */
    volatile uint32_t txcrcr;  /* 0x18 */
    volatile uint32_t i2scfgr; /* 0x1C */
    volatile uint32_t i2spr;   /* 0x20 */
} soc_stm32f4_spi_regs_t;

/* I2C Register Map */
typedef struct soc_stm32f4_i2c_regs {
    volatile uint32_t cr1;   /* 0x00 */
    volatile uint32_t cr2;   /* 0x04 */
    volatile uint32_t oar1;  /* 0x08 */
    volatile uint32_t oar2;  /* 0x0C */
    volatile uint32_t dr;    /* 0x10 */
    volatile uint32_t sr1;   /* 0x14 */
    volatile uint32_t sr2;   /* 0x18 */
    volatile uint32_t ccr;   /* 0x1C */
    volatile uint32_t trise; /* 0x20 */
} soc_stm32f4_i2c_regs_t;

/* IWDG Register Map */
typedef struct soc_stm32f4_iwdg_regs {
    volatile uint32_t kr;  /* 0x00 */
    volatile uint32_t pr;  /* 0x04 */
    volatile uint32_t rlr; /* 0x08 */
    volatile uint32_t sr;  /* 0x0C */
} soc_stm32f4_iwdg_regs_t;

#define SOC_STM32F4_IWDG_KEY_RELOAD 0xAAAAu
#define SOC_STM32F4_IWDG_KEY_ENABLE 0xCCCCu
#define SOC_STM32F4_IWDG_KEY_ACCESS 0x5555u

/* GPIO Pin Modes */
typedef enum soc_stm32f4_gpio_mode {
    SOC_STM32F4_GPIO_MODE_INPUT = 0u,
    SOC_STM32F4_GPIO_MODE_OUTPUT = 1u,
    SOC_STM32F4_GPIO_MODE_AF = 2u,
    SOC_STM32F4_GPIO_MODE_ANALOG = 3u,
} soc_stm32f4_gpio_mode_t;

/* GPIO Functions */
void soc_stm32f4_gpio_set_mode(soc_stm32f4_gpio_regs_t *gpio, uint8_t pin,
                               soc_stm32f4_gpio_mode_t mode);
void soc_stm32f4_gpio_write(soc_stm32f4_gpio_regs_t *gpio, uint8_t pin, bool value);
bool soc_stm32f4_gpio_read(const soc_stm32f4_gpio_regs_t *gpio, uint8_t pin);
void soc_stm32f4_gpio_toggle(soc_stm32f4_gpio_regs_t *gpio, uint8_t pin);

/* IWDG Functions */
void soc_stm32f4_iwdg_start(soc_stm32f4_iwdg_regs_t *iwdg, uint16_t reload_val);
void soc_stm32f4_iwdg_feed(soc_stm32f4_iwdg_regs_t *iwdg);

/* SPI Functions */
bool soc_stm32f4_spi_init(soc_stm32f4_spi_regs_t *spi, uint16_t cr1_flags);
bool soc_stm32f4_spi_transfer(soc_stm32f4_spi_regs_t *spi, const uint8_t *tx_buf, uint8_t *rx_buf,
                              size_t len);

/* I2C Functions */
bool soc_stm32f4_i2c_init(soc_stm32f4_i2c_regs_t *i2c, uint16_t ccr_val);
bool soc_stm32f4_i2c_write(soc_stm32f4_i2c_regs_t *i2c, uint8_t addr, const uint8_t *data,
                           size_t len);
bool soc_stm32f4_i2c_read(soc_stm32f4_i2c_regs_t *i2c, uint8_t addr, uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SOC_STM32F4_H */
