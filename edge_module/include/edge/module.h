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

typedef edge_status_t (*edge_module_poll_fn)(edge_module_t *self);
typedef edge_status_t (*edge_module_event_fn)(edge_module_t *self, const edge_event_t *event);
typedef edge_status_t (*edge_module_power_off_fn)(edge_module_t *self);
typedef edge_status_t (*edge_module_suspend_fn)(edge_module_t *self);
typedef edge_status_t (*edge_module_resume_fn)(edge_module_t *self);

/*
 * D51: `init` and `deinit` are deliberately **not** part of this struct. The
 * composition root calls `<app>_init(self, deps)` at assembly time and
 * `<app>_deinit(self)` at shutdown time (reverse order) -- assembly failures and
 * their skip/fatal policy therefore belong to the product, not to the scheduler.
 *
 * Fields after `next_due` are the framework contract; the scheduler adds its own
 * bookkeeping (`budget`, `next_due`) and state flags.
 */
typedef struct edge_module {
    uint32_t module_id;
    uint32_t priority;
    uint32_t period;
    uint32_t budget;
    uint64_t next_due;
    edge_module_poll_fn poll;
    edge_module_event_fn on_event;
    edge_module_power_off_fn power_off; /* sys-ordered: save module state (D9) */
    void *private_data;
    uint8_t running;
    uint8_t failed;
    uint8_t suspended;
    uint8_t reserved;
    edge_module_suspend_fn suspend; /* optional low-power entry (D51) */
    edge_module_resume_fn resume;   /* optional low-power exit (D51)   */
    uint32_t reserved2;
} edge_module_t;

#ifdef EDGE_TARGET_ARM32
_Static_assert(sizeof(void *) == 4u, "embedded ABI requires 32-bit pointers");
_Static_assert(sizeof(edge_module_t) == 56u, "edge_module_t ABI changed for 32-bit target");
#elif defined(EDGE_TARGET_RISCV32)
_Static_assert(sizeof(void *) == 4u, "rv32 ABI requires 32-bit pointers");
_Static_assert(sizeof(edge_module_t) == 56u, "edge_module_t ABI changed for rv32 target");
#elif defined(__LP64__) || defined(_LP64)
_Static_assert(sizeof(void *) == 8u, "host ABI requires 64-bit pointers");
_Static_assert(sizeof(edge_module_t) == 88u, "edge_module_t ABI changed on 64-bit host");
#endif

static inline void *edge_module_data(edge_module_t *self) {
    return self ? self->private_data : NULL;
}

#ifdef __cplusplus
}
#endif

#endif
