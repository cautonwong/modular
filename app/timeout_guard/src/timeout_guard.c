#include "timeout_guard/timeout_guard.h"
#include <string.h>

static edge_status_t timeout_guard_poll(edge_module_t *module) {
    timeout_guard_t *self = (timeout_guard_t *)edge_module_data(module);
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }

    /* Check if timeout expired */
    uint64_t elapsed_ticks = (module->next_due >= self->last_feed_ticks)
                                 ? (module->next_due - self->last_feed_ticks)
                                 : 0u;

    uint64_t timeout_ticks = (uint64_t)self->timeout_ms * (uint64_t)self->ticks_per_ms;

    if (elapsed_ticks > timeout_ticks && !self->has_timed_out) {
        self->has_timed_out = true;
        self->timeout_count++;

        if (self->motor != (void *)0) {
            if (self->brake_current_a > 0.0f && self->motor->set_brake_current != (void *)0) {
                (void)self->motor->set_brake_current(self->motor->self, self->brake_current_a);
            } else if (self->motor->emergency_stop != (void *)0) {
                (void)self->motor->emergency_stop(self->motor->self);
            }
        }
    }

    return EDGE_OK;
}

static edge_status_t timeout_guard_on_event(edge_module_t *module, const edge_event_t *event) {
    timeout_guard_t *self = (timeout_guard_t *)edge_module_data(module);
    if (self == (void *)0 || event == (void *)0) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

static edge_status_t timeout_guard_power_off(edge_module_t *module) {
    timeout_guard_t *self = (timeout_guard_t *)edge_module_data(module);
    if (self != (void *)0 && self->motor != (void *)0 && self->motor->emergency_stop != (void *)0) {
        (void)self->motor->emergency_stop(self->motor->self);
    }
    return EDGE_OK;
}

void timeout_guard_construct(timeout_guard_t *self, uint32_t module_id, uint32_t priority,
                             const timeout_motor_port_t *motor, uint32_t timeout_ms,
                             float brake_current_a, uint32_t ticks_per_ms) {
    if (self == (void *)0) {
        return;
    }

    memset(self, 0, sizeof(*self));

    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 20u,
        .budget = 0u,
        .next_due = 0u,
        .poll = timeout_guard_poll,
        .on_event = timeout_guard_on_event,
        .power_off = timeout_guard_power_off,
        .private_data = self,
    };

    self->motor = motor;
    self->timeout_ms = timeout_ms > 0 ? timeout_ms : 1000;
    self->brake_current_a = brake_current_a;
    self->ticks_per_ms = ticks_per_ms > 0 ? ticks_per_ms : 1;
    self->last_feed_ticks = 0;
    self->has_timed_out = false;
    self->timeout_count = 0;
}

edge_status_t timeout_guard_init(timeout_guard_t *self) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }
    self->has_timed_out = false;
    return EDGE_OK;
}

edge_status_t timeout_guard_deinit(timeout_guard_t *self) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

edge_status_t timeout_guard_feed(timeout_guard_t *self, uint64_t now_ticks) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }
    self->last_feed_ticks = now_ticks;
    self->has_timed_out = false;
    return EDGE_OK;
}

bool timeout_guard_is_timed_out(const timeout_guard_t *self) {
    return self != (void *)0 ? self->has_timed_out : true;
}

edge_module_t *timeout_guard_module(timeout_guard_t *self) {
    if (self == (void *)0) {
        return (void *)0;
    }
    return &self->module;
}
