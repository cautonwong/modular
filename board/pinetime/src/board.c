#include "pinetime/board.h"

#include "edge/events.h"
#include <stddef.h>

static uint32_t g_low_power_entries;
static uint32_t g_watchdog_feeds;
static uint32_t g_reset_count;
static edge_event_sink_t *g_event_sink;
static board_pinetime_hw_t *g_hw_inst;

void board_pinetime_init(edge_event_sink_t *sink) {
    g_event_sink = sink;
    g_low_power_entries = 0u;
    g_watchdog_feeds = 0u;
    g_reset_count = 0u;
    g_hw_inst = NULL;
}

bool board_pinetime_hw_init(board_pinetime_hw_t *hw) {
    if (hw == NULL) {
        return false;
    }
    g_hw_inst = hw;
    *hw = (board_pinetime_hw_t){0};

    /* 1. Configure SPIM0 for ST7789 LCD (SCK=2, MOSI=3, 8MHz) */
    soc_nrf52_spim_config_t lcd_spim_cfg = {
        .pin_sck = PINETIME_PIN_LCD_SCK,
        .pin_mosi = PINETIME_PIN_LCD_MOSI,
        .pin_miso = 0xFFu,
        .frequency_code = 0x80000000u, /* 8MHz */
        .spi_mode = 3u,
    };
    (void)soc_nrf52_spim_init(&hw->spim_lcd, &lcd_spim_cfg);

    /* 2. Configure SPIM1 for SPI Flash XT25F32B (SCK=2, MOSI=3, MISO=4, 8MHz) */
    soc_nrf52_spim_config_t flash_spim_cfg = {
        .pin_sck = PINETIME_PIN_LCD_SCK,
        .pin_mosi = PINETIME_PIN_LCD_MOSI,
        .pin_miso = 4u,
        .frequency_code = 0x80000000u,
        .spi_mode = 0u,
    };
    (void)soc_nrf52_spim_init(&hw->spim_flash, &flash_spim_cfg);

    /* 3. Configure TWIM1 for Sensors (SCL=7, SDA=6, 400kHz) */
    soc_nrf52_twim_config_t twim_cfg = {
        .pin_scl = PINETIME_PIN_TOUCH_SCL,
        .pin_sda = PINETIME_PIN_TOUCH_SDA,
        .frequency_code = 0x06680000u, /* 400kHz */
    };
    (void)soc_nrf52_twim_init(&hw->twim_sensors, &twim_cfg);

    /* 4. Configure GPIOTE interrupts for Button (P0.13), Touch (P0.28), IMU (P0.08) */
    (void)soc_nrf52_gpiote_config_input_event(&hw->gpiote, 0u, PINETIME_PIN_BUTTON,
                                              SOC_NRF52_GPIOTE_POLARITY_TOGGLE);
    (void)soc_nrf52_gpiote_config_input_event(&hw->gpiote, 1u, PINETIME_PIN_TOUCH_INT,
                                              SOC_NRF52_GPIOTE_POLARITY_HITOLO);
    (void)soc_nrf52_gpiote_config_input_event(&hw->gpiote, 2u, PINETIME_PIN_IMU_INT,
                                              SOC_NRF52_GPIOTE_POLARITY_LOTOHI);

    /* 5. Configure SAADC for Battery Measurement on AIN7 / P0.31 */
    soc_nrf52_saadc_config_t saadc_cfg = {
        .channel = 0u,
        .ain_pin = 7u,               /* AIN7 = P0.31 */
        .resolution = 2u,            /* 12-bit */
        .gain_and_ref = 0x00020000u, /* 1/6 gain, internal ref */
    };
    (void)soc_nrf52_saadc_init(&hw->saadc_battery, &saadc_cfg);

    /* 6. Configure RTC1 Clock (32768Hz / (0+1) = 32768Hz counter) */
    (void)soc_nrf52_rtc_init(&hw->rtc_clock, 0u);
    soc_nrf52_rtc_start(&hw->rtc_clock);

    /* 7. Configure Watchdog Timer (7-second timeout) */
    (void)soc_nrf52_wdt_init(&hw->wdt_watchdog, 7u);

    return true;
}

void board_pinetime_enter_low_power(void) {
    ++g_low_power_entries;
}

void board_pinetime_feed_watchdog(void) {
    ++g_watchdog_feeds;
    if (g_hw_inst != NULL) {
        soc_nrf52_wdt_feed(&g_hw_inst->wdt_watchdog);
    }
}

void board_pinetime_system_reset(void) {
    ++g_reset_count;
}

uint32_t board_pinetime_low_power_entries(void) {
    return g_low_power_entries;
}

uint32_t board_pinetime_watchdog_feeds(void) {
    return g_watchdog_feeds;
}

uint32_t board_pinetime_reset_count(void) {
    return g_reset_count;
}

void board_pinetime_irq_button(bool pressed) {
    if (g_event_sink == NULL) {
        return;
    }
    const edge_event_t evt = {
        .id = EDGE_EVT_WATCH_BUTTON,
        .source = 0u,
        .arg0 = pressed ? 1u : 0u,
        .arg1 = 0u,
        .timestamp = 0u,
    };
    (void)edge_event_sink_push_isr(g_event_sink, &evt);
}

void board_pinetime_irq_touch(void) {
    if (g_event_sink == NULL) {
        return;
    }
    const edge_event_t evt = {
        .id = EDGE_EVT_WATCH_TOUCH,
        .source = 0u,
        .arg0 = 0u,
        .arg1 = 0u,
        .timestamp = 0u,
    };
    (void)edge_event_sink_push_isr(g_event_sink, &evt);
}

void board_pinetime_irq_imu(void) {
    if (g_event_sink == NULL) {
        return;
    }
    const edge_event_t evt = {
        .id = EDGE_EVT_WATCH_WRIST_WAKE,
        .source = 0u,
        .arg0 = 0u,
        .arg1 = 0u,
        .timestamp = 0u,
    };
    (void)edge_event_sink_push_isr(g_event_sink, &evt);
}
