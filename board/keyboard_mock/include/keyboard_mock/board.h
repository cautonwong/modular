#ifndef BOARD_KEYBOARD_MOCK_H
#define BOARD_KEYBOARD_MOCK_H

#include "edge/event.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void board_keyboard_mock_init(edge_event_sink_t *sink);
void board_keyboard_mock_inject_key(uint8_t row, uint8_t col, bool pressed);
void board_keyboard_mock_inject_split_packet(const uint8_t *data, uint32_t len);

/* Hardware controls */
void board_keyboard_mock_enter_low_power(void);
void board_keyboard_mock_feed_watchdog(void);
void board_keyboard_mock_system_reset(void);

/* Observability */
uint32_t board_keyboard_mock_low_power_entries(void);
uint32_t board_keyboard_mock_watchdog_feeds(void);
uint32_t board_keyboard_mock_reset_count(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_KEYBOARD_MOCK_H */
