#include "edge/event.h"
#include "edge/module.h"

int edge_event_queue_init(edge_event_queue_t *queue, edge_event_t *items,
                          uint32_t capacity)
{
    if (!queue || !items || capacity < 2u) {
        return EDGE_EINVAL;
    }
    queue->items = items;
    queue->capacity = capacity;
    queue->head = 0u;
    queue->tail = 0u;
    queue->dropped = 0u;
    return EDGE_OK;
}

int edge_event_push_isr(edge_event_queue_t *queue, const edge_event_t *event)
{
    if (!queue || !event || !queue->items || queue->capacity < 2u) {
        return EDGE_EINVAL;
    }

    const uint32_t head = queue->head;
    const uint32_t next = (head + 1u) % queue->capacity;
    if (next == queue->tail) {
        ++queue->dropped;
        return EDGE_EOVERFLOW;
    }

    queue->items[head] = *event;
    queue->head = next;
    return EDGE_OK;
}

int edge_event_pop(edge_event_queue_t *queue, edge_event_t *event)
{
    if (!queue || !event || !queue->items || queue->capacity < 2u) {
        return EDGE_EINVAL;
    }

    const uint32_t tail = queue->tail;
    if (tail == queue->head) {
        return EDGE_ENOENT;
    }

    *event = queue->items[tail];
    queue->tail = (tail + 1u) % queue->capacity;
    return EDGE_OK;
}

size_t edge_event_count(const edge_event_queue_t *queue)
{
    if (!queue || queue->capacity < 2u) {
        return 0u;
    }
    return (size_t)((queue->head + queue->capacity - queue->tail) % queue->capacity);
}

uint32_t edge_event_dropped(const edge_event_queue_t *queue)
{
    return queue ? queue->dropped : 0u;
}

int edge_event_queue_sink_isr(const edge_event_t *event, void *arg)
{
    return edge_event_push_isr((edge_event_queue_t *)arg, event);
}
