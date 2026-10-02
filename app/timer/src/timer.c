#include "timer/timer.h"

static inline uint32_t get_now_ms(const watch_timer_app_t *self) {
    if (self->clock != NULL && self->clock->get_tick_ms != NULL) {
        return self->clock->get_tick_ms(self->clock->self);
    }
    return 0u;
}

static edge_status_t watch_timer_poll(edge_module_t *module) {
    watch_timer_app_t *self = (watch_timer_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }

    if (self->is_active) {
        uint32_t now = get_now_ms(self);
        int32_t remaining = (int32_t)(self->expiry_tick_ms - now);
        if (remaining <= 0) {
            self->is_active = false;
            self->is_triggered = true;

            if (!self->alert_emitted) {
                self->alert_emitted = true;
                if (self->alert != NULL && self->alert->start_alert != NULL) {
                    (void)self->alert->start_alert(self->alert->self);
                }
            }
        }
    }

    return EDGE_OK;
}

static edge_status_t watch_timer_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    return EDGE_OK;
}

static edge_status_t watch_timer_power_off(edge_module_t *module) {
    watch_timer_app_t *self = (watch_timer_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return watch_timer_shutdown(self);
}

void watch_timer_construct(watch_timer_app_t *self, uint32_t module_id, uint32_t priority,
                           const timer_clock_if_t *clock, const timer_alert_if_t *alert) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};

    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 1u,
        .budget = 0u,
        .next_due = 0u,
        .poll = watch_timer_poll,
        .on_event = watch_timer_on_event,
        .power_off = watch_timer_power_off,
        .private_data = self,
    };

    self->clock = clock;
    self->alert = alert;
    self->is_active = false;
    self->is_triggered = false;
    self->expiry_tick_ms = 0u;
    self->duration_ms = 0u;
    self->alert_emitted = false;
}

void timer_construct(watch_timer_app_t *self, uint32_t module_id, uint32_t priority,
                     const timer_clock_if_t *clock, const timer_alert_if_t *alert) {
    watch_timer_construct(self, module_id, priority, clock, alert);
}

edge_status_t watch_timer_init(watch_timer_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    watch_timer_stop(self);
    return EDGE_OK;
}

edge_status_t watch_timer_shutdown(watch_timer_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    watch_timer_stop(self);
    return EDGE_OK;
}

void watch_timer_start(watch_timer_app_t *self, uint32_t duration_ms) {
    if (self == NULL || duration_ms == 0u) {
        return;
    }
    uint32_t now = get_now_ms(self);
    self->duration_ms = duration_ms;
    self->expiry_tick_ms = now + duration_ms;
    self->is_active = true;
    self->is_triggered = false;
    self->alert_emitted = false;
}

void watch_timer_stop(watch_timer_app_t *self) {
    if (self == NULL) {
        return;
    }
    self->is_active = false;
    self->is_triggered = false;
    self->alert_emitted = false;

    if (self->alert != NULL && self->alert->stop_alert != NULL) {
        (void)self->alert->stop_alert(self->alert->self);
    }
}

void watch_timer_reset_expired(watch_timer_app_t *self) {
    if (self == NULL) {
        return;
    }
    self->is_triggered = false;
    self->alert_emitted = false;
    if (self->alert != NULL && self->alert->stop_alert != NULL) {
        (void)self->alert->stop_alert(self->alert->self);
    }
}

edge_status_t watch_timer_get_status(const watch_timer_app_t *self,
                                     watch_timer_status_t *out_status) {
    if (self == NULL || out_status == NULL) {
        return EDGE_EINVAL;
    }

    uint32_t now = get_now_ms(self);

    if (self->is_active) {
        out_status->distance_to_expiry_ms =
            (self->expiry_tick_ms >= now) ? (self->expiry_tick_ms - now) : 0u;
        out_status->expired = false;
        return EDGE_OK;
    }

    if (self->is_triggered) {
        out_status->distance_to_expiry_ms =
            (now >= self->expiry_tick_ms) ? (now - self->expiry_tick_ms) : 0u;
        out_status->expired = true;
        return EDGE_OK;
    }

    return EDGE_ENOENT;
}

bool watch_timer_is_running(const watch_timer_app_t *self) {
    return self && self->is_active;
}
