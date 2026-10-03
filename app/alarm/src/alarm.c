#include "alarm/alarm.h"
#include "edge/events.h"

static edge_status_t alarm_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t alarm_on_event(edge_module_t *module, const edge_event_t *event) {
    alarm_app_t *self = (alarm_app_t *)edge_module_data(module);
    if (self == NULL || event == NULL) {
        return EDGE_EINVAL;
    }

    if (event->id == EDGE_EVT_WATCH_TIME_TICK) {
        /* event->arg0: (hour << 8) | minute; event->arg1: (day_of_week << 8) | day */
        uint8_t hour = (uint8_t)((event->arg0 >> 8u) & 0xFFu);
        uint8_t minute = (uint8_t)(event->arg0 & 0xFFu);
        uint8_t dow = (uint8_t)((event->arg1 >> 8u) & 0xFFu);
        uint8_t day = (uint8_t)(event->arg1 & 0xFFu);

        (void)alarm_check_trigger(self, hour, minute, dow, day);
    }

    return EDGE_OK;
}

static edge_status_t alarm_power_off(edge_module_t *module) {
    alarm_app_t *self = (alarm_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return alarm_shutdown(self);
}

void alarm_construct(alarm_app_t *self, uint32_t module_id, uint32_t priority,
                     const alarm_storage_if_t *storage, const alarm_alert_if_t *alert) {
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
        .poll = alarm_poll,
        .on_event = alarm_on_event,
        .power_off = alarm_power_off,
        .private_data = self,
    };

    self->storage = storage;
    self->alert = alert;

    self->settings = (alarm_settings_t){
        .version = ALARM_FORMAT_VERSION,
        .hours = 7u,
        .minutes = 0u,
        .recurrence = ALARM_RECUR_NONE,
        .is_enabled = false,
    };

    self->is_alerting = false;
    self->settings_changed = false;
    self->last_triggered_day = 0xFFu;
    self->last_triggered_minute = 0xFFu;
    self->is_snoozing = false;
    self->snooze_target_hour = 0u;
    self->snooze_target_minute = 0u;
}

edge_status_t alarm_init(alarm_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }

    if (self->storage != NULL && self->storage->load != NULL) {
        alarm_settings_t loaded;
        if (self->storage->load(self->storage->self, &loaded) == EDGE_OK) {
            if (loaded.version == ALARM_FORMAT_VERSION) {
                self->settings = loaded;
            }
        }
    }

    return EDGE_OK;
}

edge_status_t alarm_shutdown(alarm_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    alarm_stop_alerting(self);
    return alarm_save(self);
}

edge_status_t alarm_set_time(alarm_app_t *self, uint8_t hour, uint8_t minute) {
    if (self == NULL || hour > 23u || minute > 59u) {
        return EDGE_EINVAL;
    }
    if (self->settings.hours != hour || self->settings.minutes != minute) {
        self->settings.hours = hour;
        self->settings.minutes = minute;
        self->settings_changed = true;
    }
    return EDGE_OK;
}

edge_status_t alarm_set_recurrence(alarm_app_t *self, alarm_recurrence_t recurrence) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    if (self->settings.recurrence != recurrence) {
        self->settings.recurrence = recurrence;
        self->settings_changed = true;
    }
    return EDGE_OK;
}

edge_status_t alarm_enable(alarm_app_t *self, bool enable) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    if (self->settings.is_enabled != enable) {
        self->settings.is_enabled = enable;
        self->settings_changed = true;
    }
    if (!enable) {
        self->is_snoozing = false;
    }
    return EDGE_OK;
}

edge_status_t alarm_save(alarm_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    if (self->settings_changed && self->storage != NULL && self->storage->save != NULL) {
        edge_status_t st = self->storage->save(self->storage->self, &self->settings);
        if (st == EDGE_OK) {
            self->settings_changed = false;
        }
        return st;
    }
    return EDGE_OK;
}

void alarm_set_off_now(alarm_app_t *self) {
    if (self == NULL) {
        return;
    }
    self->is_alerting = true;
    if (self->alert != NULL && self->alert->start_alert != NULL) {
        (void)self->alert->start_alert(self->alert->self);
    }
}

void alarm_stop_alerting(alarm_app_t *self) {
    if (self == NULL) {
        return;
    }
    self->is_alerting = false;
    self->is_snoozing = false;

    if (self->alert != NULL && self->alert->stop_alert != NULL) {
        (void)self->alert->stop_alert(self->alert->self);
    }

    if (self->settings.recurrence == ALARM_RECUR_NONE) {
        self->settings.is_enabled = false;
        self->settings_changed = true;
        (void)alarm_save(self);
    }
}

void alarm_snooze(alarm_app_t *self, uint8_t snooze_minutes) {
    if (self == NULL) {
        return;
    }
    self->is_alerting = false;
    if (self->alert != NULL && self->alert->stop_alert != NULL) {
        (void)self->alert->stop_alert(self->alert->self);
    }

    uint32_t total_min = (uint32_t)self->settings.minutes + snooze_minutes;
    self->snooze_target_minute = (uint8_t)(total_min % 60u);
    self->snooze_target_hour =
        (uint8_t)(((uint32_t)self->settings.hours + (total_min / 60u)) % 24u);
    self->is_snoozing = true;
}

bool alarm_check_trigger(alarm_app_t *self, uint8_t current_hour, uint8_t current_minute,
                         uint8_t current_day_of_week, uint8_t current_day) {
    if (self == NULL) {
        return false;
    }

    if (self->is_snoozing) {
        if (current_hour == self->snooze_target_hour &&
            current_minute == self->snooze_target_minute) {
            self->is_snoozing = false;
            alarm_set_off_now(self);
            return true;
        }
        return false;
    }

    if (!self->settings.is_enabled) {
        return false;
    }

    /* Check Weekdays recurrence: 1=Mon .. 5=Fri; 6=Sat, 7=Sun */
    if (self->settings.recurrence == ALARM_RECUR_WEEKDAYS) {
        if (current_day_of_week >= 6u) {
            return false;
        }
    }

    if (current_hour == self->settings.hours && current_minute == self->settings.minutes) {
        if (self->last_triggered_day == current_day &&
            self->last_triggered_minute == current_minute) {
            return false; /* already triggered this minute */
        }
        self->last_triggered_day = current_day;
        self->last_triggered_minute = current_minute;
        alarm_set_off_now(self);
        return true;
    }

    return false;
}

uint32_t alarm_seconds_to_alarm(const alarm_app_t *self, uint8_t current_hour,
                                uint8_t current_minute, uint8_t current_second,
                                uint8_t current_day_of_week) {
    if (self == NULL || !self->settings.is_enabled) {
        return 0u;
    }

    int32_t now_secs = (int32_t)current_hour * 3600 + (int32_t)current_minute * 60 + current_second;
    int32_t alarm_secs =
        (int32_t)self->settings.hours * 3600 + (int32_t)self->settings.minutes * 60;

    int32_t diff = alarm_secs - now_secs;
    uint32_t days_shift = 0u;

    if (diff <= 0) {
        diff += 86400; /* Next day */
        days_shift = 1u;
    }

    if (self->settings.recurrence == ALARM_RECUR_WEEKDAYS) {
        uint8_t target_dow = (uint8_t)(((current_day_of_week - 1u + days_shift) % 7u) + 1u);
        if (target_dow == 6u) { /* Saturday -> shift 2 days to Monday */
            diff += 2 * 86400;
        } else if (target_dow == 7u) { /* Sunday -> shift 1 day to Monday */
            diff += 86400;
        }
    }

    return (uint32_t)diff;
}
