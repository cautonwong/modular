#ifndef APP_TOUCH_GESTURE_PORTS_H
#define APP_TOUCH_GESTURE_PORTS_H

#include "edge/module.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum watch_gesture_type {
    WATCH_GESTURE_NONE = 0,
    WATCH_GESTURE_TAP = 1,
    WATCH_GESTURE_DOUBLE_TAP = 2,
    WATCH_GESTURE_LONG_TAP = 3,
    WATCH_GESTURE_SWIPE_UP = 4,
    WATCH_GESTURE_SWIPE_DOWN = 5,
    WATCH_GESTURE_SWIPE_LEFT = 6,
    WATCH_GESTURE_SWIPE_RIGHT = 7,
} watch_gesture_type_t;

typedef struct touch_raw_info {
    uint16_t x;
    uint16_t y;
    bool touching;
    uint8_t hardware_gesture;
    bool is_valid;
} touch_raw_info_t;

typedef struct touch_input_if {
    edge_status_t (*read_touch)(void *self, touch_raw_info_t *out_info);
    edge_status_t (*sleep)(void *self, bool enable);
    void *self;
} touch_input_if_t;

#ifdef __cplusplus
}
#endif

#endif
