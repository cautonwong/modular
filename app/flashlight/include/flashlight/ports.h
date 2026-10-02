#ifndef APP_FLASHLIGHT_PORTS_H
#define APP_FLASHLIGHT_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct flashlight_display_if {
    void *self;
    edge_status_t (*set_brightness)(void *self, uint8_t percent);
    edge_status_t (*set_screen_color)(void *self, bool is_white);
} flashlight_display_if_t;

typedef struct flashlight_system_if {
    void *self;
    edge_status_t (*set_wake_lock)(void *self, bool locked);
} flashlight_system_if_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_FLASHLIGHT_PORTS_H */
