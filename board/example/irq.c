#include "edge/board_irq.h"

int edge_board_irq_forward(const edge_irq_route_t *route, uint32_t arg, uint32_t data) {
    if (!route || !route->sink) return -1;
    edge_event_t event = {
        .id = route->event_id,
        .source = route->source_id,
        .arg = arg,
        .data = data,
    };
    return route->sink(&event, route->sink_arg);
}
