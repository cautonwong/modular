#ifndef BOARD_RISCV_VIRT_H
#define BOARD_RISCV_VIRT_H

#include "edge/event.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Board: QEMU RISC-V `virt` (RV32, machine mode).
 *
 * Owns the trap vector, the CLINT machine timer and the QEMU test finisher.
 * It only captures the timer and pushes an event; the superloop lives in sys.
 */
void board_riscv_virt_init(edge_event_sink_t *sink);
void board_riscv_virt_timer_init(void);
__attribute__((noreturn)) void board_riscv_virt_exit(int code);

#ifdef __cplusplus
}
#endif

#endif
