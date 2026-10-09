#ifndef EDGE_RING_BUFFER_H
#define EDGE_RING_BUFFER_H

#include "errors.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Driver-owned lockless Single-Producer Single-Consumer (SPSC) Ring Buffer
 * (ADR-007, IRQ-DATA-002 ~ 003)
 */
typedef struct edge_ring_buffer {
    uint8_t *storage;
    size_t capacity;
    volatile size_t head;
    volatile size_t tail;
    uint32_t overflow_count;
} edge_ring_buffer_t;

static inline void edge_ring_buffer_init(edge_ring_buffer_t *rb, uint8_t *storage,
                                         size_t capacity) {
    if (rb == NULL) {
        return;
    }
    rb->storage = storage;
    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->overflow_count = 0;
}

static inline size_t edge_ring_buffer_size(const edge_ring_buffer_t *rb) {
    if (rb == NULL || rb->storage == NULL) {
        return 0;
    }
    size_t head = rb->head;
    size_t tail = rb->tail;
    if (head >= tail) {
        return head - tail;
    }
    return rb->capacity - (tail - head);
}

static inline size_t edge_ring_buffer_free_space(const edge_ring_buffer_t *rb) {
    if (rb == NULL || rb->storage == NULL || rb->capacity == 0) {
        return 0;
    }
    return (rb->capacity - 1u) - edge_ring_buffer_size(rb);
}

static inline bool edge_ring_buffer_is_empty(const edge_ring_buffer_t *rb) {
    return rb == NULL || rb->head == rb->tail;
}

static inline bool edge_ring_buffer_is_full(const edge_ring_buffer_t *rb) {
    return edge_ring_buffer_free_space(rb) == 0;
}

static inline edge_status_t edge_ring_buffer_write_byte(edge_ring_buffer_t *rb, uint8_t byte) {
    if (rb == NULL || rb->storage == NULL || rb->capacity == 0) {
        return EDGE_EINVAL;
    }
    size_t next_head = (rb->head + 1u) % rb->capacity;
    if (next_head == rb->tail) {
        rb->overflow_count++;
        return EDGE_ENOSPC;
    }
    rb->storage[rb->head] = byte;
    __asm__ __volatile__("" ::: "memory");
    rb->head = next_head;
    return EDGE_OK;
}

static inline edge_status_t edge_ring_buffer_read_byte(edge_ring_buffer_t *rb, uint8_t *out_byte) {
    if (rb == NULL || rb->storage == NULL || out_byte == NULL) {
        return EDGE_EINVAL;
    }
    if (rb->head == rb->tail) {
        return EDGE_ENOENT;
    }
    *out_byte = rb->storage[rb->tail];
    __asm__ __volatile__("" ::: "memory");
    rb->tail = (rb->tail + 1u) % rb->capacity;
    return EDGE_OK;
}

static inline size_t edge_ring_buffer_write(edge_ring_buffer_t *rb, const uint8_t *data,
                                            size_t len) {
    if (rb == NULL || rb->storage == NULL || data == NULL || len == 0) {
        return 0;
    }
    size_t written = 0;
    for (size_t i = 0; i < len; ++i) {
        if (edge_ring_buffer_write_byte(rb, data[i]) != EDGE_OK) {
            break;
        }
        written++;
    }
    return written;
}

static inline size_t edge_ring_buffer_read(edge_ring_buffer_t *rb, uint8_t *dest, size_t max_len) {
    if (rb == NULL || rb->storage == NULL || dest == NULL || max_len == 0) {
        return 0;
    }
    size_t read_bytes = 0;
    for (size_t i = 0; i < max_len; ++i) {
        if (edge_ring_buffer_read_byte(rb, &dest[i]) != EDGE_OK) {
            break;
        }
        read_bytes++;
    }
    return read_bytes;
}

#ifdef __cplusplus
}
#endif

#endif /* EDGE_RING_BUFFER_H */
