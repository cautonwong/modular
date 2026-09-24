#ifndef DEMO_H
#define DEMO_H
#include <stddef.h>
#include <stdint.h>

typedef struct demo_port {
    int (*read)(void *self, uint8_t *buf, size_t len);
    void *self;
} demo_port_t;

typedef struct demo {
    const demo_port_t *port;
} demo_t;

void demo_construct(demo_t *self, const demo_port_t *port);
#endif
