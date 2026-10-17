#ifndef DEMO_ALARM_H
#define DEMO_ALARM_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct alarm_settings {
    uint8_t hour;
    uint8_t minute;
} alarm_settings_t;

/* Missing validator function alarm_validate_* */

edge_status_t alarm_get_settings(const void *self, alarm_settings_t *out_settings);

edge_status_t alarm_set_time(void *self, uint8_t hour, uint8_t minute);

#endif
