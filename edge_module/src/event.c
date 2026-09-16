#include "edge/event.h"

edge_status_t edge_event_queue_init(edge_event_queue_t *queue, edge_event_t *storage,
                                    uint32_t capacity) {
    if (queue == NULL || storage == NULL || capacity < 2u)
        return EDGE_EINVAL;
    queue->items = storage;
    queue->capacity = capacity;
    queue->head = 0u;
    queue->tail = 0u;
    queue->dropped = 0u;
    return EDGE_OK;
}

edge_status_t edge_event_push_isr(edge_event_queue_t *queue, const edge_event_t *event) {
    if (queue == NULL || event == NULL || queue->items == NULL || queue->capacity < 2u)
        return EDGE_EINVAL;
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

edge_status_t edge_event_sink_push_isr(edge_event_sink_t *sink, const edge_event_t *event) {
    if (sink == NULL || event == NULL || sink->queue == NULL)
        return EDGE_EINVAL;
    if (sink->guard != NULL && sink->guard->enter != NULL)
        sink->guard->enter(sink->guard->self);

    edge_event_t stamped = *event;
    if (sink->clock != NULL && sink->clock->monotonic_ticks != NULL) {
        stamped.timestamp = sink->clock->monotonic_ticks(sink->clock->self);
    }
    const edge_status_t rc = edge_event_push_isr(sink->queue, &stamped);

    if (sink->guard != NULL && sink->guard->exit != NULL)
        sink->guard->exit(sink->guard->self);
    return rc;
}

edge_status_t edge_event_pop(edge_event_queue_t *queue, edge_event_t *event) {
    if (queue == NULL || event == NULL || queue->items == NULL || queue->capacity < 2u)
        return EDGE_EINVAL;
    const uint32_t tail = queue->tail;
    if (tail == queue->head)
        return EDGE_ENOENT;
    *event = queue->items[tail];
    queue->tail = (tail + 1u) % queue->capacity;
    return EDGE_OK;
}

size_t edge_event_count(const edge_event_queue_t *queue) {
    if (queue == NULL || queue->items == NULL || queue->capacity < 2u)
        return 0u;
    return (size_t)((queue->head + queue->capacity - queue->tail) % queue->capacity);
}

uint32_t edge_event_dropped(const edge_event_queue_t *queue) {
    return queue != NULL ? queue->dropped : 0u;
}
