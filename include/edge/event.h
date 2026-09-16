#ifndef EDGE_EVENT_H
#define EDGE_EVENT_H

#include <stddef.h>
#include <stdint.h>
#include "clock.h"
#include "module.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct edge_event {
    uint32_t id;
    uint32_t source;
    uint32_t arg0;
    uint32_t arg1;
    uint64_t timestamp;
} edge_event_t;

_Static_assert(sizeof(edge_event_t) == 24u, "edge_event_t ABI changed");

typedef struct edge_event_queue {
    edge_event_t *items;
    uint32_t capacity;
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t dropped;
} edge_event_queue_t;

typedef struct edge_event_sink {
    edge_event_queue_t *queue;
    const edge_clock_port_t *clock;
} edge_event_sink_t;

edge_status_t edge_event_queue_init(edge_event_queue_t *queue,
                                    edge_event_t *storage,
                                    uint32_t capacity);
edge_status_t edge_event_push_isr(edge_event_queue_t *queue,
                                  const edge_event_t *event);
edge_status_t edge_event_sink_push_isr(edge_event_sink_t *sink,
                                       const edge_event_t *event);
edge_status_t edge_event_pop(edge_event_queue_t *queue, edge_event_t *event);
size_t edge_event_count(const edge_event_queue_t *queue);
uint32_t edge_event_dropped(const edge_event_queue_t *queue);

#ifdef __cplusplus
}
#endif

#endif
