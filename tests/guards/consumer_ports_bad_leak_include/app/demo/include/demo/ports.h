#ifndef DEMO_PORTS_H
#define DEMO_PORTS_H
#include "edge/ports.h"
#include "infra/uart/uart.h" /* VIOLATION: leaking concrete infra header in consumer port */

typedef struct demo_port {
    int (*read)(void *self, uint8_t *buf, size_t len);
    void *self;
} demo_port_t;

#endif
