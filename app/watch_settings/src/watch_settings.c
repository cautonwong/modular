#include "watch_settings/watch_settings.h"

static edge_status_t watch_settings_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t watch_settings_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    return EDGE_OK;
}

static edge_status_t watch_settings_power_off(edge_module_t *module) {
    watch_settings_app_t *self = (watch_settings_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return watch_settings_shutdown(self);
}

void watch_settings_construct(watch_settings_app_t *self, uint32_t module_id, uint32_t priority,
                              const watch_settings_store_if_t *store) {
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
        .poll = watch_settings_poll,
        .on_event = watch_settings_on_event,
        .power_off = watch_settings_power_off,
        .private_data = self,
    };

    self->store = store;
    self->data = (watch_settings_data_t){
        .version = SETTINGS_FORMAT_VERSION,
        .steps_goal = 10000u,
        .screen_timeout_ms = 15000u,
        .always_on_display = false,
        .clock_format = CLOCK_FORMAT_24H,
        .weather_format = WEATHER_FORMAT_METRIC,
        .notification_mode = NOTIF_ON,
        .watch_face = WATCH_FACE_DIGITAL,
        .chimes_mode = CHIMES_NONE,
        .pts =
            {
                .color_time = 0u,
                .color_bar = 0u,
                .color_bg = 0u,
                .gauge_style = PTS_GAUGE_FULL,
                .weather_enable = true,
            },
        .pride_flag = PRIDE_FLAG_GAY,
        .infineat =
            {
                .show_side_cover = true,
                .color_index = 0,
            },
        .wake_mode_flags = 0u,
        .shake_wake_threshold = 150u,
        .brightness_level = 2u, /* Medium */
        .heart_rate_background_period_s = 0u,
    };
    self->is_dirty = false;
    self->ble_radio_enabled = true;
}

edge_status_t watch_settings_init(watch_settings_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }

    if (self->store != NULL && self->store->load != NULL) {
        watch_settings_data_t loaded;
        if (self->store->load(self->store->self, &loaded) == EDGE_OK) {
            if (loaded.version == SETTINGS_FORMAT_VERSION) {
                self->data = loaded;
            }
        }
    }

    return EDGE_OK;
}

edge_status_t watch_settings_shutdown(watch_settings_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return watch_settings_save(self);
}

edge_status_t watch_settings_save(watch_settings_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    if (self->is_dirty && self->store != NULL && self->store->save != NULL) {
        edge_status_t st = self->store->save(self->store->self, &self->data);
        if (st == EDGE_OK) {
            self->is_dirty = false;
        }
        return st;
    }
    return EDGE_OK;
}

void watch_settings_set_watch_face(watch_settings_app_t *self, watch_face_type_t face) {
    if (self != NULL && self->data.watch_face != face) {
        self->data.watch_face = face;
        self->is_dirty = true;
    }
}

watch_face_type_t watch_settings_get_watch_face(const watch_settings_app_t *self) {
    return self ? self->data.watch_face : WATCH_FACE_DIGITAL;
}

void watch_settings_set_clock_format(watch_settings_app_t *self, clock_format_type_t format) {
    if (self != NULL && self->data.clock_format != format) {
        self->data.clock_format = format;
        self->is_dirty = true;
    }
}

clock_format_type_t watch_settings_get_clock_format(const watch_settings_app_t *self) {
    return self ? self->data.clock_format : CLOCK_FORMAT_24H;
}

void watch_settings_set_notification_mode(watch_settings_app_t *self, notification_mode_t mode) {
    if (self != NULL && self->data.notification_mode != mode) {
        self->data.notification_mode = mode;
        self->is_dirty = true;
    }
}

notification_mode_t watch_settings_get_notification_mode(const watch_settings_app_t *self) {
    return self ? self->data.notification_mode : NOTIF_ON;
}

void watch_settings_set_screen_timeout(watch_settings_app_t *self, uint32_t timeout_ms) {
    if (self != NULL && self->data.screen_timeout_ms != timeout_ms) {
        self->data.screen_timeout_ms = timeout_ms;
        self->is_dirty = true;
    }
}

uint32_t watch_settings_get_screen_timeout(const watch_settings_app_t *self) {
    return self ? self->data.screen_timeout_ms : 15000u;
}

void watch_settings_set_always_on_display(watch_settings_app_t *self, bool enable) {
    if (self != NULL && self->data.always_on_display != enable) {
        self->data.always_on_display = enable;
        self->is_dirty = true;
    }
}

bool watch_settings_get_always_on_display(const watch_settings_app_t *self) {
    return self && self->data.always_on_display && self->data.notification_mode != NOTIF_SLEEP;
}

