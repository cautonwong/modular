#ifndef PAL_CORTEX_M_H
#define PAL_CORTEX_M_H

#include "edge/pal.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bare-metal Cortex-M PAL (D46/D85).
 *
 * - critical sections save/restore PRIMASK (nesting counted in `depth`);
 * - `memory_barrier` is a DSB;
 * - `monotonic_ticks` extends a free-running SysTick to 64 bits;
 * - `in_isr` reads IPSR.
 *
 * On a non-ARM host build the same symbols exist with deterministic fallbacks so
 * the target still compiles and the contract can be exercised on the host.
 */
typedef struct edge_pal_cortex_m_state {
    uint32_t primask;
    uint32_t depth;
    uint64_t wrap;
    uint64_t host_ticks;
} edge_pal_cortex_m_state_t;

/* Configure SysTick as a free-running monotonic source and zero the state. */
void edge_pal_cortex_m_bare_init(edge_pal_cortex_m_state_t *state);

edge_pal_port_t edge_pal_cortex_m_bare_port(edge_pal_cortex_m_state_t *state);

#ifdef __cplusplus
}
#endif

#endif
