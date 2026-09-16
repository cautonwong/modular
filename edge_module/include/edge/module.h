#ifndef EDGE_MODULE_H
#define EDGE_MODULE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum edge_status {
    EDGE_OK = 0,
    EDGE_EINVAL = -1,
    EDGE_ENOENT = -2,
    EDGE_EBUSY = -3,
    EDGE_ESTATE = -4,
    EDGE_EDEPEND = -5,
    EDGE_EOVERFLOW = -6,
    EDGE_ENOSPC = -7,
    EDGE_EIO = -8,
    EDGE_ENOTSUP = -9
} edge_status_t;

typedef struct edge_event edge_event_t;
typedef struct edge_module edge_module_t;

typedef edge_status_t (*edge_module_init_fn)(edge_module_t *self);
typedef edge_status_t (*edge_module_poll_fn)(edge_module_t *self);
typedef edge_status_t (*edge_module_event_fn)(edge_module_t *self,
                                               const edge_event_t *event);
typedef edge_status_t (*edge_module_power_off_fn)(edge_module_t *self);
typedef edge_status_t (*edge_module_deinit_fn)(edge_module_t *self);

typedef struct edge_module {
    uint32_t module_id;
    uint32_t priority;
    edge_module_init_fn init;
    edge_module_poll_fn poll;
    edge_module_event_fn on_event;
    edge_module_power_off_fn power_off;
    edge_module_deinit_fn deinit;
    void *private_data;
    uint8_t initialized;
    uint8_t running;
    uint8_t failed;
    uint8_t reserved;
} edge_module_t;

#ifdef EDGE_TARGET_ARM32
_Static_assert(sizeof(void *) == 4u, "embedded ABI requires 32-bit pointers");
_Static_assert(sizeof(edge_module_t) == 32u, "edge_module_t ABI changed for 32-bit target");
#endif

static inline void *edge_module_data(edge_module_t *self)
{
    return self ? self->private_data : NULL;
}

#ifdef __cplusplus
}
#endif

#endif
