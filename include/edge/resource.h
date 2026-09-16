#ifndef EDGE_RESOURCE_H
#define EDGE_RESOURCE_H
#include <stdint.h>
#include <stddef.h>
struct edge_module_context;
typedef uint32_t edge_resource_id_t;
typedef struct edge_resource_ops { int (*open)(void *impl, struct edge_module_context *ctx); int (*close)(void *impl, struct edge_module_context *ctx); int (*control)(void *impl, uint32_t op, void *arg); } edge_resource_ops_t;
typedef struct { edge_resource_id_t id; const char *name; const edge_resource_ops_t *ops; void *impl; uint32_t owner; } edge_resource_t;
#define EDGE_RESOURCE_NONE 0u
int edge_resource_claim(edge_resource_t *, uint32_t owner);
int edge_resource_release(edge_resource_t *, uint32_t owner);
int edge_resource_control(edge_resource_t *, uint32_t op, void *arg);
#endif
