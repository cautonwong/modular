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
 * Architecture binding (D49/D85): this port compiles for an ARM Cortex-M target
 * (`__arm__`) or, for host unit tests only, with EDGE_PAL_CORTEX_M_HOST_TEST.
 * Anything else fails to compile. A silent host fallback on a non-ARM target
 * would compile, link and "pass" while the clock reported call counts instead of
 * time, so it is refused rather than substituted.
 *
 * - critical sections save/restore PRIMASK (nesting counted in `depth`);
 * - `memory_barrier` is a DSB;
 * - `monotonic_ticks` extends a free-running SysTick to 64 bits;
 * - `in_isr` reads IPSR.
 *
 * ## Critical sections: the invariant this port relies on
 *
 * `critical_enter`/`critical_exit` are a void -> void pair, so the saved PRIMASK
 * has nowhere to live except the caller's state: there is one saved slot plus a
 * nesting counter, and the slot is written only on the outermost enter. The
 * consequence, which the whole design rests on:
 *
 *   **a critical section masks every maskable interrupt, so no masking context can
 *   run inside one.** An interrupt that cannot interleave cannot corrupt the saved
 *   slot or the counter, which is exactly why one slot is sufficient.
 *
 * NMI and HardFault are *not* masked by PRIMASK. They must not call these two
 * functions: an enter/exit pair inside a held critical section overwrites the
 * saved slot and, on its exit, restores a mask that was current for the fault -
 * leaving the interrupted critical section with interrupts enabled. That is a
 * silent failure, so it is excluded here rather than supported.
 *
 * `depth` and `primask` are shared state. Nothing outside this port may write
 * them; they are asserted by the host tests, which is also why `depth` exists.
 *
 * ## SysTick ownership
 *
 * `edge_pal_cortex_m_bare_init()` takes over SysTick (LOAD/VAL/CTRL) to provide
 * monotonic time, and this port is therefore **mutually exclusive with any RTOS
 * that owns the tick**. A product either uses this PAL for time or uses a kernel
 * tick (`pal/rtos/freertos`'s `edge_rtos_pal_port`), never both: initialising this
 * PAL under a running kernel would reprogram the tick out from under the
 * scheduler. Nothing in the tree calls it from an RTOS product today, and this
 * paragraph is the statement that keeps it that way.
 *
 * ## `idle` is a primitive, not the sequence
 *
 * `idle` is the raw wait (WFI). The D71 atomic sequence - mask, re-check pending
 * work, wait, unmask - lives in `edge_os_idle_wait()` (`pal/os`), which calls
 * `pal->idle` *inside* the mask. Putting the sequence here as well would
 * double-mask and, worse, move the re-check outside the mask that protects it.
 *
 * `monotonic_ticks` contract - both halves are load-bearing:
 *
 *   1. It must be sampled at least once per SysTick period (`load + 1` counts).
 *      The wrap is detected by comparing a sample with the previous one, so two
 *      wraps between two samples are indistinguishable from one and the time
 *      between them is lost silently.
 *   2. `load` is read once, in `edge_pal_cortex_m_bare_init`, and cached in the
 *      state. Changing SYSTICK_LOAD afterwards (an RTOS tick, a debugger, a later
 *      1 ms schedule) violates the contract: the cached period is what keeps the
 *      already-elapsed timeline continuous, and rescaling it mid-flight would
 *      silently move every previously reported timestamp. Such a change is
 *      observable in `anomalies` instead of being absorbed.
 */
typedef struct edge_pal_cortex_m_state {
    volatile uint32_t primask; /* saved on the outermost enter; see above */
    volatile uint32_t depth;   /* nesting count; also what the host tests assert */
    uint64_t wrap;
    uint64_t host_ticks;
    /* Appended (D40 append-only). */
    uint32_t load;      /* period - 1, cached at init; never re-read later */
    uint32_t last;      /* previous accepted VAL sample, for wrap detection */
    uint32_t anomalies; /* observed contract violations (VAL above the cached load) */
} edge_pal_cortex_m_state_t;

/* Configure SysTick as a free-running monotonic source, cache its period, and
 * zero the state. */
void edge_pal_cortex_m_bare_init(edge_pal_cortex_m_state_t *state);

edge_pal_port_t edge_pal_cortex_m_bare_port(edge_pal_cortex_m_state_t *state);

/*
 * Extend one SysTick sample into the 64-bit timeline. Pure: no registers, no
 * globals, so the arithmetic is host-testable while the register read is not.
 *
 * `val` is the SYSTICK_VAL sample. The wrap is detected from the counter itself:
 * SysTick counts *down*, so a sample above the previous one means it reloaded,
 * i.e. a wrap was crossed between the two samples.
 *
 * That comparison is deliberately preferred over COUNTFLAG. Reading CTRL clears
 * the flag, so a wrap landing between the VAL and CTRL reads is exactly the case
 * where the flag and the counter disagree - and the MPS2 model under QEMU does
 * not report the flag the way the documentation describes, so a flag-based
 * detector silently loses the wrap and the clock runs backwards by a whole
 * period. The counter's own value cannot lie about that.
 */
uint64_t edge_pal_cortex_m_extend(edge_pal_cortex_m_state_t *state, uint32_t val);

#ifdef __cplusplus
}
#endif

#endif
