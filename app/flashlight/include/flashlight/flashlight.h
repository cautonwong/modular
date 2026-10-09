#ifndef APP_FLASHLIGHT_H
#define APP_FLASHLIGHT_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "flashlight/ports.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum flashlight_level {
    FLASHLIGHT_LEVEL_LOW = 0,
    FLASHLIGHT_LEVEL_MEDIUM = 1,
    FLASHLIGHT_LEVEL_HIGH = 2,
} flashlight_level_t;

typedef struct flashlight_app {
    edge_module_t module;
    const flashlight_display_if_t *display_port;
    const flashlight_system_if_t *sys_port;

    bool is_on;
    flashlight_level_t level;
    uint8_t previous_brightness;
} flashlight_app_t;

void flashlight_construct(flashlight_app_t *self, uint32_t module_id, uint32_t priority,
                          const flashlight_display_if_t *display,
                          const flashlight_system_if_t *sys);

edge_status_t flashlight_init(flashlight_app_t *self);
edge_status_t flashlight_shutdown(flashlight_app_t *self);

void flashlight_open(flashlight_app_t *self, uint8_t current_brightness);
void flashlight_close(flashlight_app_t *self);
void flashlight_toggle(flashlight_app_t *self);
void flashlight_swipe_left(flashlight_app_t *self);
void flashlight_swipe_right(flashlight_app_t *self);
flashlight_level_t flashlight_get_level(const flashlight_app_t *self);
bool flashlight_is_on(const flashlight_app_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_FLASHLIGHT_H */
