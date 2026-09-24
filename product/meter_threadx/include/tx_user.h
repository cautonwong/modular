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
#define TX_PORT_BASEPRI (5u << (8u - 4u)) /* library priority 5, 4 priority bits */
#define EDGE_THREADX_MASK_MODE 1          /* basepri */
#else
#define EDGE_THREADX_MASK_MODE 0 /* primask (host port: nominal) */
#endif

#include "edge_threadx_config.h"

#endif
