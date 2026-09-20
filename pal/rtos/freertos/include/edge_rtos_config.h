#ifndef EDGE_RTOS_CONFIG_H
#define EDGE_RTOS_CONFIG_H

/*
 * PAL-side FreeRTOS configuration contract (D87).
 *
 * `pal/` must not depend on `board/`, `soc/` or `product/` (layer rule), so this
 * header is included by the *product-owned* `FreeRTOSConfig.h` **after** the
 * product has supplied its values. It then:
 *
 *   1. fills in the defaults a product is not expected to change, and
 *   2. fails the build if the product disabled a PAL-required capability.
 *
 * Keeping the checks here means the invariants cannot be silently dropped by a
 * product; the product only has to include this header last.
 */

/* Defaults for values a product is not expected to change. */
#ifndef configUSE_PREEMPTION
#define configUSE_PREEMPTION 1
#endif
#ifndef configUSE_IDLE_HOOK
#define configUSE_IDLE_HOOK 0
#endif
#ifndef configUSE_TASK_NOTIFICATIONS
#define configUSE_TASK_NOTIFICATIONS 1
#endif
#ifndef INCLUDE_xTaskGetCurrentTaskHandle
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#endif
#ifndef configUSE_TICK_HOOK
#define configUSE_TICK_HOOK 0
#endif
#ifndef configUSE_16_BIT_TICKS
#define configUSE_16_BIT_TICKS 0
#endif
#ifndef configIDLE_SHOULD_YIELD
#define configIDLE_SHOULD_YIELD 1
#endif
#ifndef configUSE_RECURSIVE_MUTEXES
#define configUSE_RECURSIVE_MUTEXES 0
#endif
#ifndef configUSE_COUNTING_SEMAPHORES
#define configUSE_COUNTING_SEMAPHORES 0
#endif
#ifndef configUSE_TIMERS
#define configUSE_TIMERS 0
#endif
#ifndef configUSE_MALLOC_FAILED_HOOK
#define configUSE_MALLOC_FAILED_HOOK 0
#endif
#ifndef configUSE_TRACE_FACILITY
#define configUSE_TRACE_FACILITY 0
#endif
#ifndef INCLUDE_vTaskDelete
#define INCLUDE_vTaskDelete 1
#endif
#ifndef INCLUDE_vTaskSuspend
#define INCLUDE_vTaskSuspend 1
#endif

/*
 * PAL-required invariants. A product must not be able to silently disable the
 * safety nets the stack-usage / overrun workstream depends on.
 */
#ifndef configCHECK_FOR_STACK_OVERFLOW
#error "PAL requires configCHECK_FOR_STACK_OVERFLOW (set it before including edge_rtos_config.h)"
#elif configCHECK_FOR_STACK_OVERFLOW == 0
#error "PAL requires configCHECK_FOR_STACK_OVERFLOW != 0"
#endif

#ifndef INCLUDE_uxTaskGetStackHighWaterMark
#error "PAL requires INCLUDE_uxTaskGetStackHighWaterMark for edge_rtos_task_stack_high_water()"
#elif INCLUDE_uxTaskGetStackHighWaterMark == 0
#error "PAL requires INCLUDE_uxTaskGetStackHighWaterMark != 0"
#endif

#ifndef configASSERT
#error "PAL requires a product-provided configASSERT; a silent no-op is not allowed"
#endif

#ifndef configSUPPORT_DYNAMIC_ALLOCATION
#error "PAL requires configSUPPORT_DYNAMIC_ALLOCATION to be stated explicitly"
#endif

/*
 * The wake/block primitive (edge_rtos_wait_for_work) is the difference between a
 * battery product and a spinning demo, so its kernel features are required, not
 * optional: task notifications are what the ISR uses to wake the runner, and
 * xTaskGetCurrentTaskHandle is how the runner publishes itself as the target.
 */
#ifndef configUSE_TASK_NOTIFICATIONS
#error "PAL requires configUSE_TASK_NOTIFICATIONS for edge_rtos_wait_for_work"
#elif configUSE_TASK_NOTIFICATIONS == 0
#error "PAL requires configUSE_TASK_NOTIFICATIONS != 0"
#endif

#ifndef INCLUDE_xTaskGetCurrentTaskHandle
#error "PAL requires INCLUDE_xTaskGetCurrentTaskHandle for edge_rtos_wake_target_set_self"
#elif INCLUDE_xTaskGetCurrentTaskHandle == 0
#error "PAL requires INCLUDE_xTaskGetCurrentTaskHandle != 0"
#endif

#endif
