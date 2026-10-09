#ifndef SOC_NRF52_H
#define SOC_NRF52_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Memory Map Base Addresses for Nordic nRF52832 */
#define SOC_NRF52_FICR_BASE ((uintptr_t)0x10000000u)
#define SOC_NRF52_UICR_BASE ((uintptr_t)0x10001000u)
#define SOC_NRF52_RADIO_BASE ((uintptr_t)0x40001000u)
#define SOC_NRF52_TWIM0_BASE ((uintptr_t)0x40003000u)
#define SOC_NRF52_SPIM0_BASE ((uintptr_t)0x40003000u)
#define SOC_NRF52_SPIM1_BASE ((uintptr_t)0x40004000u)
#define SOC_NRF52_TWIM1_BASE ((uintptr_t)0x40004000u)
#define SOC_NRF52_GPIOTE_BASE ((uintptr_t)0x40006000u)
#define SOC_NRF52_SAADC_BASE ((uintptr_t)0x40007000u)
#define SOC_NRF52_TIMER0_BASE ((uintptr_t)0x40008000u)
#define SOC_NRF52_RTC0_BASE ((uintptr_t)0x4000B000u)
#define SOC_NRF52_WDT_BASE ((uintptr_t)0x40010000u)
#define SOC_NRF52_RTC1_BASE ((uintptr_t)0x40011000u)
#define SOC_NRF52_GPIO_P0_BASE ((uintptr_t)0x50000000u)

/* System clock frequencies */
#define SOC_NRF52_SYSCLK_HZ 64000000u
#define SOC_NRF52_LFCLK_HZ 32768u

/* Watchdog reload magic token */
#define SOC_NRF52_WDT_RR_VALUE 0x6E524635u

/* GPIO Peripheral Register Map */
typedef struct soc_nrf52_gpio_regs {
    uint8_t reserved0[0x504];
    volatile uint32_t out;         /* 0x504 */
    volatile uint32_t outset;      /* 0x508 */
    volatile uint32_t outclr;      /* 0x50C */
    volatile uint32_t in;          /* 0x510 */
    volatile uint32_t dir;         /* 0x514 */
    volatile uint32_t dirset;      /* 0x518 */
    volatile uint32_t dirclr;      /* 0x51C */
    uint8_t reserved1[0x1E0];      /* 0x520..0x6FF */
    volatile uint32_t pin_cnf[32]; /* 0x700..0x77C */
} soc_nrf52_gpio_regs_t;

_Static_assert(offsetof(soc_nrf52_gpio_regs_t, out) == 0x504u, "GPIO OUT is at 0x504");
_Static_assert(offsetof(soc_nrf52_gpio_regs_t, in) == 0x510u, "GPIO IN is at 0x510");
_Static_assert(offsetof(soc_nrf52_gpio_regs_t, dir) == 0x514u, "GPIO DIR is at 0x514");
_Static_assert(offsetof(soc_nrf52_gpio_regs_t, pin_cnf) == 0x700u, "GPIO PIN_CNF is at 0x700");

/* GPIOTE Register Map */
typedef struct soc_nrf52_gpiote_regs {
    volatile uint32_t tasks_out[8]; /* 0x000..0x01C */
    uint8_t reserved0[0x10];        /* 0x020..0x02F */
    volatile uint32_t tasks_set[8]; /* 0x030..0x04C */
    uint8_t reserved1[0x10];        /* 0x050..0x05F */
    volatile uint32_t tasks_clr[8]; /* 0x060..0x07C */
    uint8_t reserved2[0x80];        /* 0x080..0x0FF */
    volatile uint32_t events_in[8]; /* 0x100..0x11C */
    uint8_t reserved3[0x5C];        /* 0x120..0x17B */
    volatile uint32_t events_port;  /* 0x17C */
    uint8_t reserved4[0x184];       /* 0x180..0x303 */
    volatile uint32_t intenset;     /* 0x304 */
    volatile uint32_t intenclr;     /* 0x308 */
    uint8_t reserved5[0x204];       /* 0x30C..0x50F */
    volatile uint32_t config[8];    /* 0x510..0x52C */
} soc_nrf52_gpiote_regs_t;

_Static_assert(offsetof(soc_nrf52_gpiote_regs_t, events_in) == 0x100u,
               "GPIOTE EVENTS_IN is at 0x100");
