#include "colmi_p8/board.h"

#include "edge/events.h"
#include <stddef.h>

static uint32_t g_low_power_entries;
static uint32_t g_watchdog_feeds;
static uint32_t g_reset_count;
static edge_event_sink_t *g_event_sink;
static board_colmi_p8_hw_t *g_hw_inst;

void board_colmi_p8_init(edge_event_sink_t *sink) {
    g_event_sink = sink;
    g_low_power_entries = 0u;
    g_watchdog_feeds = 0u;
    g_reset_count = 0u;
    g_hw_inst = NULL;
}

bool board_colmi_p8_hw_init(board_colmi_p8_hw_t *hw) {
    if (hw == NULL) {
        return false;
    }
    g_hw_inst = hw;
    *hw = (board_colmi_p8_hw_t){0};

    soc_nrf52_spim_config_t lcd_spim_cfg = {
        .pin_sck = COLMI_P8_PIN_LCD_SCK,
        .pin_mosi = COLMI_P8_PIN_LCD_MOSI,
        .pin_miso = 0xFFu,
        .frequency_code = 0x80000000u, /* 8MHz */
        .spi_mode = 3u,
    };
    (void)soc_nrf52_spim_init(&hw->spim_lcd, &lcd_spim_cfg);

    soc_nrf52_twim_config_t twim_cfg = {
        .pin_scl = COLMI_P8_PIN_TOUCH_SCL,
        .pin_sda = COLMI_P8_PIN_TOUCH_SDA,
        .frequency_code = 0x06680000u, /* 400kHz */
    };
    (void)soc_nrf52_twim_init(&hw->twim_touch, &twim_cfg);

    (void)soc_nrf52_gpiote_config_input_event(&hw->gpiote, 0u, COLMI_P8_PIN_BUTTON,
                                              SOC_NRF52_GPIOTE_POLARITY_TOGGLE);
    (void)soc_nrf52_gpiote_config_input_event(&hw->gpiote, 1u, COLMI_P8_PIN_TOUCH_INT,
                                              SOC_NRF52_GPIOTE_POLARITY_HITOLO);

    soc_nrf52_saadc_config_t saadc_cfg = {
        .channel = 0u,
        .ain_pin = 7u,               /* AIN7 = P0.31 */
        .resolution = 2u,            /* 12-bit */
        .gain_and_ref = 0x00020000u, /* 1/6 gain, internal ref */
    };
    (void)soc_nrf52_saadc_init(&hw->saadc_battery, &saadc_cfg);

    (void)soc_nrf52_rtc_init(&hw->rtc_clock, 0u);
    soc_nrf52_rtc_start(&hw->rtc_clock);

    (void)soc_nrf52_wdt_init(&hw->wdt_watchdog, 7u);

    return true;
}

void board_colmi_p8_enter_low_power(void) {
    ++g_low_power_entries;
}

void board_colmi_p8_feed_watchdog(void) {
    ++g_watchdog_feeds;
    if (g_hw_inst != NULL) {
        soc_nrf52_wdt_feed(&g_hw_inst->wdt_watchdog);
    }
}

void board_colmi_p8_system_reset(void) {
    ++g_reset_count;
}

uint32_t board_colmi_p8_low_power_entries(void) {
    return g_low_power_entries;
}

uint32_t board_colmi_p8_watchdog_feeds(void) {
    return g_watchdog_feeds;
}

uint32_t board_colmi_p8_reset_count(void) {
    return g_reset_count;
}

void board_colmi_p8_irq_button(bool pressed) {
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

void board_colmi_p8_irq_touch(void) {
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
