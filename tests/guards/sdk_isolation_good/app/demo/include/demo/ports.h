#include "edge/errors.h"
#include <stdint.h>
#include <stddef.h>

typedef struct demo_sink_if {
    void *self;
    edge_status_t (*emit)(void *self, uint32_t val);
} demo_sink_if_t;
