#ifndef EDGE_SERVICE_H
#define EDGE_SERVICE_H
#include <stdint.h>
#include <stddef.h>
typedef uint32_t edge_service_id_t;
typedef struct { edge_service_id_t id; uint16_t major; uint16_t minor; const void *api; } edge_service_t;
typedef struct { const edge_service_t *items; size_t count; } edge_service_registry_t;
int edge_service_register(edge_service_registry_t *, const edge_service_t *);
const edge_service_t *edge_service_get(const edge_service_registry_t *, edge_service_id_t, uint16_t major, uint16_t min_minor);
#endif
