#include "keyboard_mock/board.h"
#include "edge/events.h"

static edge_event_sink_t *g_sink = NULL;
static uint32_t g_low_power_count = 0;
static uint32_t g_watchdog_count = 0;
static uint32_t g_reset_count = 0;

void board_keyboard_mock_init(edge_event_sink_t *sink) {
    g_sink = sink;
    g_low_power_count = 0;
    g_watchdog_count = 0;
    g_reset_count = 0;
}

void board_keyboard_mock_inject_key(uint8_t row, uint8_t col, bool pressed) {
    if (g_sink == NULL) {
        return;
    }
    edge_event_t event = {
        .id = EDGE_EVT_ZMK_POSITION_STATE_CHANGED,
        .source = 0,
        .arg0 = (uint32_t)row | ((uint32_t)col << 8) | (pressed ? (1u << 16) : 0u),
        .arg1 = 0,
        .timestamp = 0,
    };
    edge_event_sink_push_isr(g_sink, &event);
}

void board_keyboard_mock_inject_split_packet(const uint8_t *data, uint32_t len) {
    (void)data;
    if (g_sink == NULL) {
        return;
    }
    edge_event_t event = {
        .id = EDGE_EVT_ZMK_SPLIT_EVENT,
        .source = 0,
        .arg0 = len,
        .arg1 = 0,
        .timestamp = 0,
    };
    edge_event_sink_push_isr(g_sink, &event);
}

void board_keyboard_mock_enter_low_power(void) {
    g_low_power_count++;
}

void board_keyboard_mock_feed_watchdog(void) {
    g_watchdog_count++;
}

void board_keyboard_mock_system_reset(void) {
    g_reset_count++;
}

uint32_t board_keyboard_mock_low_power_entries(void) {
    return g_low_power_count;
}

uint32_t board_keyboard_mock_watchdog_feeds(void) {
    return g_watchdog_count;
}

uint32_t board_keyboard_mock_reset_count(void) {
    return g_reset_count;
}
