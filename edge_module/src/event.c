#include "edge/event.h"

#include <stdatomic.h>

edge_status_t edge_event_queue_init(edge_event_queue_t *queue, edge_event_t *storage,
                                    uint32_t capacity) {
    if (queue == NULL || storage == NULL || capacity < 2u)
        return EDGE_EINVAL;
    queue->items = storage;
    queue->capacity = capacity;
    queue->head = 0u;
    queue->tail = 0u;
    queue->dropped = 0u;
    queue->high_watermark = 0u;
    queue->policy = (uint8_t)EDGE_QUEUE_DROP_NEWEST;
    queue->critical_reserve = 0u;
    return EDGE_OK;
}

edge_status_t edge_event_queue_set_policy(edge_event_queue_t *queue, edge_queue_policy_t policy) {
    if (queue == NULL)
        return EDGE_EINVAL;
    queue->policy = (uint8_t)policy;
    return EDGE_OK;
}

edge_status_t edge_event_queue_set_critical_reserve(edge_event_queue_t *queue, uint8_t reserve) {
    if (queue == NULL)
        return EDGE_EINVAL;
    queue->critical_reserve = reserve;
    return EDGE_OK;
}

uint32_t edge_event_high_watermark(const edge_event_queue_t *queue) {
    return queue != NULL ? queue->high_watermark : 0u;
}

edge_status_t edge_event_push_isr(edge_event_queue_t *queue, const edge_event_t *event) {
    if (queue == NULL || event == NULL || queue->items == NULL || queue->capacity < 2u)
        return EDGE_EINVAL;

    const uint32_t head = queue->head;
    const uint32_t tail = queue->tail;
    const uint32_t count = (head + queue->capacity - tail) % queue->capacity;
    if (count + 1u > queue->high_watermark) {
        queue->high_watermark = count + 1u;
    }

    /* Check critical reserved slots */
    const uint32_t free_slots = (queue->capacity - 1u) - count;
    if (queue->critical_reserve > 0u && free_slots <= (uint32_t)queue->critical_reserve) {
        /* Only critical events (id < 0x0100) are accepted into reserved slots */
        if (event->id >= 0x0100u) {
            ++queue->dropped;
            return EDGE_EOVERFLOW;
        }
    }

    const uint32_t next = (head + 1u) % queue->capacity;
    if (next == tail) {
        if (queue->policy == (uint8_t)EDGE_QUEUE_DROP_OLDEST) {
            /* Drop oldest event: advance tail to make room */
            queue->tail = (tail + 1u) % queue->capacity;
            ++queue->dropped;
        } else {
            ++queue->dropped;
            return EDGE_EOVERFLOW;
        }
    }

    queue->items[head] = *event;
    /* Publish the payload before the index that makes it visible */
    atomic_thread_fence(memory_order_release);
    queue->head = next;
    return EDGE_OK;
}

edge_status_t edge_event_push_coalesced_isr(edge_event_queue_t *queue, const edge_event_t *event) {
    if (queue == NULL || event == NULL || queue->items == NULL || queue->capacity < 2u)
        return EDGE_EINVAL;

    const uint32_t head = queue->head;
    const uint32_t tail = queue->tail;
    if (head != tail) {
        const uint32_t prev = (head + queue->capacity - 1u) % queue->capacity;
        if (queue->items[prev].id == event->id && queue->items[prev].source == event->source) {
            /* Merge in-place */
            queue->items[prev].arg0 = event->arg0;
            queue->items[prev].arg1 = event->arg1;
            queue->items[prev].timestamp = event->timestamp;
            atomic_thread_fence(memory_order_release);
            return EDGE_OK;
        }
    }
    return edge_event_push_isr(queue, event);
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
    /* The other half of the pair above: observe the index, then read what the
     * producer published before it. */
    atomic_thread_fence(memory_order_acquire);
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
