#include "button_handler/button_handler.h"
#include <stddef.h>

static edge_status_t button_handler_poll(edge_module_t *module) {
    button_handler_t *self = (button_handler_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->poll_count++;

    if (self->input != NULL && self->input->read_button_pressed != NULL) {
        bool is_pressed = false;
        if (self->input->read_button_pressed(self->input->self, &is_pressed) == EDGE_OK) {
            // Assume 10ms nominal tick period if stepping via runner
            (void)button_handler_update(self, is_pressed, 10u);
        }
    }
    return EDGE_OK;
}

static edge_status_t button_handler_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    return EDGE_OK;
}

static edge_status_t button_handler_power_off(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

void button_handler_construct(button_handler_t *self, uint32_t module_id, uint32_t priority,
                              const button_input_if_t *input) {
    if (self == NULL) {
        return;
    }
    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 1u,
        .budget = 0u,
        .next_due = 0u,
        .poll = button_handler_poll,
        .on_event = button_handler_on_event,
        .power_off = button_handler_power_off,
        .private_data = self,
    };
    self->input = input;
    self->raw_pressed = false;
    self->debounced_pressed = false;
    self->press_duration_ms = 0u;
    self->long_press_emitted = false;
    self->last_event = WATCH_BUTTON_NONE;
    self->poll_count = 0u;
}

edge_status_t button_handler_init(button_handler_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->raw_pressed = false;
    self->debounced_pressed = false;
    self->press_duration_ms = 0u;
    self->long_press_emitted = false;
    self->last_event = WATCH_BUTTON_NONE;
    self->poll_count = 0u;
    return EDGE_OK;
}

edge_status_t button_handler_deinit(button_handler_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->input = NULL;
    return EDGE_OK;
}

edge_module_t *button_handler_module(button_handler_t *self) {
    if (self == NULL) {
        return NULL;
    }
    return &self->module;
}

watch_button_event_t button_handler_get_event(button_handler_t *self) {
    if (self == NULL) {
        return WATCH_BUTTON_NONE;
    }
    const watch_button_event_t ev = self->last_event;
    self->last_event = WATCH_BUTTON_NONE;
    return ev;
}

edge_status_t button_handler_update(button_handler_t *self, bool is_pressed, uint32_t delta_ms) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }

    self->raw_pressed = is_pressed;

    if (is_pressed) {
        self->press_duration_ms += delta_ms;

        // Long press detection
        if (self->press_duration_ms >= BUTTON_LONG_PRESS_MS && !self->long_press_emitted) {
            self->long_press_emitted = true;
            self->last_event = WATCH_BUTTON_LONG_PRESS;
        }
    } else {
        // Button released
        if (self->press_duration_ms >= BUTTON_DEBOUNCE_MS && !self->long_press_emitted) {
            // Short press confirmed upon release
            self->last_event = WATCH_BUTTON_SHORT_PRESS;
        }

        self->press_duration_ms = 0u;
        self->long_press_emitted = false;
    }

    return EDGE_OK;
}
