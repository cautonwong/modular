#ifndef BOARD_RISCV_VIRT_H
#define BOARD_RISCV_VIRT_H

#include "edge/event.h"

/*
 * MTIMECMP is 64 bits wide on riscv-virt, and `mtime` passes 2^32 after ~429 s at
 * the machine's 10 MHz - long after a CI smoke finishes and well inside a real
 * deployment. Arming with `hi = 0` therefore looks right in CI and fails in the
 * field: once the high word is non-zero the compare value is in the past and the
 * timer fires immediately instead of after the delta.
 *
 * Pure, and in the header, so the carry is host-testable: reaching the boundary in
 * CI would otherwise take seven minutes of emulated time.
 */
static inline void riscv_virt_deadline(uint32_t now_lo, uint32_t now_hi, uint32_t delta,
                                       uint32_t *cmp_lo, uint32_t *cmp_hi) {
    const uint32_t lo = now_lo + delta;
    *cmp_lo = lo;
    *cmp_hi = now_hi + ((lo < now_lo) ? 1u : 0u); /* carry out of the low word */
}

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
