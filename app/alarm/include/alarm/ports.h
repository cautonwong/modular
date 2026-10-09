#ifndef APP_ALARM_PORTS_H
#define APP_ALARM_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum alarm_recurrence {
    ALARM_RECUR_NONE = 0,
    ALARM_RECUR_DAILY = 1,
    ALARM_RECUR_WEEKDAYS = 2,
} alarm_recurrence_t;

typedef struct alarm_settings {
    uint8_t version;
    uint8_t hours;   /* 0..23 */
    uint8_t minutes; /* 0..59 */
    alarm_recurrence_t recurrence;
    bool is_enabled;
} alarm_settings_t;

/* Consumer-defined port for persisting alarm configuration */
typedef struct alarm_storage_if {
    void *self;
    edge_status_t (*load)(void *self, alarm_settings_t *out_settings);
    edge_status_t (*save)(void *self, const alarm_settings_t *settings);
} alarm_storage_if_t;

/* Consumer-defined port for triggering haptic motor alert */
typedef struct alarm_alert_if {
    void *self;
    edge_status_t (*start_alert)(void *self);
    edge_status_t (*stop_alert)(void *self);
} alarm_alert_if_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_ALARM_PORTS_H */
