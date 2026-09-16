#ifndef EDGE_BOARD_IRQ_H
#define EDGE_BOARD_IRQ_H

#include "event.h"
#include <stdint.h>

typedef int (*edge_irq_event_sink_fn)(const edge_event_t *event, void *arg);

typedef struct {
    uint32_t irq;
    uint32_t event_id;
    uint32_t source_id;
    edge_irq_event_sink_fn sink;
    void *sink_arg;
} edge_irq_route_t;

int edge_board_irq_forward(const edge_irq_route_t *route, uint32_t arg, uint32_t data);

#endif
