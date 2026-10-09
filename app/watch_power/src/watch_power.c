#include "watch_power/watch_power.h"
#include <stddef.h>

static void apply_hardware_power_state(watch_power_t *self) {
    if (self->display == NULL) {
        return;
    }
    switch (self->state) {
    case WATCH_POWER_AWAKE:
        if (self->display->sleep != NULL) {
            (void)self->display->sleep(self->display->self, false);
        }
        if (self->display->set_brightness != NULL) {
            (void)self->display->set_brightness(self->display->self, self->user_brightness_percent);
        }
        break;
    case WATCH_POWER_DIMMED:
        if (self->display->set_brightness != NULL) {
            const uint8_t dim_val =
                (self->user_brightness_percent > 10u) ? 10u : self->user_brightness_percent;
            (void)self->display->set_brightness(self->display->self, dim_val);
        }
        break;
    case WATCH_POWER_SLEEPING:
        if (self->display->set_brightness != NULL) {
            (void)self->display->set_brightness(self->display->self, 0u);
        }
        if (self->display->sleep != NULL) {
            (void)self->display->sleep(self->display->self, true);
        }
        break;
    case WATCH_POWER_CHARGING:
        if (self->display->sleep != NULL) {
            (void)self->display->sleep(self->display->self, false);
        }
        if (self->display->set_brightness != NULL) {
            (void)self->display->set_brightness(self->display->self, 50u);
        }
        break;
    }
}

static edge_status_t watch_power_poll(edge_module_t *module) {
    watch_power_t *self = (watch_power_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->poll_count++;

    if (self->battery != NULL && self->battery->read_status != NULL) {
        uint16_t mv = 0;
        uint8_t pct = 0;
        bool is_chg = false;
        bool is_pwr = false;
        if (self->battery->read_status(self->battery->self, &mv, &pct, &is_chg, &is_pwr) ==
            EDGE_OK) {
            if (is_pwr && !self->is_power_present) {
                // Charger newly plugged in -> wake into CHARGING / AWAKE
                self->state = WATCH_POWER_CHARGING;
                self->inactivity_timer_ms = 0u;
                apply_hardware_power_state(self);
            }
            self->is_charging = is_chg;
            self->is_power_present = is_pwr;
        }
    }

    return watch_power_update(self, 10u);
}

static edge_status_t watch_power_on_event(edge_module_t *module, const edge_event_t *event) {
    watch_power_t *self = (watch_power_t *)edge_module_data(module);
    if (self == NULL || event == NULL) {
        return EDGE_EINVAL;
    }
    // Any touch, button, or wrist wake event resets inactivity and wakes display
    watch_power_reset_inactivity(self);
    return EDGE_OK;
}

static edge_status_t watch_power_power_off(edge_module_t *module) {
    watch_power_t *self = (watch_power_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    watch_power_go_to_sleep(self);
    return EDGE_OK;
}

void watch_power_construct(watch_power_t *self, uint32_t module_id, uint32_t priority,
                           const watch_power_display_if_t *display,
                           const watch_power_battery_if_t *battery) {
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
        .poll = watch_power_poll,
        .on_event = watch_power_on_event,
        .power_off = watch_power_power_off,
        .private_data = self,
    };
    self->display = display;
    self->battery = battery;
    self->state = WATCH_POWER_AWAKE;
    self->inactivity_timer_ms = 0u;
    self->user_brightness_percent = 100u;
    self->is_charging = false;
    self->is_power_present = false;
    self->poll_count = 0u;
    self->wake_lock_count = 0u;
}

edge_status_t watch_power_init(watch_power_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->state = WATCH_POWER_AWAKE;
    self->inactivity_timer_ms = 0u;
    self->user_brightness_percent = 100u;
    self->is_charging = false;
    self->is_power_present = false;
    self->poll_count = 0u;
    self->wake_lock_count = 0u;
    apply_hardware_power_state(self);
    return EDGE_OK;
}

edge_status_t watch_power_deinit(watch_power_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->display = NULL;
    self->battery = NULL;
    return EDGE_OK;
}

edge_module_t *watch_power_module(watch_power_t *self) {
    if (self == NULL) {
        return NULL;
    }
    return &self->module;
}

void watch_power_reset_inactivity(watch_power_t *self) {
    if (self == NULL) {
        return;
    }
    self->inactivity_timer_ms = 0u;
    if (self->state != WATCH_POWER_AWAKE) {
        self->state = WATCH_POWER_AWAKE;
        apply_hardware_power_state(self);
    }
}

void watch_power_wake_up(watch_power_t *self) {
    watch_power_reset_inactivity(self);
}

void watch_power_go_to_sleep(watch_power_t *self) {
    if (self == NULL) {
        return;
    }
    self->state = WATCH_POWER_SLEEPING;
    self->inactivity_timer_ms = 0u;
    apply_hardware_power_state(self);
}

void watch_power_acquire_wake_lock(watch_power_t *self) {
    if (self == NULL) {
        return;
    }
    self->wake_lock_count++;
    watch_power_reset_inactivity(self);
}

void watch_power_release_wake_lock(watch_power_t *self) {
    if (self == NULL) {
        return;
    }
    if (self->wake_lock_count > 0u) {
        self->wake_lock_count--;
    }
}

bool watch_power_is_wake_locked(const watch_power_t *self) {
    return self && (self->wake_lock_count > 0u);
}

edge_status_t watch_power_update(watch_power_t *self, uint32_t delta_ms) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }

    if (self->wake_lock_count > 0u) {
        // While wake locked, prevent dimming or sleeping
        self->inactivity_timer_ms = 0u;
        if (self->state != WATCH_POWER_AWAKE && self->state != WATCH_POWER_CHARGING) {
            self->state = WATCH_POWER_AWAKE;
            apply_hardware_power_state(self);
        }
        return EDGE_OK;
    }

    if (self->state == WATCH_POWER_AWAKE) {
        self->inactivity_timer_ms += delta_ms;
        if (self->inactivity_timer_ms >= WATCH_INACTIVITY_DIM_MS) {
            self->state = WATCH_POWER_DIMMED;
            self->inactivity_timer_ms = 0u;
            apply_hardware_power_state(self);
        }
    } else if (self->state == WATCH_POWER_DIMMED) {
        self->inactivity_timer_ms += delta_ms;
        if (self->inactivity_timer_ms >= WATCH_DIM_TO_SLEEP_MS) {
            self->state = WATCH_POWER_SLEEPING;
            self->inactivity_timer_ms = 0u;
            apply_hardware_power_state(self);
        }
    }

    return EDGE_OK;
}
