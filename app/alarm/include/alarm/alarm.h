#ifndef APP_ALARM_H
#define APP_ALARM_H

#include "alarm/ports.h"
#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALARM_FORMAT_VERSION 1u

typedef struct alarm_app {
    edge_module_t module;
    const alarm_storage_if_t *storage;
    const alarm_alert_if_t *alert;

    alarm_settings_t settings;
    bool is_alerting;
    bool settings_changed;

    /* Internal trigger tracking to prevent re-firing in the same minute */
    uint8_t last_triggered_day;
    uint8_t last_triggered_minute;

    /* Snooze tracking */
    bool is_snoozing;
    uint8_t snooze_target_hour;
    uint8_t snooze_target_minute;
} alarm_app_t;

void alarm_construct(alarm_app_t *self, uint32_t module_id, uint32_t priority,
                     const alarm_storage_if_t *storage, const alarm_alert_if_t *alert);

edge_status_t alarm_init(alarm_app_t *self);
edge_status_t alarm_shutdown(alarm_app_t *self);

edge_status_t alarm_set_time(alarm_app_t *self, uint8_t hour, uint8_t minute);
edge_status_t alarm_set_recurrence(alarm_app_t *self, alarm_recurrence_t recurrence);
edge_status_t alarm_enable(alarm_app_t *self, bool enable);

edge_status_t alarm_save(alarm_app_t *self);
void alarm_set_off_now(alarm_app_t *self);
void alarm_stop_alerting(alarm_app_t *self);
void alarm_snooze(alarm_app_t *self, uint8_t snooze_minutes);

/**
 * Check if the alarm should trigger based on current wall time.
 *
 * @param current_hour 0..23
 * @param current_minute 0..59
 * @param current_day_of_week 1=Mon .. 7=Sun
 * @param current_day 1..31
 * @return true if alarm triggered, false otherwise.
 */
bool alarm_check_trigger(alarm_app_t *self, uint8_t current_hour, uint8_t current_minute,
                         uint8_t current_day_of_week, uint8_t current_day);

uint32_t alarm_seconds_to_alarm(const alarm_app_t *self, uint8_t current_hour,
                                uint8_t current_minute, uint8_t current_second,
                                uint8_t current_day_of_week);

#ifdef __cplusplus
}
#endif

#endif /* APP_ALARM_H */
