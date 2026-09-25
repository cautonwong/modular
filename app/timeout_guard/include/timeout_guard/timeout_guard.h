#ifndef APP_TIMEOUT_GUARD_H
#define APP_TIMEOUT_GUARD_H

#include "edge/errors.h"
#include "edge/module.h"
#include <stdalign.h>
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

/*
 * Opaque, caller-provided memory:
 *
 *   static alignas(TIMEOUT_GUARD_STORAGE_ALIGN)
 *       unsigned char storage[TIMEOUT_GUARD_STORAGE_SIZE];
 *   timeout_guard_t *guard = (timeout_guard_t *)storage;
 *
 * The definition lives in src/timeout_guard_internal.h, which also asserts that
 * the size and alignment below still cover it.
 */
#define TIMEOUT_GUARD_STORAGE_SIZE 128u
#define TIMEOUT_GUARD_STORAGE_ALIGN alignof(max_align_t)

typedef struct timeout_guard timeout_guard_t;

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
