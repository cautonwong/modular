#include "example/board.h"
#include "edge/events.h"

static edge_event_sink_t *g_event_sink;

void board_example_init(edge_event_sink_t *sink) {
    g_event_sink = sink;
}

void board_example_irq_uart0_rx(uint32_t byte_count) {
    if (g_event_sink == NULL) {
        return;
    }
    const edge_event_t event = {
        .id = EDGE_EVT_UART0_RX,
        .source = 0u,
        .arg0 = byte_count,
        .arg1 = 0u,
        .timestamp = 0u,
    };
    (void)edge_event_sink_push_isr(g_event_sink, &event);
}

#ifdef EDGE_QEMU_SEMIHOSTING
#define TIMER0_BASE 0x40000000u
#define TIMER_CTRL 0x00u
#define TIMER_RELOAD 0x08u
#define TIMER_INTSTATUS 0x0cu
#define TIMER_CTRL_ENABLE (1u << 0)
#define TIMER_CTRL_IRQEN (1u << 3)
#define NVIC_ISER0 (*(volatile uint32_t *)0xe000e100u)

void board_example_qemu_timer0_init(void) {
    *(volatile uint32_t *)(TIMER0_BASE + TIMER_RELOAD) = 250u;
    *(volatile uint32_t *)(TIMER0_BASE + TIMER_INTSTATUS) = 1u;
    *(volatile uint32_t *)(TIMER0_BASE + TIMER_CTRL) =
        TIMER_CTRL_ENABLE | TIMER_CTRL_IRQEN;
    NVIC_ISER0 = (1u << 8);
}

void board_example_irq_timer0(void) {
    *(volatile uint32_t *)(TIMER0_BASE + TIMER_INTSTATUS) = 1u;
    if (g_event_sink == NULL) {
        return;
    }
    const edge_event_t event = {
        .id = EDGE_EVT_BOARD_TIMER0,
        .source = 8u,
        .arg0 = 1u,
        .arg1 = 0u,
        .timestamp = 0u,
    };
    (void)edge_event_sink_push_isr(g_event_sink, &event);
}
#else
void board_example_qemu_timer0_init(void) {}
void board_example_irq_timer0(void) {}
#endif
