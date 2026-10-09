#ifndef APP_BUTTON_HANDLER_H
#define APP_BUTTON_HANDLER_H

#include "edge/module.h"
#include "ports.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_DEBOUNCE_MS 50u
#define BUTTON_LONG_PRESS_MS 2500u

typedef struct button_handler {
    edge_module_t module;
    const button_input_if_t *input;
    bool raw_pressed;
    bool debounced_pressed;
    uint32_t press_duration_ms;
    bool long_press_emitted;
    watch_button_event_t last_event;
    uint32_t poll_count;
} button_handler_t;

void button_handler_construct(button_handler_t *self, uint32_t module_id, uint32_t priority,
                              const button_input_if_t *input);
edge_status_t button_handler_init(button_handler_t *self);
edge_status_t button_handler_deinit(button_handler_t *self);
edge_module_t *button_handler_module(button_handler_t *self);

watch_button_event_t button_handler_get_event(button_handler_t *self);
edge_status_t button_handler_update(button_handler_t *self, bool is_pressed, uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif
