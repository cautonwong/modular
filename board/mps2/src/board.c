#include "mps2/board.h"

#include "edge/events.h"

/* CMSDK APB timer 0 and the Cortex-M NVIC on the MPS2 AN386 image. Addresses
 * are uintptr_t so the same translation unit also compiles on 64-bit hosts. */
#define TIMER0_BASE ((uintptr_t)0x40000000u)
#define TIMER_CTRL 0x00u
#define TIMER_RELOAD 0x08u
#define TIMER_INTSTATUS 0x0cu
#define TIMER_CTRL_ENABLE (1u << 0)
#define TIMER_CTRL_IRQEN (1u << 3)
#define TIMER0_IRQ 8u
#define NVIC_ISER0 (*(volatile uint32_t *)(uintptr_t)0xe000e100u)

static edge_event_sink_t *g_event_sink;

void board_mps2_init(edge_event_sink_t *sink) {
    g_event_sink = sink;
}

void board_mps2_timer_init(void) {
    *(volatile uint32_t *)(TIMER0_BASE + TIMER_RELOAD) = 250u;
    *(volatile uint32_t *)(TIMER0_BASE + TIMER_INTSTATUS) = 1u;
    *(volatile uint32_t *)(TIMER0_BASE + TIMER_CTRL) = TIMER_CTRL_ENABLE | TIMER_CTRL_IRQEN;
    NVIC_ISER0 = (1u << TIMER0_IRQ);
}

void board_mps2_irq_timer0(void) {
    *(volatile uint32_t *)(TIMER0_BASE + TIMER_INTSTATUS) = 1u;
    if (g_event_sink == NULL)
        return;
    const edge_event_t event = {
        .id = EDGE_EVT_BOARD_TIMER0,
        .source = TIMER0_IRQ,
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
