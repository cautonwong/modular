/*
 * The one piece of a ThreadX Cortex-M firmware that the vendor port does not ship: the
 * port's `ports/cortex_m3/gnu/src` has no `tx_initialize_low_level.S` (its own readme
 * says that file is responsible for the vector area and a periodic timer), and it does
 * not wire the kernel timer either. Everything here is the MPS2's hardware facts, which
 * is why it lives in the product and not in `pal/`.
 *
 * The vector table itself is `.github/arm/startup.c`, driven by EDGE_*_HANDLER, so this
 * file only has to provide the two symbols that table names.
 */
#include "tx_api.h"

#include <stdint.h>

/* The MPS2 AN386's core clock. This is the only calibration knob here: it turns
 * TX_TIMER_TICKS_PER_SECOND into a SysTick reload value, and getting it wrong changes
 * the tick's frequency, not its monotonicity or the scheduling policy. */
#define EDGE_MPS2_AN386_HZ 25000000u

/* SysTick and the system control space registers. Three addresses, so no CMSIS: the
 * bare-metal PAL addresses SysTick the same way. */
#define SYSTICK_CTRL (*(volatile uint32_t *)0xe000e010u)
#define SYSTICK_LOAD (*(volatile uint32_t *)0xe000e014u)
#define SYSTICK_VAL (*(volatile uint32_t *)0xe000e018u)
#define SCS_SHPR2 (*(volatile uint32_t *)0xe000ed1cu) /* SVCall */
#define SCS_SHPR3 (*(volatile uint32_t *)0xe000ed20u) /* PendSV, SysTick */

/* Provided by the vendor port. */
void _tx_timer_interrupt(void);

/* The kernel tick. ThreadX calls this vector, which is why the table points at it. */
void __tx_SysTickHandler(void) {
    _tx_timer_interrupt();
}

/*
 * Called by ThreadX from `_tx_initialize_kernel_enter()` before `tx_application_define()`
 * and before any thread runs. Priorities first: the tick has to be maskable by the
 * port's BASEPRI ceiling, and the context switch has to sit below every maskable
 * interrupt - the vendor's own low-level file orders it the same way.
 */
void _tx_initialize_low_level(void) {
    SCS_SHPR2 = (SCS_SHPR2 & 0x00ffffffu) | 0xff000000u;  /* SVCall lowest */
    SCS_SHPR3 = (SCS_SHPR3 & 0x0000ffffu) | (0xffu << 16) /* PendSV lowest */
                | (4u << 24); /* SysTick above the board IRQ, below nothing maskable */
    SYSTICK_LOAD = (EDGE_MPS2_AN386_HZ / TX_TIMER_TICKS_PER_SECOND) - 1u;
    SYSTICK_VAL = 0u;
    SYSTICK_CTRL = 0x7u; /* core clock, interrupt enabled, counting */
}
