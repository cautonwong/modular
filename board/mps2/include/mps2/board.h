#ifndef BOARD_MPS2_H
#define BOARD_MPS2_H

#include "edge/event.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Board: ARM MPS2 AN386 (Cortex-M4). Owns the IRQ vectors and safe actions. */
void board_mps2_init(edge_event_sink_t *sink);
void board_mps2_irq_uart0_rx(uint32_t byte_count);
void board_mps2_timer_init(void);
void board_mps2_irq_timer0(void);
__attribute__((noreturn)) void board_mps2_exit(int code);

/* Hardware actions (D9/D71). */
void board_mps2_enter_low_power(void);
void board_mps2_feed_watchdog(void);
void board_mps2_system_reset(void);

#ifdef __cplusplus
}
#endif

#endif
