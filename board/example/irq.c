#include "edge/board_irq.h"

int edge_board_irq_forward(const edge_irq_route_t *route, uint32_t arg0, uint32_t arg1)
{
    if (!route || !route->sink) {
        return -1;
    }
    const edge_event_t event = {
        .id = route->event_id,
        .source = route->source_id,
        .arg0 = arg0,
        .arg1 = arg1,
        .timestamp = 0u,
    };
    return route->sink(&event, route->sink_arg);
}
