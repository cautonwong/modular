#ifndef DEMO_H
#define DEMO_H
#include <stdint.h>
typedef struct demo {
    uint32_t count;
} demo_t;
void demo_init(demo_t *self);
#endif
