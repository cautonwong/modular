#ifndef EDGE_EVENT_H
#define EDGE_EVENT_H
#include <stdint.h>
#include <stddef.h>
typedef struct { uint32_t id; uint32_t source; uint32_t arg; uint32_t data; } edge_event_t;
typedef struct { edge_event_t *items; uint32_t capacity; volatile uint32_t head; volatile uint32_t tail; } edge_event_queue_t;
int edge_event_queue_init(edge_event_queue_t *, edge_event_t *, uint32_t);
int edge_event_push_isr(edge_event_queue_t *, const edge_event_t *);
int edge_event_pop(edge_event_queue_t *, edge_event_t *);
size_t edge_event_count(const edge_event_queue_t *);
#endif
