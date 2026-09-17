#ifndef EDGE_EVENT_H
#define EDGE_EVENT_H
#include <stdint.h>
typedef struct edge_event {
    uint32_t id;
    uint32_t source;
    uint32_t arg0;
    uint32_t arg1;
    uint64_t timestamp;
} edge_event_t;
#endif