_Static_assert(offsetof(soc_nrf52_gpiote_regs_t, intenset) == 0x304u,
               "GPIOTE INTENSET is at 0x304");
_Static_assert(offsetof(soc_nrf52_gpiote_regs_t, config) == 0x510u, "GPIOTE CONFIG is at 0x510");

/* SPIM (SPI Master with EasyDMA) Register Map */
typedef struct soc_nrf52_spim_regs {
    uint8_t reserved0[0x010];
    volatile uint32_t tasks_start; /* 0x010 */
    volatile uint32_t tasks_stop;  /* 0x014 */
    uint8_t reserved1[0x4];
    volatile uint32_t tasks_suspend; /* 0x01C */
    volatile uint32_t tasks_resume;  /* 0x020 */
    uint8_t reserved2[0xE0];
    volatile uint32_t events_stopped; /* 0x104 */
    uint8_t reserved3[0x8];
    volatile uint32_t events_endrx; /* 0x110 */
    uint8_t reserved4[0x4];
    volatile uint32_t events_end; /* 0x118 */
    uint8_t reserved5[0x4];
    volatile uint32_t events_endtx; /* 0x120 */
    uint8_t reserved6[0x28];
    volatile uint32_t events_started; /* 0x14C */
    uint8_t reserved7[0x1B4];
    volatile uint32_t intenset; /* 0x304 */
    volatile uint32_t intenclr; /* 0x308 */
    uint8_t reserved8[0x1F4];
    volatile uint32_t enable; /* 0x500 */
    uint8_t reserved9[0x4];
    volatile uint32_t psel_sck;  /* 0x508 */
    volatile uint32_t psel_mosi; /* 0x50C */
    volatile uint32_t psel_miso; /* 0x510 */
    uint8_t reserved10[0x10];
    volatile uint32_t frequency; /* 0x524 */
    uint8_t reserved11[0xC];
    volatile uint32_t rxd_ptr;    /* 0x534 */
    volatile uint32_t rxd_maxcnt; /* 0x538 */
    volatile uint32_t rxd_amount; /* 0x53C */
    volatile uint32_t rxd_list;   /* 0x540 */
    volatile uint32_t txd_ptr;    /* 0x544 */
    volatile uint32_t txd_maxcnt; /* 0x548 */
    volatile uint32_t txd_amount; /* 0x54C */
    volatile uint32_t txd_list;   /* 0x550 */
    volatile uint32_t config;     /* 0x554 */
} soc_nrf52_spim_regs_t;

_Static_assert(offsetof(soc_nrf52_spim_regs_t, tasks_start) == 0x010u, "SPIM TASKS_START at 0x010");
_Static_assert(offsetof(soc_nrf52_spim_regs_t, events_end) == 0x118u, "SPIM EVENTS_END at 0x118");
_Static_assert(offsetof(soc_nrf52_spim_regs_t, enable) == 0x500u, "SPIM ENABLE at 0x500");
_Static_assert(offsetof(soc_nrf52_spim_regs_t, psel_sck) == 0x508u, "SPIM PSEL.SCK at 0x508");
_Static_assert(offsetof(soc_nrf52_spim_regs_t, frequency) == 0x524u, "SPIM FREQUENCY at 0x524");
_Static_assert(offsetof(soc_nrf52_spim_regs_t, txd_ptr) == 0x544u, "SPIM TXD.PTR at 0x544");

