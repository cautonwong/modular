#ifndef GUARD_DEMO_APP_H
#define GUARD_DEMO_APP_H

#include "edge/event.h"

int demo_app_wrap(int a, int b);
void demo_app_init(int pair, const edge_event_sink_t *sink);

#endif
