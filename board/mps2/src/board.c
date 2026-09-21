#include "mps2/board.h"

#include "edge/events.h"
#include "soc_mps2/soc_mps2.h"

#include <stdbool.h>
#include <stddef.h>

static edge_event_sink_t *g_event_sink;

/* Shared-IRQ table (D84). Sized, static, no allocation (D21). */
typedef struct board_mps2_irq_handler {
    uint32_t irq;
    board_mps2_irq_fn cb;
    void *ctx;
} board_mps2_irq_handler_t;

static board_mps2_irq_handler_t g_irq_handlers[BOARD_MPS2_IRQ_MAX_HANDLERS];
static uint32_t g_irq_handler_count;
static bool g_defaults_registered;

edge_status_t board_mps2_irq_attach(uint32_t irq, board_mps2_irq_fn cb, void *ctx) {
    if (cb == NULL)
        return EDGE_EINVAL;
    if (g_irq_handler_count >= BOARD_MPS2_IRQ_MAX_HANDLERS)
        return EDGE_ENOSPC;
    g_irq_handlers[g_irq_handler_count] =
        (board_mps2_irq_handler_t){.irq = irq, .cb = cb, .ctx = ctx};
    ++g_irq_handler_count;
    return EDGE_OK;
}

void board_mps2_irq_dispatch(uint32_t irq) {
    /* Snapshot the count: a handler that registers another one while we walk the
     * table must not extend the walk, and the ISR stays bounded by the capacity
     * (D75). Registration order is dispatch order. */
    const uint32_t count = g_irq_handler_count;
    for (uint32_t i = 0u; i < count; ++i) {
        if (g_irq_handlers[i].irq != irq)
            continue;
        g_irq_handlers[i].cb(g_irq_handlers[i].ctx);
    }
}

uint32_t board_mps2_irq_handler_count(void) {
    return g_irq_handler_count;
}

/* The board's own timer consumer. It is registered through the same public entry
 * point a peripheral driver uses, so the built-in path exercises the dispatcher
 * on every real interrupt instead of reaching around it. */
static void on_timer0(void *ctx) {
    (void)ctx;
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

void board_mps2_init(edge_event_sink_t *sink) {
    g_event_sink = sink;
    if (!g_defaults_registered) {
        g_defaults_registered = true;
        (void)board_mps2_irq_attach(SOC_MPS2_TIMER0_IRQ, on_timer0, NULL);
    }
}

void board_mps2_timer_init(void) {
    *(volatile uint32_t *)(SOC_MPS2_TIMER0_BASE + SOC_MPS2_TIMER_RELOAD) = 250u;
    *(volatile uint32_t *)(SOC_MPS2_TIMER0_BASE + SOC_MPS2_TIMER_INTSTATUS) = 1u;
    *(volatile uint32_t *)(SOC_MPS2_TIMER0_BASE + SOC_MPS2_TIMER_CTRL) =
        SOC_MPS2_TIMER_CTRL_ENABLE | SOC_MPS2_TIMER_CTRL_IRQEN;
    SOC_MPS2_NVIC_ISER0 = (1u << SOC_MPS2_TIMER0_IRQ);
}

void board_mps2_timer_set_priority(uint8_t library_priority) {
    soc_mps2_nvic_set_priority(SOC_MPS2_TIMER0_IRQ, library_priority);
}

void board_mps2_irq_timer0(void) {
    /* Vector entry for this line: clear it, then hand over to the dispatcher.
     * One entry per line, not per handler (D84). */
    *(volatile uint32_t *)(SOC_MPS2_TIMER0_BASE + SOC_MPS2_TIMER_INTSTATUS) = 1u;
    board_mps2_irq_dispatch(SOC_MPS2_TIMER0_IRQ);
}

void board_mps2_irq_uart0_rx(uint32_t byte_count) {
    /* Not vector-wired yet: the MPS2 UART RX line is not routed through the
     * dispatcher because nothing in the tree enables it. When it is, this becomes
     * a `board_mps2_irq_fn` and the byte count moves into the context (D84). */
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
    /*
     * SYS_EXIT_EXTENDED (0x20), not SYS_EXIT: r1 points at
     * { ADP_Stopped_ApplicationExit, code }, so the harness sees the real exit
     * status. Plain SYS_EXIT collapses every failure to 1, which is exactly the
     * information a smoke test needs to tell "asserted at 9" from "returned 5".
     */
    const uint32_t args[2] = {0x20026u, (uint32_t)code};
    register int r0 __asm("r0") = 0x20; /* SYS_EXIT_EXTENDED */
    register const uint32_t *r1 __asm("r1") = args;
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
