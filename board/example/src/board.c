#include "example/board.h"
#include "edge/events.h"

static edge_event_sink_t *g_event_sink;

void board_example_init(edge_event_sink_t *sink)
{
    g_event_sink = sink;
}

void board_example_irq_uart0_rx(uint32_t byte_count)
{
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
