#include "mps2/board.h"

#include "edge/events.h"
#include "soc_mps2/soc_mps2.h"

static edge_event_sink_t *g_event_sink;

void board_mps2_init(edge_event_sink_t *sink) {
    g_event_sink = sink;
}

void board_mps2_timer_init(void) {
    *(volatile uint32_t *)(SOC_MPS2_TIMER0_BASE + SOC_MPS2_TIMER_RELOAD) = 250u;
    *(volatile uint32_t *)(SOC_MPS2_TIMER0_BASE + SOC_MPS2_TIMER_INTSTATUS) = 1u;
    *(volatile uint32_t *)(SOC_MPS2_TIMER0_BASE + SOC_MPS2_TIMER_CTRL) =
        SOC_MPS2_TIMER_CTRL_ENABLE | SOC_MPS2_TIMER_CTRL_IRQEN;
    SOC_MPS2_NVIC_ISER0 = (1u << SOC_MPS2_TIMER0_IRQ);
}

void board_mps2_irq_timer0(void) {
    *(volatile uint32_t *)(SOC_MPS2_TIMER0_BASE + SOC_MPS2_TIMER_INTSTATUS) = 1u;
    if (g_event_sink == NULL)
        return;
    const edge_event_t event = {
        .id = EDGE_EVT_BOARD_TIMER0,
        .source = SOC_MPS2_TIMER0_IRQ,
        .arg0 = 1u,
        .arg1 = 0u,
        .timestamp = 0u,
    };
    (void)edge_event_sink_push_isr(g_event_sink, &event);
}

void board_mps2_irq_uart0_rx(uint32_t byte_count) {
    if (g_event_sink == NULL)
        return;
    const edge_event_t event = {
        .id = EDGE_EVT_UART0_RX,
        .source = 0u,
        .arg0 = byte_count,
        .arg1 = 0u,
        .timestamp = 0u,
    };
    (void)edge_event_sink_push_isr(g_event_sink, &event);
}

__attribute__((noreturn)) void board_mps2_exit(int code) {
#ifdef EDGE_QEMU_SEMIHOSTING
    const int reason = (code == 0) ? 0x20026 : 0x20023;
    register int r0 __asm("r0") = 0x18; /* SYS_EXIT */
    register int r1 __asm("r1") = reason;
    __asm volatile("bkpt 0xAB" : : "r"(r0), "r"(r1) : "memory");
#else
    (void)code;
#endif
    for (;;) {
    }
}

void board_mps2_enter_low_power(void) {
    /* Board-specific clock/power gating would go here; the atomic wait itself is
     * the PAL's (edge_os_idle_wait). */
}

void board_mps2_feed_watchdog(void) {
    /* MPS2 has no watchdog model in QEMU; the call site is what matters. */
}

void board_mps2_system_reset(void) {
#if defined(__arm__)
    __asm volatile("dsb 0xF" ::: "memory");
    *(volatile uint32_t *)0xe000ed0cu = 0x05FA0004u; /* AIRCR: SYSRESETREQ */
#endif
    for (;;) {
    }
}
