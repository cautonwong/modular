#include "edge/event.h"

#include <stdatomic.h>

edge_status_t edge_event_queue_init(edge_event_queue_t *queue, edge_event_t *storage,
                                    uint32_t capacity) {
    if (queue == NULL || storage == NULL || capacity < 2u)
        return EDGE_EINVAL;
    queue->items = storage;
    queue->capacity = capacity;
    atomic_store_explicit(&queue->head, 0u, memory_order_relaxed);
    atomic_store_explicit(&queue->tail, 0u, memory_order_relaxed);
    atomic_store_explicit(&queue->dropped, 0u, memory_order_relaxed);
    return EDGE_OK;
}

edge_status_t edge_event_push_isr(edge_event_queue_t *queue, const edge_event_t *event) {
    if (queue == NULL || event == NULL || queue->items == NULL || queue->capacity < 2u)
        return EDGE_EINVAL;
    const uint32_t head = atomic_load_explicit(&queue->head, memory_order_relaxed);
    const uint32_t next = (head + 1u) % queue->capacity;
    if (next == atomic_load_explicit(&queue->tail, memory_order_acquire)) {
        atomic_fetch_add_explicit(&queue->dropped, 1u, memory_order_relaxed);
        return EDGE_EOVERFLOW;
    }
    queue->items[head] = *event;
    /* The store that publishes the payload: a release store orders the slot write
     * before the index write, so a consumer that sees the new head sees the event. */
    atomic_store_explicit(&queue->head, next, memory_order_release);
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
    const uint32_t tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);
    if (tail == atomic_load_explicit(&queue->head, memory_order_acquire))
        return EDGE_ENOENT;
    /* Acquire above: the slot is readable only after the index that published it. */
    *event = queue->items[tail];
    atomic_store_explicit(&queue->tail, (tail + 1u) % queue->capacity, memory_order_release);
    return EDGE_OK;
}

size_t edge_event_count(const edge_event_queue_t *queue) {
    if (queue == NULL || queue->items == NULL || queue->capacity < 2u)
        return 0u;
    const uint32_t head = atomic_load_explicit(&queue->head, memory_order_acquire);
    const uint32_t tail = atomic_load_explicit(&queue->tail, memory_order_acquire);
    return (size_t)((head + queue->capacity - tail) % queue->capacity);
}

void edge_event_queue_reset_dropped(edge_event_queue_t *queue) {
    if (queue != NULL)
        atomic_store_explicit(&queue->dropped, 0u, memory_order_relaxed);
}

uint32_t edge_event_dropped(const edge_event_queue_t *queue) {
    return queue != NULL ? atomic_load_explicit(&queue->dropped, memory_order_relaxed) : 0u;
}
