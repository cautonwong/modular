#ifndef BOARD_EXAMPLE_H
#define BOARD_EXAMPLE_H

#include "edge/event.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void board_example_init(edge_event_sink_t *sink);
void board_example_irq_uart0_rx(uint32_t byte_count);
void board_example_irq_timer0(void);
void board_example_qemu_timer0_init(void);

/* Hardware actions (D9/D71): low power, watchdog, reset. */
void board_example_enter_low_power(void);
void board_example_feed_watchdog(void);
void board_example_system_reset(void);

/* Test observability. */
uint32_t board_example_low_power_entries(void);
uint32_t board_example_watchdog_feeds(void);
uint32_t board_example_reset_count(void);

#ifdef __cplusplus
}
#endif

#endif
