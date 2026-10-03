#include "watch_ui/watch_ui.h"
#include "edge/events.h"

static edge_status_t watch_ui_poll(edge_module_t *module) {
    watch_ui_t *self = (watch_ui_t *)module->private_data;
    if (self != NULL && self->screen_on) {
        return watch_ui_refresh(self);
    }
    return EDGE_OK;
}

static edge_status_t watch_ui_on_event(edge_module_t *module, const edge_event_t *event) {
    watch_ui_t *self = (watch_ui_t *)module->private_data;
    if (self == NULL || event == NULL) {
        return EDGE_EINVAL;
    }

    switch (event->id) {
    case EDGE_EVT_WATCH_TOUCH:
        return watch_ui_on_touch_gesture(self, (watch_ui_gesture_t)event->arg0);

    case EDGE_EVT_WATCH_BUTTON:
        return watch_ui_on_button_event(self, (watch_ui_button_action_t)event->arg0);

    case EDGE_EVT_WATCH_WRIST_WAKE:
        self->screen_on = true;
        if (self->display != NULL && self->display->set_power_mode != NULL) {
            (void)self->display->set_power_mode(self->display->self, true);
        }
        return watch_ui_refresh(self);

    case EDGE_EVT_WATCH_TIME_TICK:
    case EDGE_EVT_WATCH_WEATHER_UPDATED:
    case EDGE_EVT_WATCH_MUSIC_UPDATED:
    case EDGE_EVT_WATCH_NAV_UPDATED:
    case EDGE_EVT_WATCH_NOTIF_NEW:
        if (self->screen_on) {
            return watch_ui_refresh(self);
        }
        return EDGE_OK;

    default:
        return EDGE_OK;
    }
}

static edge_status_t watch_ui_power_off(edge_module_t *module) {
    watch_ui_t *self = (watch_ui_t *)module->private_data;
    if (self != NULL) {
        self->screen_on = false;
        if (self->display != NULL && self->display->set_power_mode != NULL) {
            (void)self->display->set_power_mode(self->display->self, false);
        }
    }
    return EDGE_OK;
}

void watch_ui_construct(watch_ui_t *self, uint32_t module_id, uint32_t priority,
                        const watch_ui_display_port_t *display,
                        const watch_ui_status_port_t *status_port,
                        const edge_event_sink_t *event_sink) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->module.module_id = module_id;
    self->module.priority = priority;
    self->module.period = 500u;
    self->module.budget = 50u;
    self->module.next_due = 0u;
    self->module.poll = watch_ui_poll;
    self->module.on_event = watch_ui_on_event;
    self->module.power_off = watch_ui_power_off;
    self->module.private_data = self;

    self->display = display;
    self->status_port = status_port;
    self->event_sink = event_sink;

    self->current_screen = WATCH_SCREEN_WATCHFACE;
    self->watchface_style = WATCHFACE_STYLE_DIGITAL;
    self->stack_ptr = 0;
    self->launcher_page = 0;
    self->screen_on = true;
    self->refresh_count = 0;
}

edge_status_t watch_ui_init(watch_ui_t *self, const watch_ui_display_port_t *display,
                            const watch_ui_status_port_t *status_port,
                            const edge_event_sink_t *event_sink) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    watch_ui_construct(self, 0x1B00u, 10u, display, status_port, event_sink);
    return EDGE_OK;
}

watch_screen_id_t watch_ui_get_current_screen(const watch_ui_t *self) {
    return self != NULL ? self->current_screen : WATCH_SCREEN_WATCHFACE;
}

void watch_ui_set_watchface_style(watch_ui_t *self, watchface_style_t style) {
    if (self != NULL) {
        self->watchface_style = style;
    }
}

watchface_style_t watch_ui_get_watchface_style(const watch_ui_t *self) {
    return self != NULL ? self->watchface_style : WATCHFACE_STYLE_DIGITAL;
}

edge_status_t watch_ui_load_screen(watch_ui_t *self, watch_screen_id_t screen) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }

    /* If transitioning to watchface, reset stack */
    if (screen == WATCH_SCREEN_WATCHFACE) {
        self->stack_ptr = 0;
    } else if (self->current_screen == WATCH_SCREEN_WATCHFACE) {
        self->stack_ptr = 0;
        self->return_stack[self->stack_ptr++] = WATCH_SCREEN_WATCHFACE;
    } else if (self->current_screen != screen) {
        if (self->stack_ptr < WATCH_UI_STACK_DEPTH) {
            self->return_stack[self->stack_ptr++] = self->current_screen;
        }
    }

    self->current_screen = screen;
    return watch_ui_refresh(self);
}

