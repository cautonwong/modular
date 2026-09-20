#ifndef EDGE_PAL_H
#define EDGE_PAL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*edge_pal_critical_fn)(void *self);
typedef uint64_t (*edge_pal_now_fn)(void *self);
typedef bool (*edge_pal_in_isr_fn)(void *self);
typedef void (*edge_pal_barrier_fn)(void *self);
typedef void (*edge_pal_isr_fn)(void *self);
typedef void (*edge_pal_idle_fn)(void *self);

typedef struct edge_pal_port {
    edge_pal_critical_fn critical_enter;
    edge_pal_critical_fn critical_exit;
    edge_pal_barrier_fn memory_barrier;
    edge_pal_now_fn monotonic_ticks;
    edge_pal_in_isr_fn in_isr;
    edge_pal_isr_fn isr_enter;
    edge_pal_isr_fn isr_exit;
    edge_pal_idle_fn idle; /* wait-for-interrupt / low-power until an IRQ (D71) */
    void *self;
} edge_pal_port_t;

#ifdef __cplusplus
}
#endif

#endif
