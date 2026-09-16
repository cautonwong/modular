#ifndef EDGE_LOG_H
#define EDGE_LOG_H

typedef enum edge_log_level {
    EDGE_LOG_DEBUG = 0,
    EDGE_LOG_INFO,
    EDGE_LOG_WARN,
    EDGE_LOG_ERROR
} edge_log_level_t;

typedef struct edge_log_port {
    void (*write)(void *self, edge_log_level_t level, const char *message);
    void *self;
} edge_log_port_t;

#endif
