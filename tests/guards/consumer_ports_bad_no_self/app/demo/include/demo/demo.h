#ifndef DEMO_H
#define DEMO_H
#include <stddef.h>
#include <stdint.h>

/* VIOLATION: Port struct missing void *self */
typedef struct demo_port {
    int (*read)(uint8_t *buf, size_t len);
} demo_port_t;

#endif
