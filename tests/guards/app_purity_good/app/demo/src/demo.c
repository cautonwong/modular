#include "demo/demo.h"
#include <stddef.h>

static const uint8_t g_lookup[4] = {1, 2, 3, 4}; /* OK: static const lookup table */

void demo_init(demo_t *self) {
    if (self != NULL) {
        self->count = g_lookup[0];
    }
}
