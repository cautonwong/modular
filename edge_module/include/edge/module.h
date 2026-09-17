#ifndef EDGE_MODULE_H
#define EDGE_MODULE_H

#include "errors.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct edge_event edge_event_t;
typedef struct edge_module edge_module_t;

typedef edge_status_t (*edge_module_init_fn)(edge_module_t *self);
typedef edge_status_t (*edge_module_poll_fn)(edge_module_t *self);
typedef edge_status_t (*edge_module_event_fn)(edge_module_t *self, const edge_event_t *event);
typedef edge_status_t (*edge_module_power_off_fn)(edge_module_t *self);
typedef edge_status_t (*edge_module_deinit_fn)(edge_module_t *self);
typedef edge_status_t (*edge_module_suspend_fn)(edge_module_t *self);
typedef edge_status_t (*edge_module_resume_fn)(edge_module_t *self);

/*
 * Append-only layout (D40): new optional callbacks and flags are added at the
 * end so already compiled modules keep their field offsets.
 */
typedef struct edge_module {
    uint32_t module_id;
    uint32_t priority;
    uint32_t period;
    uint32_t budget;
    uint64_t next_due;
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
    edge_module_suspend_fn suspend; /* optional low-power entry (D51) */
    edge_module_resume_fn resume;   /* optional low-power exit (D51)   */
    uint8_t suspended;
    uint8_t fatal; /* assembly-time init failure is fatal to the product (D53) */
    uint8_t reserved2;
    uint8_t reserved3;
} edge_module_t;

#ifdef EDGE_TARGET_ARM32
_Static_assert(sizeof(void *) == 4u, "embedded ABI requires 32-bit pointers");
_Static_assert(sizeof(edge_module_t) == 64u, "edge_module_t ABI changed for 32-bit target");
#elif defined(EDGE_TARGET_RISCV32)
_Static_assert(sizeof(void *) == 4u, "rv32 ABI requires 32-bit pointers");
_Static_assert(sizeof(edge_module_t) == 64u, "edge_module_t ABI changed for rv32 target");
#elif defined(__LP64__) || defined(_LP64)
_Static_assert(sizeof(void *) == 8u, "host ABI requires 64-bit pointers");
_Static_assert(sizeof(edge_module_t) == 104u, "edge_module_t ABI changed on 64-bit host");
#endif

static inline void *edge_module_data(edge_module_t *self) {
    return self ? self->private_data : NULL;
}

#ifdef __cplusplus
}
#endif

#endif
