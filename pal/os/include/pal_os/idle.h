#ifndef PAL_OS_IDLE_H
#define PAL_OS_IDLE_H

#include "edge/pal.h"
#include "pal_os/os.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bridge an `edge_os_port_t` to the sys idle hook (D52/D71): when a runner step
 * had no event and no due poll, the capsule yields and, if configured, sleeps.
 */
typedef struct edge_os_idle {
    const edge_os_port_t *os;
    uint32_t sleep_ms;
} edge_os_idle_t;

/* Signature-compatible with `edge_sys_idle_fn`. */
void edge_os_idle_hook(void *ctx);

typedef bool (*edge_os_pending_fn)(void *ctx);

/*
 * Atomic low-power wait (D71): disable interrupts, re-check `pending`, only then
 * wait for the next interrupt, and re-enable. A fact posted between the check and
 * the sleep therefore cannot be lost. `pending` may be NULL to always wait.
 */
void edge_os_idle_wait(const edge_pal_port_t *pal, edge_os_pending_fn pending, void *ctx);

#ifdef __cplusplus
}
#endif

#endif
