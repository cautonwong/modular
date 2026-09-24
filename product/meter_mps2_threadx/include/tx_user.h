#ifndef TX_USER_H
#define TX_USER_H

/*
 * Product-owned ThreadX configuration (D87) for the Cortex-M4 firmware.
 *
 * Pure preprocessor, because ThreadX includes it from assembly as well as C
 * (`TX_INCLUDE_USER_DEFINE_FILE`), and the PAL's contract is included last so its
 * invariants cannot be skipped.
 */

/* Tick frequency. SysTick is programmed from this in the product's low-level
 * initialisation, and it is the unit of the sleep conversion (docs/time-model.md). */
#define TX_TIMER_TICKS_PER_SECOND 1000

/* ThreadX ships no assert macro. Keeping the error checking and the stack checking that
 * feed its fault path is the closest thing to this framework's "never silent" rule, and
 * the PAL's contract refuses a product that disables either. */
#define TX_ENABLE_STACK_CHECKING

/* BASEPRI, so a critical section masks everything at or above the library ceiling and
 * leaves NMI and HardFault alone. TX_PORT_BASEPRI is written by the port's assembly too,
 * which is why it is stated here rather than left to the default. */
#define TX_PORT_USE_BASEPRI
/* The ceiling sits at 4, one step above the board's timer IRQ (5): the kernel tick
 * takes that step so it can preempt a busy peripheral interrupt, and both stay maskable
 * by a critical section - which is the property the kernel needs from every interrupt
 * that calls into it. At the same priority the two cannot preempt each other, and a
 * fast periodic IRQ then starves the tick (measured: the witness task froze at 20 ms
 * and every kernel timeout stopped expiring). */
#define TX_PORT_BASEPRI (4u << (8u - 4u)) /* library priority 4, 4 priority bits */
#define EDGE_THREADX_MASK_MODE 1          /* basepri */

/*
 * The static task pool, which only the product can size: it owns both the number of
 * tasks and their stacks. The port refuses a request that does not fit rather than
 * overrunning, and this board has 16 KiB of RAM in total, so an unspecified pool would
 * be either too small to create anything or too large to link.
 */
#define EDGE_THREADX_MAX_TASKS 2u
#define EDGE_THREADX_STACK_BYTES 2048u

#include "edge_threadx_config.h"

#endif
