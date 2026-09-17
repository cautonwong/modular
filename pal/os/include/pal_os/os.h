#ifndef PAL_OS_H
#define PAL_OS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal host/OS abstraction for a product that runs the sys superloop inside
 * an RTOS task.
 *
 * It intentionally covers only "give the CPU back" operations. Critical
 * sections, barriers and time stay in the architecture PAL (`edge/pal.h`), so
 * this contract never duplicates them.
 */
typedef void (*edge_os_yield_fn)(void *self);
typedef void (*edge_os_sleep_ms_fn)(void *self, uint32_t ms);

typedef struct edge_os_port {
    edge_os_yield_fn yield;
    edge_os_sleep_ms_fn sleep_ms;
    void *self;
} edge_os_port_t;

#ifdef __cplusplus
}
#endif

#endif
