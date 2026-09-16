#ifndef EDGE_MODULE_H
#define EDGE_MODULE_H
#include "module_abi.h"
#include "event.h"
#include "resource.h"
#include "service.h"
#include <stddef.h>
#include <stdint.h>
enum edge_status { EDGE_OK=0, EDGE_EINVAL=-1, EDGE_ENOENT=-2, EDGE_EBUSY=-3, EDGE_ESTATE=-4, EDGE_EABI=-5, EDGE_EDEPEND=-6, EDGE_EOVERFLOW=-7, EDGE_EQUEUE=-8, EDGE_ENOSPC=-9, EDGE_ENOTSUP=-10 };
enum edge_module_state { EDGE_MODULE_CREATED, EDGE_MODULE_INITIALIZED, EDGE_MODULE_RUNNING, EDGE_MODULE_SUSPENDED, EDGE_MODULE_STOPPED, EDGE_MODULE_FAILED };
struct edge_module_context;
typedef int (*edge_module_init_fn)(struct edge_module_context *);
typedef int (*edge_module_power_fn)(struct edge_module_context *);
typedef int (*edge_module_poll_fn)(struct edge_module_context *);
typedef int (*edge_module_deinit_fn)(struct edge_module_context *);
typedef int (*edge_module_suspend_fn)(struct edge_module_context *);
typedef int (*edge_module_resume_fn)(struct edge_module_context *);
typedef struct edge_module_context { void *private_data; const edge_module_preamble_v1_t *preamble; void *platform; void *config; edge_event_queue_t *events; uint32_t state; uint32_t instance_id; } edge_module_context_t;
typedef struct edge_module_descriptor { const edge_module_preamble_v1_t *preamble; uint32_t instance_id; edge_module_context_t *ctx; edge_module_init_fn init; edge_module_power_fn power_on; edge_module_poll_fn poll; edge_module_power_fn power_off; edge_module_deinit_fn deinit; edge_module_suspend_fn suspend; edge_module_resume_fn resume; const uint32_t *dependencies; size_t dependency_count; const uint32_t *resources; size_t resource_count; const uint32_t *events; size_t event_count; } edge_module_descriptor_t;
typedef struct { edge_module_descriptor_t *instances; uint8_t *started; size_t count; const edge_module_descriptor_t **registry; size_t registry_count; } edge_module_manager_t;
int edge_module_validate_preamble(const edge_module_preamble_v1_t *p);
int edge_module_manager_init(edge_module_manager_t *, edge_module_descriptor_t *, uint8_t *, size_t, const edge_module_descriptor_t **, size_t);
int edge_module_init_all(edge_module_manager_t *); int edge_module_power_on_all(edge_module_manager_t *); int edge_module_poll_all(edge_module_manager_t *); int edge_module_power_off_all(edge_module_manager_t *); int edge_module_deinit_all(edge_module_manager_t *); int edge_module_suspend_all(edge_module_manager_t *); int edge_module_resume_all(edge_module_manager_t *);
const edge_module_descriptor_t *edge_module_find(const edge_module_manager_t *, uint32_t instance_id);
#if defined(__GNUC__)
#define EDGE_MODULE_REGISTER(name, descriptor) static const edge_module_descriptor_t * const edge_module_registry_##name __attribute__((section(".edge_module_registry"), used)) = &(descriptor)
#endif
#endif
