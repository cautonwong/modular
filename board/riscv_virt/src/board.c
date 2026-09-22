#include "riscv_virt/board.h"

#include "edge/events.h"

#include <stdint.h>

/* QEMU `virt` machine map. Addresses are uintptr_t so the casts stay exact. */
#define TEST_FINISHER ((uintptr_t)0x00100000u)
#define CLINT_MTIME_LO ((uintptr_t)0x0200bff8u)
#define CLINT_MTIMECMP0_LO ((uintptr_t)0x02004000u)
#define CLINT_MTIMECMP0_HI ((uintptr_t)0x02004004u)
#define CLINT_MTIME_HI ((uintptr_t)0x0200bffcu)

#define MCAUSE_TIMER 7u
#define MIE_MTIE (1u << 7)
#define MSTATUS_MIE (1u << 3)

#define TIMER_DELTA 1000u
#define TEST_PASS 0x5555u
#define TEST_FAIL 0x3333u

static edge_event_sink_t *g_event_sink;

__attribute__((interrupt("machine"))) void board_riscv_virt_trap(void) {
    uint32_t mcause = 0u;
    __asm__ volatile("csrr %0, mcause" : "=r"(mcause));
    if ((mcause & 0x80000000u) == 0u || (mcause & 0xffu) != MCAUSE_TIMER)
        return;

    /* Stop the timer, then hand the fact to the injected sink. */
    *(volatile uint32_t *)CLINT_MTIMECMP0_LO = 0xffffffffu;
    *(volatile uint32_t *)CLINT_MTIMECMP0_HI = 0xffffffffu;
    if (g_event_sink == NULL)
        return;

    const edge_event_t event = {.id = EDGE_EVT_BOARD_TIMER0,
                                .source = MCAUSE_TIMER,
                                .arg0 = 1u,
                                .arg1 = 0u,
                                .timestamp = 0u};
    (void)edge_event_sink_push_isr(g_event_sink, &event);
}

void board_riscv_virt_init(edge_event_sink_t *sink) {
    g_event_sink = sink;
    const uintptr_t vector = (uintptr_t)&board_riscv_virt_trap;
    __asm__ volatile("csrw mtvec, %0" ::"r"(vector));
}

void board_riscv_virt_timer_init(void) {
    /* Both words: the counter is 64-bit and its high word is non-zero after ~429 s
     * at 10 MHz. See riscv_virt_deadline() for the carry and where it is tested. */
    uint32_t cmp_lo = 0u;
    uint32_t cmp_hi = 0u;
    riscv_virt_deadline(*(volatile uint32_t *)CLINT_MTIME_LO, *(volatile uint32_t *)CLINT_MTIME_HI,
                        TIMER_DELTA, &cmp_lo, &cmp_hi);
    *(volatile uint32_t *)CLINT_MTIMECMP0_LO = cmp_lo;
    *(volatile uint32_t *)CLINT_MTIMECMP0_HI = cmp_hi;
    __asm__ volatile("csrs mie, %0" ::"r"(MIE_MTIE));
    __asm__ volatile("csrs mstatus, %0" ::"r"(MSTATUS_MIE));
}

void board_riscv_virt_exit(int code) {
    /*
     * The SiFive test finisher carries the status: 0x5555 exits 0, and the failure
     * word takes the code in its upper half, which QEMU turns back into the exit
     * status. Collapsing every failure to one value is how "the timer never fired"
     * and "an assert fired" became indistinguishable - and how a firmware that
     * hangs instead of exiting could pass a smoke step that asserts nothing.
     */
    *(volatile uint32_t *)TEST_FINISHER =
        (code == 0) ? TEST_PASS : ((uint32_t)code << 16) | TEST_FAIL;
    for (;;) {
    }
}
