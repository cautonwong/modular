#ifndef PAL_HOST_H
#define PAL_HOST_H

#include "edge/pal.h"
#include "pal_os/os.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Host PAL: a process-local platform abstraction used by unit tests and host
 * products. Critical sections are a nesting counter and the monotonic clock is
 * a settable software counter.
 */
edge_pal_port_t pal_host_port(void);

/* Host OS port: `yield` and `sleep_ms` are recorded no-ops. */
edge_os_port_t pal_host_os_port(void);

/* Test controls. */
void pal_host_set_ticks(uint64_t ticks);
uint32_t pal_host_critical_depth(void);
void pal_host_set_in_isr(bool in_isr);
uint32_t pal_host_yields(void);
uint64_t pal_host_slept_ms(void);
uint32_t pal_host_idle_waits(void);

#ifdef __cplusplus
}
#endif

#endif
