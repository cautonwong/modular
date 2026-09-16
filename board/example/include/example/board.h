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

#ifdef __cplusplus
}
#endif

#endif
