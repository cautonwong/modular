#include "flashlight/flashlight.h"

static uint8_t level_to_percent(flashlight_level_t level) {
    switch (level) {
    case FLASHLIGHT_LEVEL_LOW:
        return 33u;
    case FLASHLIGHT_LEVEL_MEDIUM:
        return 66u;
    case FLASHLIGHT_LEVEL_HIGH:
    default:
        return 100u;
    }
}

static void apply_state(flashlight_app_t *self) {
    if (self == NULL || self->display_port == NULL) {
        return;
    }
    if (self->display_port->set_screen_color != NULL) {
        self->display_port->set_screen_color(self->display_port->self, self->is_on);
    }
    if (self->display_port->set_brightness != NULL) {
        uint8_t pct = self->is_on ? level_to_percent(self->level) : 33u;
        self->display_port->set_brightness(self->display_port->self, pct);
    }
}

static edge_status_t app_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t app_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    return EDGE_OK;
}

static edge_status_t app_power_off(edge_module_t *module) {
    flashlight_app_t *self = (flashlight_app_t *)module->private_data;
    if (self != NULL) {
        flashlight_close(self);
    }
    return EDGE_OK;
}

void flashlight_construct(flashlight_app_t *self, uint32_t module_id, uint32_t priority,
                          const flashlight_display_if_t *display,
                          const flashlight_system_if_t *sys) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->module.module_id = module_id;
    self->module.priority = priority;
    self->module.period = 100u;
    self->module.budget = 0u;
    self->module.next_due = 0u;
    self->module.poll = app_poll;
    self->module.on_event = app_on_event;
    self->module.power_off = app_power_off;
    self->module.private_data = self;

    self->display_port = display;
    self->sys_port = sys;
    self->level = FLASHLIGHT_LEVEL_HIGH;
}

edge_status_t flashlight_init(flashlight_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->is_on = false;
    self->level = FLASHLIGHT_LEVEL_HIGH;
    self->previous_brightness = 100u;
    return EDGE_OK;
}

edge_status_t flashlight_shutdown(flashlight_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    flashlight_close(self);
    return EDGE_OK;
}

void flashlight_open(flashlight_app_t *self, uint8_t current_brightness) {
    if (self == NULL) {
        return;
    }
    self->previous_brightness = current_brightness;
    self->is_on = false;
    if (self->sys_port != NULL && self->sys_port->set_wake_lock != NULL) {
        self->sys_port->set_wake_lock(self->sys_port->self, true);
    }
    apply_state(self);
}

void flashlight_close(flashlight_app_t *self) {
    if (self == NULL) {
        return;
    }
    self->is_on = false;
    if (self->display_port != NULL) {
        if (self->display_port->set_screen_color != NULL) {
            self->display_port->set_screen_color(self->display_port->self, false);
        }
        if (self->display_port->set_brightness != NULL) {
            self->display_port->set_brightness(self->display_port->self, self->previous_brightness);
        }
    }
    if (self->sys_port != NULL && self->sys_port->set_wake_lock != NULL) {
        self->sys_port->set_wake_lock(self->sys_port->self, false);
    }
}

void flashlight_toggle(flashlight_app_t *self) {
    if (self == NULL) {
        return;
    }
    self->is_on = !self->is_on;
    apply_state(self);
}

void flashlight_swipe_left(flashlight_app_t *self) {
    if (self == NULL) {
        return;
    }
    if (self->level == FLASHLIGHT_LEVEL_HIGH) {
        self->level = FLASHLIGHT_LEVEL_MEDIUM;
    } else if (self->level == FLASHLIGHT_LEVEL_MEDIUM) {
        self->level = FLASHLIGHT_LEVEL_LOW;
    }
    apply_state(self);
}

void flashlight_swipe_right(flashlight_app_t *self) {
    if (self == NULL) {
        return;
    }
    if (self->level == FLASHLIGHT_LEVEL_LOW) {
        self->level = FLASHLIGHT_LEVEL_MEDIUM;
    } else if (self->level == FLASHLIGHT_LEVEL_MEDIUM) {
        self->level = FLASHLIGHT_LEVEL_HIGH;
    }
    apply_state(self);
}

flashlight_level_t flashlight_get_level(const flashlight_app_t *self) {
    return self != NULL ? self->level : FLASHLIGHT_LEVEL_LOW;
}

bool flashlight_is_on(const flashlight_app_t *self) {
    return self != NULL && self->is_on;
}
