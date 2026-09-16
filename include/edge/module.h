#ifndef EDGE_MODULE_H
#define EDGE_MODULE_H

#include <stddef.h>
#include <stdint.h>

typedef enum edge_status {
    EDGE_OK = 0, EDGE_EINVAL = -1, EDGE_ENOENT = -2, EDGE_EBUSY = -3,
    EDGE_ESTATE = -4, EDGE_EABI = -5, EDGE_EDEPEND = -6,
    EDGE_EOVERFLOW = -7, EDGE_EQUEUE = -8, EDGE_ENOSPC = -9, EDGE_ENOTSUP = -10
} edge_status_t;

typedef struct edge_event edge_event_t;
typedef struct edge_module edge_module_t;
typedef int (*edge_module_init_fn)(edge_module_t *);
typedef int (*edge_module_poll_fn)(edge_module_t *);
typedef int (*edge_module_event_fn)(edge_module_t *, const edge_event_t *);
typedef int (*edge_module_power_off_fn)(edge_module_t *);

typedef struct edge_module {
    uint32_t module_id;
    uint32_t priority;
    edge_module_init_fn init;
    edge_module_poll_fn poll;
    edge_module_event_fn on_event;
    edge_module_power_off_fn power_off;
    void *private_data;
} edge_module_t;

/* Existing descriptor ABI is retained for source compatibility. New products use edge_module_t directly. */
#include "module_abi.h"
typedef struct edge_module_context {
    void *private_data;
    const edge_module_preamble_v1_t *preamble;
    void *platform;
    void *config;
    void *events;
    uint32_t state;
    uint32_t instance_id;
} edge_module_context_t;

typedef struct edge_module_descriptor {
    const edge_module_preamble_v1_t *preamble;
    uint32_t instance_id;
    uint32_t module_id;
    uint32_t priority;
    edge_module_context_t *ctx;
    int (*init)(edge_module_context_t *);
    int (*power_on)(edge_module_context_t *);
    int (*poll)(edge_module_context_t *);
    int (*power_off)(edge_module_context_t *);
    int (*deinit)(edge_module_context_t *);
    int (*suspend)(edge_module_context_t *);
    int (*resume)(edge_module_context_t *);
    const uint32_t *dependencies;
    size_t dependency_count;
} edge_module_descriptor_t;

typedef struct edge_module_manager {
    edge_module_descriptor_t *instances;
    uint8_t *started;
    size_t count;
    const edge_module_descriptor_t **registry;
    size_t registry_count;
} edge_module_manager_t;

int edge_module_validate_preamble(const edge_module_preamble_v1_t *p);
int edge_module_manager_init(edge_module_manager_t *, edge_module_descriptor_t *, uint8_t *, size_t, const edge_module_descriptor_t **, size_t);
int edge_module_init_all(edge_module_manager_t *);
int edge_module_power_on_all(edge_module_manager_t *);
int edge_module_poll_all(edge_module_manager_t *);
int edge_module_power_off_all(edge_module_manager_t *);
int edge_module_deinit_all(edge_module_manager_t *);
int edge_module_suspend_all(edge_module_manager_t *);
int edge_module_resume_all(edge_module_manager_t *);
const edge_module_descriptor_t *edge_module_find(const edge_module_manager_t *, uint32_t instance_id);

#endif
