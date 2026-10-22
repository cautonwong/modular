#ifndef DEMO_GLUE_H
#define DEMO_GLUE_H

#include "edge/module.h"

typedef struct demo_glue_state {
    edge_module_t telemetry_module;
    int dummy;
} demo_glue_state_t;

#endif
