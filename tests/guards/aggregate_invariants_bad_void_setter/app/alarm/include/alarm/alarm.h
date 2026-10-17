#ifndef DEMO_ALARM_H
#define DEMO_ALARM_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct alarm_settings {
    uint8_t hour;
    uint8_t minute;
} alarm_settings_t;

bool alarm_validate_settings(const alarm_settings_t *settings);

edge_status_t alarm_get_settings(const void *self, alarm_settings_t *out_settings);

/* Violation: void return ignores invalid time inputs */
void alarm_set_time(void *self, uint8_t hour, uint8_t minute);

#endif