/* TWIM (I2C Master with EasyDMA) Register Map */
typedef struct soc_nrf52_twim_regs {
    volatile uint32_t tasks_startrx; /* 0x000 */
    uint8_t reserved0[0x4];
    volatile uint32_t tasks_starttx; /* 0x008 */
    uint8_t reserved1[0x8];
    volatile uint32_t tasks_stop; /* 0x014 */
    uint8_t reserved2[0x4];
    volatile uint32_t tasks_suspend; /* 0x01C */
    volatile uint32_t tasks_resume;  /* 0x020 */
    uint8_t reserved3[0xE0];
    volatile uint32_t events_stopped; /* 0x104 */
    uint8_t reserved4[0x1C];
    volatile uint32_t events_error; /* 0x124 */
    uint8_t reserved5[0x20];
    volatile uint32_t events_suspended; /* 0x148 */
    volatile uint32_t events_rxstarted; /* 0x14C */
    volatile uint32_t events_txstarted; /* 0x150 */
    uint8_t reserved6[0x8];
    volatile uint32_t events_lastrx; /* 0x15C */
    volatile uint32_t events_lasttx; /* 0x160 */
    uint8_t reserved7[0x1A0];
    volatile uint32_t intenset; /* 0x304 */
    volatile uint32_t intenclr; /* 0x308 */
    uint8_t reserved8[0x1F4];
    volatile uint32_t enable; /* 0x500 */
    uint8_t reserved9[0x4];
    volatile uint32_t psel_scl; /* 0x508 */
    volatile uint32_t psel_sda; /* 0x50C */
    uint8_t reserved10[0x14];
    volatile uint32_t frequency; /* 0x524 */
    uint8_t reserved11[0xC];
    volatile uint32_t rxd_ptr;    /* 0x534 */
    volatile uint32_t rxd_maxcnt; /* 0x538 */
    volatile uint32_t rxd_amount; /* 0x53C */
    volatile uint32_t rxd_list;   /* 0x540 */
    volatile uint32_t txd_ptr;    /* 0x544 */
    volatile uint32_t txd_maxcnt; /* 0x548 */
    volatile uint32_t txd_amount; /* 0x54C */
    volatile uint32_t txd_list;   /* 0x550 */
    uint8_t reserved12[0x34];
    volatile uint32_t address; /* 0x588 */
} soc_nrf52_twim_regs_t;

_Static_assert(offsetof(soc_nrf52_twim_regs_t, tasks_startrx) == 0x000u,
               "TWIM TASKS_STARTRX at 0x000");
_Static_assert(offsetof(soc_nrf52_twim_regs_t, tasks_starttx) == 0x008u,
               "TWIM TASKS_STARTTX at 0x008");
_Static_assert(offsetof(soc_nrf52_twim_regs_t, enable) == 0x500u, "TWIM ENABLE at 0x500");
_Static_assert(offsetof(soc_nrf52_twim_regs_t, address) == 0x588u, "TWIM ADDRESS at 0x588");

/* SAADC Register Map */
typedef struct soc_nrf52_saadc_regs {
    volatile uint32_t tasks_start;           /* 0x000 */
    volatile uint32_t tasks_sample;          /* 0x004 */
    volatile uint32_t tasks_stop;            /* 0x008 */
    volatile uint32_t tasks_calibrateoffset; /* 0x00C */
    uint8_t reserved0[0xF0];
    volatile uint32_t events_started;       /* 0x100 */
    volatile uint32_t events_end;           /* 0x104 */
    volatile uint32_t events_done;          /* 0x108 */
    volatile uint32_t events_resultdone;    /* 0x10C */
    volatile uint32_t events_calibratedone; /* 0x110 */
    volatile uint32_t events_stopped;       /* 0x114 */
    uint8_t reserved1[0x1EC];
    volatile uint32_t intenset; /* 0x304 */
    volatile uint32_t intenclr; /* 0x308 */
    uint8_t reserved2[0x1F4];
    volatile uint32_t enable; /* 0x500 */
    uint8_t reserved3[0xC];
    struct {
        volatile uint32_t pselp;  /* 0x510 + 0x10*i */
        volatile uint32_t pseln;  /* 0x514 + 0x10*i */
        volatile uint32_t config; /* 0x518 + 0x10*i */
        volatile uint32_t limit;  /* 0x51C + 0x10*i */
    } ch[8];
    uint8_t reserved4[0x60];      /* 0x590..0x5EF */
    volatile uint32_t resolution; /* 0x5F0 */
    uint8_t reserved5[0x38];
    volatile uint32_t result_ptr;    /* 0x62C */
    volatile uint32_t result_maxcnt; /* 0x630 */
    volatile uint32_t result_amount; /* 0x634 */
} soc_nrf52_saadc_regs_t;

_Static_assert(offsetof(soc_nrf52_saadc_regs_t, tasks_start) == 0x000u,
               "SAADC TASKS_START at 0x000");
_Static_assert(offsetof(soc_nrf52_saadc_regs_t, ch) == 0x510u, "SAADC CH[0] is at 0x510");
_Static_assert(offsetof(soc_nrf52_saadc_regs_t, resolution) == 0x5F0u, "SAADC RESOLUTION at 0x5F0");
_Static_assert(offsetof(soc_nrf52_saadc_regs_t, result_ptr) == 0x62Cu, "SAADC RESULT.PTR at 0x62C");

