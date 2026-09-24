#ifndef APP_TIMEOUT_GUARD_H
#define APP_TIMEOUT_GUARD_H

#include "edge/errors.h"
#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Consumer-Defined Ports (Rule: void *self; callbacks take void *self)
 */
typedef struct timeout_motor_port {
    edge_status_t (*emergency_stop)(void *self);
    edge_status_t (*set_brake_current)(void *self, float current);
    void *self;
} timeout_motor_port_t;

typedef struct timeout_guard {
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
} timeout_guard_t;

void timeout_guard_construct(timeout_guard_t *self, uint32_t module_id, uint32_t priority,
                             const timeout_motor_port_t *motor, uint32_t timeout_ms,
                             float brake_current_a, uint32_t ticks_per_ms);

edge_status_t timeout_guard_init(timeout_guard_t *self);
edge_status_t timeout_guard_deinit(timeout_guard_t *self);
edge_status_t timeout_guard_feed(timeout_guard_t *self, uint64_t now_ticks);
bool timeout_guard_is_timed_out(const timeout_guard_t *self);
edge_module_t *timeout_guard_module(timeout_guard_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_TIMEOUT_GUARD_H */
