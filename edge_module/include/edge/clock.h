#ifndef EDGE_CLOCK_H
#define EDGE_CLOCK_H

#include <stdint.h>

typedef struct edge_clock_port {
    uint64_t (*monotonic_ticks)(void *self);
    uint64_t (*wall_time)(void *self);
    void *self;
} edge_clock_port_t;

#endif