/* RTC Register Map */
typedef struct soc_nrf52_rtc_regs {
    volatile uint32_t tasks_start;      /* 0x000 */
    volatile uint32_t tasks_stop;       /* 0x004 */
    volatile uint32_t tasks_clear;      /* 0x008 */
    volatile uint32_t tasks_trigovrflw; /* 0x00C */
    uint8_t reserved0[0xF0];
    volatile uint32_t events_tick;   /* 0x100 */
    volatile uint32_t events_ovrflw; /* 0x104 */
    uint8_t reserved1[0x38];
    volatile uint32_t events_compare[4]; /* 0x140..0x14C */
    uint8_t reserved2[0x1B4];
    volatile uint32_t intenset; /* 0x304 */
    volatile uint32_t intenclr; /* 0x308 */
    uint8_t reserved3[0x34];
    volatile uint32_t evten;    /* 0x340 */
    volatile uint32_t evtenset; /* 0x344 */
    volatile uint32_t evtenclr; /* 0x348 */
    uint8_t reserved4[0x1B8];
    volatile uint32_t counter;   /* 0x504 */
    volatile uint32_t prescaler; /* 0x508 */
    uint8_t reserved5[0x34];
    volatile uint32_t cc[4]; /* 0x540..0x54C */
} soc_nrf52_rtc_regs_t;

_Static_assert(offsetof(soc_nrf52_rtc_regs_t, counter) == 0x504u, "RTC COUNTER at 0x504");
_Static_assert(offsetof(soc_nrf52_rtc_regs_t, prescaler) == 0x508u, "RTC PRESCALER at 0x508");
_Static_assert(offsetof(soc_nrf52_rtc_regs_t, cc) == 0x540u, "RTC CC is at 0x540");

/* WDT Register Map */
typedef struct soc_nrf52_wdt_regs {
    volatile uint32_t tasks_start; /* 0x000 */
    uint8_t reserved0[0xFC];
    volatile uint32_t events_timeout; /* 0x100 */
    uint8_t reserved1[0x200];
    volatile uint32_t intenset; /* 0x304 */
    volatile uint32_t intenclr; /* 0x308 */
    uint8_t reserved2[0x1F8];
    volatile uint32_t config; /* 0x504 */
    volatile uint32_t crv;    /* 0x508 */
    volatile uint32_t rren;   /* 0x50C */
    volatile uint32_t status; /* 0x510 */
    uint8_t reserved3[0xEC];
    volatile uint32_t rr[8]; /* 0x600..0x61C */
} soc_nrf52_wdt_regs_t;

_Static_assert(offsetof(soc_nrf52_wdt_regs_t, config) == 0x504u, "WDT CONFIG at 0x504");
_Static_assert(offsetof(soc_nrf52_wdt_regs_t, crv) == 0x508u, "WDT CRV at 0x508");
_Static_assert(offsetof(soc_nrf52_wdt_regs_t, rren) == 0x50Cu, "WDT RREN at 0x50C");
_Static_assert(offsetof(soc_nrf52_wdt_regs_t, rr) == 0x600u, "WDT RR is at 0x600");

/* NVMC (Non-Volatile Memory Controller) Register Map */
typedef struct soc_nrf52_nvmc_regs {
    uint8_t reserved0[0x400];
    volatile uint32_t ready; /* 0x400 */
    uint8_t reserved1[0x100];
    volatile uint32_t config;    /* 0x504: 0=Ren, 1=Wen, 2=Een */
    volatile uint32_t erasepage; /* 0x508 */
    volatile uint32_t eraseall;  /* 0x50C */
    volatile uint32_t erasepcr0; /* 0x510 */
    volatile uint32_t eraseuicr; /* 0x514 */
} soc_nrf52_nvmc_regs_t;

_Static_assert(offsetof(soc_nrf52_nvmc_regs_t, ready) == 0x400u, "NVMC READY at 0x400");
_Static_assert(offsetof(soc_nrf52_nvmc_regs_t, config) == 0x504u, "NVMC CONFIG at 0x504");
_Static_assert(offsetof(soc_nrf52_nvmc_regs_t, erasepage) == 0x508u, "NVMC ERASEPAGE at 0x508");
_Static_assert(offsetof(soc_nrf52_nvmc_regs_t, eraseuicr) == 0x514u, "NVMC ERASEUICR at 0x514");