void watch_settings_set_steps_goal(watch_settings_app_t *self, uint32_t goal) {
    if (self != NULL && self->data.steps_goal != goal) {
        self->data.steps_goal = goal;
        self->is_dirty = true;
    }
}

uint32_t watch_settings_get_steps_goal(const watch_settings_app_t *self) {
    return self ? self->data.steps_goal : 10000u;
}

void watch_settings_set_wake_mode(watch_settings_app_t *self, uint8_t wake_flag, bool enable) {
    if (self == NULL) {
        return;
    }
    if (enable) {
        self->data.wake_mode_flags |= wake_flag;
        if (wake_flag == WAKE_MODE_FLAG_SINGLE_TAP) {
            self->data.wake_mode_flags &= ~WAKE_MODE_FLAG_DOUBLE_TAP;
        } else if (wake_flag == WAKE_MODE_FLAG_DOUBLE_TAP) {
            self->data.wake_mode_flags &= ~WAKE_MODE_FLAG_SINGLE_TAP;
        }
    } else {
        self->data.wake_mode_flags &= ~wake_flag;
    }
    self->is_dirty = true;
}

bool watch_settings_is_wake_mode_enabled(const watch_settings_app_t *self, uint8_t wake_flag) {
    return self && ((self->data.wake_mode_flags & wake_flag) != 0u);
}

void watch_settings_set_brightness(watch_settings_app_t *self, uint8_t level) {
    if (self != NULL && self->data.brightness_level != level) {
        self->data.brightness_level = level;
        self->is_dirty = true;
    }
}

uint8_t watch_settings_get_brightness(const watch_settings_app_t *self) {
    return self ? self->data.brightness_level : 2u;
}

void watch_settings_set_weather_format(watch_settings_app_t *self, weather_format_type_t format) {
    if (self != NULL && self->data.weather_format != format) {
        self->data.weather_format = format;
        self->is_dirty = true;
    }
}

weather_format_type_t watch_settings_get_weather_format(const watch_settings_app_t *self) {
    return self ? self->data.weather_format : WEATHER_FORMAT_METRIC;
}

void watch_settings_set_chimes_mode(watch_settings_app_t *self, chimes_mode_t mode) {
    if (self != NULL && self->data.chimes_mode != mode) {
        self->data.chimes_mode = mode;
        self->is_dirty = true;
    }
}

chimes_mode_t watch_settings_get_chimes_mode(const watch_settings_app_t *self) {
    return self ? self->data.chimes_mode : CHIMES_NONE;
}

void watch_settings_set_pts_settings(watch_settings_app_t *self, const pts_settings_t *pts) {
    if (self != NULL && pts != NULL) {
        self->data.pts = *pts;
        self->is_dirty = true;
    }
}

pts_settings_t watch_settings_get_pts_settings(const watch_settings_app_t *self) {
    static const pts_settings_t default_pts = {0};
    return self ? self->data.pts : default_pts;
}

void watch_settings_set_pride_flag(watch_settings_app_t *self, pride_flag_type_t flag) {
    if (self != NULL && self->data.pride_flag != flag) {
        self->data.pride_flag = flag;
        self->is_dirty = true;
    }
}

pride_flag_type_t watch_settings_get_pride_flag(const watch_settings_app_t *self) {
    return self ? self->data.pride_flag : PRIDE_FLAG_GAY;
}

void watch_settings_set_infineat_settings(watch_settings_app_t *self,
                                          const infineat_settings_t *infineat) {
    if (self != NULL && infineat != NULL) {
        self->data.infineat = *infineat;
        self->is_dirty = true;
    }
}

infineat_settings_t watch_settings_get_infineat_settings(const watch_settings_app_t *self) {
    static const infineat_settings_t default_infineat = {.show_side_cover = true, .color_index = 0};
    return self ? self->data.infineat : default_infineat;
}

void watch_settings_set_shake_wake_threshold(watch_settings_app_t *self, uint16_t threshold) {
    if (self != NULL && self->data.shake_wake_threshold != threshold) {
        self->data.shake_wake_threshold = threshold;
        self->is_dirty = true;
    }
}

uint16_t watch_settings_get_shake_wake_threshold(const watch_settings_app_t *self) {
    return self ? self->data.shake_wake_threshold : 150u;
}

void watch_settings_set_ble_enabled(watch_settings_app_t *self, bool enabled) {
    if (self != NULL) {
        self->ble_radio_enabled = enabled;
    }
}

bool watch_settings_get_ble_enabled(const watch_settings_app_t *self) {
    return self ? self->ble_radio_enabled : true;
}
