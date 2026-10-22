#ifndef EDGE_EVENT_H
#define EDGE_EVENT_H

#include "clock.h"
#include "module.h"
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * An event is a scalar fact: these four numbers and a timestamp, never a pointer.
 * A payload (a received frame, a measurement block) stays in the *producer's*
 * buffer and the consumer reads it through a port it defines itself (D66); the
 * number handed over here is a token - a length, or better a generation, which
 * lets the consumer's port refuse a frame that is already gone.
 *
 * A pointer in `arg0` would be just an integer to every check in this repository
 * and an address to a frame that no longer exists by the time the runner reads the
 * event. See docs/payload-token.md.
 */
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
    /* Indices are atomic so the release/acquire pair in src/event.c is a real
     * happens-before edge: a C11 fence orders atomic accesses, not plain ones, so
     * `volatile` here was a promise the standard does not make (#audit). */
    _Atomic uint32_t head;
    _Atomic uint32_t tail;
    /* Diagnostic counter, deliberately not atomic: a read-modify-write on Cortex-M0
     * has no exclusive instruction and would pull in libatomic for a number nobody
     * decides anything safety-related on. The push itself is masked, so the counter
     * is accurate whenever the queue is used as designed. */
    volatile uint32_t dropped;
} edge_event_queue_t;

typedef void (*edge_irq_enter_fn)(void *self);
typedef void (*edge_irq_exit_fn)(void *self);

typedef struct edge_irq_guard {
    edge_irq_enter_fn enter;
    edge_irq_exit_fn exit;
    void *self;
} edge_irq_guard_t;

typedef struct edge_event_sink {
    edge_event_queue_t *queue;
    const edge_clock_port_t *clock;
    const edge_irq_guard_t *guard;
} edge_event_sink_t;

edge_status_t edge_event_queue_init(edge_event_queue_t *queue, edge_event_t *storage,
                                    uint32_t capacity);
edge_status_t edge_event_push_isr(edge_event_queue_t *queue, const edge_event_t *event);
edge_status_t edge_event_sink_push_isr(edge_event_sink_t *sink, const edge_event_t *event);
edge_status_t edge_event_pop(edge_event_queue_t *queue, edge_event_t *event);
size_t edge_event_count(const edge_event_queue_t *queue);
uint32_t edge_event_dropped(const edge_event_queue_t *queue);

/* Zero the drop counter. It is cumulative by design - a queue that loses an event
 * should keep saying so - so resetting it is an explicit act, for a window reading. */
void edge_event_queue_reset_dropped(edge_event_queue_t *queue);

#ifdef __cplusplus
}
#endif

#endif
