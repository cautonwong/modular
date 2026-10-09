#ifndef APP_TOUCH_GESTURE_H
#define APP_TOUCH_GESTURE_H

#include "edge/module.h"
#include "ports.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct touch_gesture {
    edge_module_t module;
    const touch_input_if_t *input;
    watch_gesture_type_t last_gesture;
    uint16_t last_x;
    uint16_t last_y;
    bool touching;
    bool gesture_released;
    uint32_t poll_count;
} touch_gesture_t;

void touch_gesture_construct(touch_gesture_t *self, uint32_t module_id, uint32_t priority,
                             const touch_input_if_t *input);
edge_status_t touch_gesture_init(touch_gesture_t *self);
edge_status_t touch_gesture_deinit(touch_gesture_t *self);
edge_module_t *touch_gesture_module(touch_gesture_t *self);

watch_gesture_type_t touch_gesture_get(touch_gesture_t *self);
edge_status_t touch_gesture_process_raw(touch_gesture_t *self, const touch_raw_info_t *info);

#ifdef __cplusplus
}
#endif

#endif
