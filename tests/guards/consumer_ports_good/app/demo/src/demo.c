#include "demo/demo.h"
#include <stddef.h>

void demo_construct(demo_t *self, const demo_port_t *port) {
    if (self == NULL)
        return;
    self->port = port;
}
