#include "riscv_virt/board.h"

#include "edge/events.h"

#include <stdint.h>

/* QEMU `virt` machine map. Addresses are uintptr_t so the casts stay exact. */
#define TEST_FINISHER ((uintptr_t)0x00100000u)
#define CLINT_MTIME_LO ((uintptr_t)0x0200bff8u)
#define CLINT_MTIMECMP0_LO ((uintptr_t)0x02004000u)
#define CLINT_MTIMECMP0_HI ((uintptr_t)0x02004004u)

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
    const uint32_t now = *(volatile uint32_t *)CLINT_MTIME_LO;
    *(volatile uint32_t *)CLINT_MTIMECMP0_LO = now + TIMER_DELTA;
    *(volatile uint32_t *)CLINT_MTIMECMP0_HI = 0u;
    __asm__ volatile("csrs mie, %0" ::"r"(MIE_MTIE));
    __asm__ volatile("csrs mstatus, %0" ::"r"(MSTATUS_MIE));
}

void board_riscv_virt_exit(int code) {
    *(volatile uint32_t *)TEST_FINISHER = (code == 0) ? TEST_PASS : TEST_FAIL;
    for (;;) {
    }
}
