#ifndef APP_BUTTON_HANDLER_PORTS_H
#define APP_BUTTON_HANDLER_PORTS_H

#include "edge/module.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum watch_button_event {
    WATCH_BUTTON_NONE = 0,
    WATCH_BUTTON_SHORT_PRESS = 1,
    WATCH_BUTTON_LONG_PRESS = 2,
} watch_button_event_t;

typedef struct button_input_if {
    edge_status_t (*read_button_pressed)(void *self, bool *is_pressed);
    void *self;
} button_input_if_t;

#ifdef __cplusplus
}
#endif

#endif
