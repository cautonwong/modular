#ifndef TX_USER_H
#define TX_USER_H

/*
 * Product-owned ThreadX configuration (D87).
 *
 * This file states the product's decisions; the PAL's contract
 * (`edge_threadx_config.h`) validates them and supplies the rest. It must stay **pure
 * preprocessor**: ThreadX includes it from assembly as well as C
 * (`TX_INCLUDE_USER_DEFINE_FILE`), so no typedef, no include and no declaration may
 * appear here.
 *
 * Include order, as on the FreeRTOS side: this file is the composition root, and the
 * PAL's contract is included last so the invariants cannot be skipped.
 */

/* Tick frequency: the unit of the sleep conversion and of `period`/`budget` when a
 * kernel-tick clock is injected (docs/time-model.md). */
#define TX_TIMER_TICKS_PER_SECOND 1000

/* A failed check is never silent in this framework, and ThreadX has no assert macro:
 * the closest thing is to keep the error checking and the stack checking that feed the
 * fault hook (the PAL's contract refuses a product that disables either). */
#define TX_ENABLE_STACK_CHECKING

/*
 * Mask mode, per target.
 *
 * On Cortex-M the product chooses BASEPRI so the syscall ceiling is what gets masked -
 * the same latency property the other PALs have - and states it explicitly. On the
 * host port the choice is nominal: its critical section is the vendor's own.
 */
#if defined(__arm__)
#define TX_PORT_USE_BASEPRI
/* The ceiling sits at 4, one step above the board's timer IRQ (5): the kernel tick
 * takes that step so it can preempt a busy peripheral interrupt, and both stay maskable
 * by a critical section - which is the property the kernel needs from every interrupt
 * that calls into it. At the same priority the two cannot preempt each other, and a
 * fast periodic IRQ then starves the tick (measured: the witness task froze at 20 ms
 * and every kernel timeout stopped expiring). */
#define TX_PORT_BASEPRI (4u << (8u - 4u)) /* library priority 4, 4 priority bits */
#define EDGE_THREADX_MASK_MODE 1          /* basepri */
#else
#define EDGE_THREADX_MASK_MODE 0 /* primask (host port: nominal) */
#endif

/* The static pool, stated by the product (D87). On the host the floor is 8 KiB per
 * task, because the port builds its thread context on the task's own stack. */
#define EDGE_THREADX_MAX_TASKS 4u
#define EDGE_THREADX_STACK_BYTES 8192u

#include "edge_threadx_config.h"

#endif
