#include "rpi_pico/board.h"

#include "edge/events.h"
#include <stddef.h>

static uint32_t g_low_power_entries;
static uint32_t g_watchdog_feeds;
static uint32_t g_reset_count;
static bool g_led_state;
static edge_event_sink_t *g_event_sink;
static board_rpi_pico_hw_t *g_hw_inst;

void board_rpi_pico_init(edge_event_sink_t *sink) {
    g_event_sink = sink;
    g_low_power_entries = 0u;
    g_watchdog_feeds = 0u;
    g_reset_count = 0u;
    g_led_state = false;
    g_hw_inst = NULL;
}

bool board_rpi_pico_hw_init(board_rpi_pico_hw_t *hw) {
    if (hw == NULL) {
        return false;
    }
    g_hw_inst = hw;
    *hw = (board_rpi_pico_hw_t){0};

    soc_rp2040_gpio_init(&hw->sio, RPI_PICO_PIN_LED, true);
    (void)soc_rp2040_spi_init(&hw->spi, 1000000u, 8u);
    (void)soc_rp2040_i2c_init(&hw->i2c, 400000u);
    soc_rp2040_wdt_start(&hw->wdt, 5000u);

    return true;
}

void board_rpi_pico_set_led(bool on) {
    g_led_state = on;
    if (g_hw_inst != NULL) {
        soc_rp2040_gpio_put(&g_hw_inst->sio, RPI_PICO_PIN_LED, on);
    }
}

void board_rpi_pico_enter_low_power(void) {
    ++g_low_power_entries;
}

void board_rpi_pico_feed_watchdog(void) {
    ++g_watchdog_feeds;
    if (g_hw_inst != NULL) {
        soc_rp2040_wdt_feed(&g_hw_inst->wdt);
    }
}

void board_rpi_pico_system_reset(void) {
    ++g_reset_count;
}

uint32_t board_rpi_pico_low_power_entries(void) {
    return g_low_power_entries;
}

uint32_t board_rpi_pico_watchdog_feeds(void) {
    return g_watchdog_feeds;
}

uint32_t board_rpi_pico_reset_count(void) {
    return g_reset_count;
}

void board_rpi_pico_irq_matrix(uint8_t row, uint8_t col, bool pressed) {
    if (g_event_sink == NULL) {
        return;
    }
    const edge_event_t evt = {
        .id = EDGE_EVT_ZMK_POSITION_STATE_CHANGED,
        .source = 0u,
        .arg0 = ((uint32_t)row << 16u) | (uint32_t)col,
        .arg1 = pressed ? 1u : 0u,
        .timestamp = 0u,
    };
    (void)edge_event_sink_push_isr(g_event_sink, &evt);
}