/* Peripheral Configuration & Helpers */

/* NVMC Helpers */
bool soc_nrf52_nvmc_erase_page(soc_nrf52_nvmc_regs_t *nvmc, uint32_t page_addr);
bool soc_nrf52_nvmc_write_word(soc_nrf52_nvmc_regs_t *nvmc, uintptr_t dest_addr, uint32_t val);
bool soc_nrf52_nvmc_write_words(soc_nrf52_nvmc_regs_t *nvmc, uintptr_t dest_addr,
                                const uint32_t *src, size_t num_words);

/* SPIM Helpers */
typedef struct soc_nrf52_spim_config {
    uint8_t pin_sck;
    uint8_t pin_mosi;
    uint8_t pin_miso;
    uint32_t frequency_code; /* e.g. 0x80000000 for 8MHz */
    uint8_t spi_mode;        /* 0..3 */
} soc_nrf52_spim_config_t;

bool soc_nrf52_spim_init(soc_nrf52_spim_regs_t *spim, const soc_nrf52_spim_config_t *config);
bool soc_nrf52_spim_transfer(soc_nrf52_spim_regs_t *spim, const uint8_t *tx_buf, size_t tx_len,
                             uint8_t *rx_buf, size_t rx_len);

/* TWIM Helpers */
typedef struct soc_nrf52_twim_config {
    uint8_t pin_scl;
    uint8_t pin_sda;
    uint32_t frequency_code; /* e.g. 0x06680000 for 400kHz */
} soc_nrf52_twim_config_t;

bool soc_nrf52_twim_init(soc_nrf52_twim_regs_t *twim, const soc_nrf52_twim_config_t *config);
bool soc_nrf52_twim_write(soc_nrf52_twim_regs_t *twim, uint8_t addr, const uint8_t *tx_buf,
                          size_t tx_len, bool no_stop);
bool soc_nrf52_twim_read(soc_nrf52_twim_regs_t *twim, uint8_t addr, uint8_t *rx_buf, size_t rx_len);

/* SAADC Helpers */
typedef struct soc_nrf52_saadc_config {
    uint8_t channel;
    uint8_t ain_pin;       /* 0..7 for AIN0..AIN7 */
    uint8_t resolution;    /* 0=8b, 1=10b, 2=12b, 3=14b */
    uint32_t gain_and_ref; /* e.g. Gain 1/6, internal 0.6V ref */
} soc_nrf52_saadc_config_t;

bool soc_nrf52_saadc_init(soc_nrf52_saadc_regs_t *saadc, const soc_nrf52_saadc_config_t *config);
int16_t soc_nrf52_saadc_sample_blocking(soc_nrf52_saadc_regs_t *saadc, int16_t *out_val);

/* RTC Helpers */
bool soc_nrf52_rtc_init(soc_nrf52_rtc_regs_t *rtc, uint32_t prescaler);
uint32_t soc_nrf52_rtc_get_counter(const soc_nrf52_rtc_regs_t *rtc);
void soc_nrf52_rtc_start(soc_nrf52_rtc_regs_t *rtc);
void soc_nrf52_rtc_stop(soc_nrf52_rtc_regs_t *rtc);

/* WDT Helpers */
bool soc_nrf52_wdt_init(soc_nrf52_wdt_regs_t *wdt, uint32_t timeout_seconds);
void soc_nrf52_wdt_feed(soc_nrf52_wdt_regs_t *wdt);

/* GPIOTE Helpers */
typedef enum soc_nrf52_gpiote_polarity {
    SOC_NRF52_GPIOTE_POLARITY_NONE = 0,
    SOC_NRF52_GPIOTE_POLARITY_LOTOHI = 1,
    SOC_NRF52_GPIOTE_POLARITY_HITOLO = 2,
    SOC_NRF52_GPIOTE_POLARITY_TOGGLE = 3,
} soc_nrf52_gpiote_polarity_t;

bool soc_nrf52_gpiote_config_input_event(soc_nrf52_gpiote_regs_t *gpiote, uint8_t channel,
                                         uint8_t pin, soc_nrf52_gpiote_polarity_t polarity);

#ifdef __cplusplus
}
#endif

#endif /* SOC_NRF52_H */
