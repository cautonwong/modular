#ifndef BOARD_MPS2_H
#define BOARD_MPS2_H

#include "edge/errors.h"
#include "edge/event.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Board: ARM MPS2 AN386 (Cortex-M4). Owns the IRQ vectors and safe actions. */
void board_mps2_init(edge_event_sink_t *sink);
void board_mps2_irq_uart0_rx(uint32_t byte_count);
void board_mps2_timer_init(void);

/* NVIC priority for the TIMER0 IRQ, in library units (0 = highest). An RTOS
 * product must call this before board_mps2_timer_init() if its handler uses any
 * *FromISR API, otherwise the IRQ sits above the syscall ceiling. */
void board_mps2_timer_set_priority(uint8_t library_priority);
void board_mps2_irq_timer0(void);

/*
 * Shared-IRQ dispatch (D84).
 *
 * One IRQ line can carry several handlers, and they run in **registration
 * order**. The vector entry of a line is the *dispatcher*, not a single handler,
 * so a second consumer of the same line needs no new vector and no new board
 * function.
 *
 * The table is written at startup and has no detach, on purpose: the composition
 * root owns the wiring (D5), and a board does not rewire its interrupts at run
 * time. Capacity is fixed (no allocation, D21): a full table returns
 * EDGE_ENOSPC, and a null callback returns EDGE_EINVAL.
 */
#define BOARD_MPS2_IRQ_MAX_HANDLERS 4u

typedef void (*board_mps2_irq_fn)(void *ctx);

edge_status_t board_mps2_irq_attach(uint32_t irq, board_mps2_irq_fn cb, void *ctx);

/* The ISR-side entry point for `irq`. Safe with no handler registered, and
 * bounded by the table's capacity (D75). */
void board_mps2_irq_dispatch(uint32_t irq);

/* Observability: how many handlers are registered. */
uint32_t board_mps2_irq_handler_count(void);
__attribute__((noreturn)) void board_mps2_exit(int code);

/* Hardware actions (D9/D71). */
void board_mps2_enter_low_power(void);
void board_mps2_feed_watchdog(void);
void board_mps2_system_reset(void);

#ifdef __cplusplus
}
#endif

#endif