edge_status_t watch_ui_return_previous(watch_ui_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }

    if (self->stack_ptr > 0) {
        self->current_screen = self->return_stack[--self->stack_ptr];
    } else {
        self->current_screen = WATCH_SCREEN_WATCHFACE;
    }

    return watch_ui_refresh(self);
}

edge_status_t watch_ui_on_touch_gesture(watch_ui_t *self, watch_ui_gesture_t gesture) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }

    if (self->current_screen == WATCH_SCREEN_WATCHFACE) {
        switch (gesture) {
        case WATCH_UI_GESTURE_SWIPE_UP:
            return watch_ui_load_screen(self, WATCH_SCREEN_NOTIFICATIONS);
        case WATCH_UI_GESTURE_SWIPE_DOWN:
            return watch_ui_load_screen(self, WATCH_SCREEN_QUICK_SETTINGS);
        case WATCH_UI_GESTURE_SWIPE_RIGHT:
            self->launcher_page = 0;
            return watch_ui_load_screen(self, WATCH_SCREEN_APP_LAUNCHER);
        case WATCH_UI_GESTURE_SWIPE_LEFT:
            self->launcher_page = 0;
            return watch_ui_load_screen(self, WATCH_SCREEN_APP_LAUNCHER);
        case WATCH_UI_GESTURE_DOUBLE_TAP:
            self->screen_on = false;
            if (self->display != NULL && self->display->set_power_mode != NULL) {
                (void)self->display->set_power_mode(self->display->self, false);
            }
            return EDGE_OK;
        default:
            break;
        }
    } else if (self->current_screen == WATCH_SCREEN_APP_LAUNCHER) {
        switch (gesture) {
        case WATCH_UI_GESTURE_SWIPE_RIGHT:
            if (self->launcher_page > 0) {
                self->launcher_page--;
                return watch_ui_refresh(self);
            }
            return watch_ui_return_previous(self);
        case WATCH_UI_GESTURE_SWIPE_LEFT:
            if ((uint32_t)self->launcher_page + 1u < WATCH_UI_LAUNCHER_PAGES) {
                self->launcher_page++;
                return watch_ui_refresh(self);
            }
            break;
        default:
            break;
        }
    } else {
        /* Other apps: Swipe right returns to previous screen */
        if (gesture == WATCH_UI_GESTURE_SWIPE_RIGHT) {
            return watch_ui_return_previous(self);
        }
    }

    return EDGE_OK;
}

edge_status_t watch_ui_on_button_event(watch_ui_t *self, watch_ui_button_action_t event) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }

    switch (event) {
    case WATCH_UI_BUTTON_CLICK:
        return watch_ui_on_button_pressed(self);

    case WATCH_UI_BUTTON_DOUBLE_CLICK:
        if (self->current_screen != WATCH_SCREEN_NOTIFICATIONS &&
            self->current_screen != WATCH_SCREEN_NOTIF_PREVIEW) {
            return watch_ui_load_screen(self, WATCH_SCREEN_NOTIF_PREVIEW);
        }
        break;

    case WATCH_UI_BUTTON_LONG_PRESS:
        if (self->current_screen != WATCH_SCREEN_WATCHFACE) {
            self->stack_ptr = 0;
            return watch_ui_load_screen(self, WATCH_SCREEN_WATCHFACE);
        }
        break;

    case WATCH_UI_BUTTON_LONGER_PRESS:
        return watch_ui_load_screen(self, WATCH_SCREEN_SYSINFO);

    default:
        break;
    }

    return EDGE_OK;
}

edge_status_t watch_ui_on_button_pressed(watch_ui_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }

    if (self->current_screen != WATCH_SCREEN_WATCHFACE) {
        return watch_ui_return_previous(self);
    } else {
        /* On watchface: toggle screen state */
        self->screen_on = !self->screen_on;
        if (self->display != NULL && self->display->set_power_mode != NULL) {
            (void)self->display->set_power_mode(self->display->self, self->screen_on);
        }
    }

    return EDGE_OK;
}

edge_status_t watch_ui_refresh(watch_ui_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }

    self->refresh_count++;

    if (self->display != NULL && self->display->clear_screen != NULL) {
        /* Screen clear color depends on screen */
        uint16_t bg_color = 0x0000; /* Black default */
        if (self->current_screen == WATCH_SCREEN_FLASHLIGHT) {
            bg_color = 0xFFFF; /* White for flashlight */
        }
        (void)self->display->clear_screen(self->display->self, bg_color);
    }

    return EDGE_OK;
}
