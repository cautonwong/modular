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

#include "pal_rtos/rtos.h"

/* Product resource decisions. */
#define configMAX_PRIORITIES (5)
#define configMINIMAL_STACK_SIZE ((unsigned short)128)
#define configTOTAL_HEAP_SIZE ((size_t)(8 * 1024))
#define configMAX_TASK_NAME_LEN (8)
#define configUSE_MUTEXES 0
#define configSUPPORT_STATIC_ALLOCATION 0
#define configSUPPORT_DYNAMIC_ALLOCATION 1

/* PAL-required capabilities, stated explicitly by the product. */
#define configCHECK_FOR_STACK_OVERFLOW 2
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_vTaskDelay 1

/*
 * Assert policy: never silent. The composition root installs an observable
 * handler (see main.c); with no handler installed, edge_rtos_assert_failed()
 * halts instead of continuing.
 */
#define configASSERT(x) ((x) ? (void)0 : edge_rtos_assert_failed(__FILE__, __LINE__))

/* PAL contract: defaults for anything not set above, then the invariant checks. */
#include "edge_rtos_config.h"

#endif
