#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*
 * Product-owned FreeRTOS configuration (D87).
 *
 * This file is the composition root for the RTOS configuration, in order:
 *   `soc/<soc>`     interrupt priority encoding        (soc_mps2)
 *   `board/<board>` CPU clock / tick source            (mps2)
 *   product         heap, priorities, switches, assert policy (below)
 *   `pal/`          defaults + required-invariant check (edge_rtos_config.h)
 *
 * It lives under the product's include directory, which the composition root
 * puts on the include path of `freertos_kernel`, `pal_rtos_freertos` and the
 * firmware, so all three are compiled against the same configuration instance.
 * `pal/` ships no `FreeRTOSConfig.h`: the layer rule forbids `pal -> product`.
 *
 * Include blocks are kept sorted (case sensitive) for the clang-format gate;
 * the composition order is documented above instead.
 */
#include "mps2/freertos_config.h"
#include "soc_mps2/freertos_config.h"

/* Assert contract only: this header is included by every FreeRTOS kernel
 * translation unit, so it must not pull in the framework or the OS port. */
#include "pal_rtos/assert.h"

/* Product resource decisions. */
#define configMAX_PRIORITIES (5)
#define configMINIMAL_STACK_SIZE ((unsigned short)128)
#define configTOTAL_HEAP_SIZE ((size_t)(8 * 1024))
#define configMAX_TASK_NAME_LEN (8)
/* Tickless idle (D47): the scheduler's idle task suppresses the tick and sleeps
 * the core while nothing is runnable. The runner parks on a notification, so this
 * is what turns the parked time into real sleep. */
#define configUSE_TICKLESS_IDLE 1
#define configEXPECTED_IDLE_TIME_BEFORE_SLEEP 2
#define configUSE_MUTEXES 0
/* No heap: the port owns static task storage, and heap_4.c is not compiled in
 * (#172). Stated explicitly because the PAL requires it to be stated. */
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0

/* PAL-required capabilities, stated explicitly by the product. */
#define configCHECK_FOR_STACK_OVERFLOW 2
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_vTaskDelay 1
/* The runner parks instead of polling, and the ISR wakes it (D47/D71). */
#define configUSE_TASK_NOTIFICATIONS 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1

/*
 * Assert policy: never silent. The composition root installs an observable
 * handler (see main.c); with no handler installed, edge_rtos_assert_failed()
 * halts instead of continuing.
 */
#define configASSERT(x) ((x) ? (void)0 : edge_rtos_assert_failed(__FILE__, __LINE__))

/* PAL contract: defaults for anything not set above, then the invariant checks. */
#include "edge_rtos_config.h"

#endif
