#ifndef PAL_RTOS_H
#define PAL_RTOS_H

#include "edge/module.h"
#include "pal_os/os.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Neutral RTOS host contract.
 *
 * A product runs its superloop as a single RTOS task and injects events from a
 * sibling task. Products depend on this header only, so RTOS headers stay
 * confined to `pal/rtos/<os>` (D44/D47).
 */
typedef void (*edge_rtos_task_fn)(void *arg);

edge_status_t edge_rtos_task_create(const char *name, edge_rtos_task_fn fn, void *arg,
                                    uint32_t stack_words, uint32_t priority);

/* Starts the RTOS scheduler. Does not return. */
void edge_rtos_start(void);

/* yield/sleep backed by the RTOS, for the sys idle hook. */
edge_os_port_t edge_rtos_os_port(void);

/* Bytes of stack still unused by the calling task (high-water mark). */
uint32_t edge_rtos_task_stack_high_water(void);

#ifdef __cplusplus
}
#endif

#endif
