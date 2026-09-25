#ifndef APP_TIMEOUT_GUARD_INTERNAL_H
#define APP_TIMEOUT_GUARD_INTERNAL_H

/*
 * Private definition of timeout_guard. Same contract as vesc_comm: the public
 * header exposes an opaque type plus a size and an alignment, and the two
 * assertions below are what keep that contract from going stale when a field is
 * added here.
 */

#include "edge/module.h"
#include "timeout_guard/timeout_guard.h"
#include <assert.h>
#include <stdalign.h>
#include <stddef.h>

struct timeout_guard {
    edge_module_t module;

    /* Injected Port */
    const timeout_motor_port_t *motor;

    /* Timeout Configuration */
    uint32_t timeout_ms;
    float brake_current_a;

    /* State */
    uint64_t last_feed_ticks;
    uint32_t ticks_per_ms;
    bool has_timed_out;
    uint32_t timeout_count;
};

static_assert(sizeof(struct timeout_guard) <= TIMEOUT_GUARD_STORAGE_SIZE,
              "TIMEOUT_GUARD_STORAGE_SIZE is stale: the caller would under-allocate");
static_assert(alignof(struct timeout_guard) <= TIMEOUT_GUARD_STORAGE_ALIGN,
              "TIMEOUT_GUARD_STORAGE_ALIGN is stale: the caller would under-align");

#endif /* APP_TIMEOUT_GUARD_INTERNAL_H */
