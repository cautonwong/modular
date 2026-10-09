#include "nice_nano/board.h"

#include "edge/events.h"
#include <stddef.h>

static uint32_t g_low_power_entries;
static uint32_t g_watchdog_feeds;
static uint32_t g_reset_count;
static bool g_led_state;
static bool g_ext_power_state;
static edge_event_sink_t *g_event_sink;
static board_nice_nano_hw_t *g_hw_inst;

void board_nice_nano_init(edge_event_sink_t *sink) {
    g_event_sink = sink;
    g_low_power_entries = 0u;
    g_watchdog_feeds = 0u;
    g_reset_count = 0u;
    g_led_state = false;
    g_ext_power_state = false;
    g_hw_inst = NULL;
}

bool board_nice_nano_hw_init(board_nice_nano_hw_t *hw) {
    if (hw == NULL) {
        return false;
    }
    g_hw_inst = hw;
    *hw = (board_nice_nano_hw_t){0};

    soc_nrf52_saadc_config_t saadc_cfg = {
        .channel = 0u,
        .ain_pin = 2u,               /* AIN2 = P0.04 */
        .resolution = 2u,            /* 12-bit */
        .gain_and_ref = 0x00020000u, /* 1/6 gain, internal ref */
    };
    (void)soc_nrf52_saadc_init(&hw->saadc_battery, &saadc_cfg);

    (void)soc_nrf52_rtc_init(&hw->rtc_clock, 0u);
    soc_nrf52_rtc_start(&hw->rtc_clock);

    (void)soc_nrf52_wdt_init(&hw->wdt_watchdog, 5u);

    return true;
}

void board_nice_nano_set_led(bool on) {
    g_led_state = on;
}

void board_nice_nano_set_ext_power(bool enable) {
    g_ext_power_state = enable;
}

void board_nice_nano_enter_low_power(void) {
    ++g_low_power_entries;
}

void board_nice_nano_feed_watchdog(void) {
    ++g_watchdog_feeds;
    if (g_hw_inst != NULL) {
        soc_nrf52_wdt_feed(&g_hw_inst->wdt_watchdog);
    }
}

void board_nice_nano_system_reset(void) {
    ++g_reset_count;
}

uint32_t board_nice_nano_low_power_entries(void) {
    return g_low_power_entries;
}

uint32_t board_nice_nano_watchdog_feeds(void) {
    return g_watchdog_feeds;
}

uint32_t board_nice_nano_reset_count(void) {
    return g_reset_count;
}

void board_nice_nano_irq_matrix(uint8_t row, uint8_t col, bool pressed) {
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
