#include "touch_gesture/touch_gesture.h"
#include <stddef.h>

static watch_gesture_type_t convert_hardware_gesture(uint8_t hw_gesture) {
    switch (hw_gesture) {
    case 0x05: // SingleTap
        return WATCH_GESTURE_TAP;
    case 0x0B: // DoubleTap
        return WATCH_GESTURE_DOUBLE_TAP;
    case 0x0C: // LongPress
        return WATCH_GESTURE_LONG_TAP;
    case 0x01: // SlideDown
        return WATCH_GESTURE_SWIPE_DOWN;
    case 0x02: // SlideUp
        return WATCH_GESTURE_SWIPE_UP;
    case 0x03: // SlideLeft
        return WATCH_GESTURE_SWIPE_LEFT;
    case 0x04: // SlideRight
        return WATCH_GESTURE_SWIPE_RIGHT;
    default:
        return WATCH_GESTURE_NONE;
    }
}

static edge_status_t touch_gesture_poll(edge_module_t *module) {
    touch_gesture_t *self = (touch_gesture_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->poll_count++;

    if (self->input != NULL && self->input->read_touch != NULL) {
        touch_raw_info_t raw;
        if (self->input->read_touch(self->input->self, &raw) == EDGE_OK) {
            (void)touch_gesture_process_raw(self, &raw);
        }
    }
    return EDGE_OK;
}

static edge_status_t touch_gesture_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    return EDGE_OK;
}

static edge_status_t touch_gesture_power_off(edge_module_t *module) {
    touch_gesture_t *self = (touch_gesture_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    if (self->input != NULL && self->input->sleep != NULL) {
        (void)self->input->sleep(self->input->self, true);
    }
    return EDGE_OK;
}

void touch_gesture_construct(touch_gesture_t *self, uint32_t module_id, uint32_t priority,
                             const touch_input_if_t *input) {
    if (self == NULL) {
        return;
    }
    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 1u,
        .budget = 0u,
        .next_due = 0u,
        .poll = touch_gesture_poll,
        .on_event = touch_gesture_on_event,
        .power_off = touch_gesture_power_off,
        .private_data = self,
    };
    self->input = input;
    self->last_gesture = WATCH_GESTURE_NONE;
    self->last_x = 0u;
    self->last_y = 0u;
    self->touching = false;
    self->gesture_released = true;
    self->poll_count = 0u;
}

edge_status_t touch_gesture_init(touch_gesture_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->last_gesture = WATCH_GESTURE_NONE;
    self->last_x = 0u;
    self->last_y = 0u;
    self->touching = false;
    self->gesture_released = true;
    self->poll_count = 0u;
    return EDGE_OK;
}

edge_status_t touch_gesture_deinit(touch_gesture_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->input = NULL;
    return EDGE_OK;
}

edge_module_t *touch_gesture_module(touch_gesture_t *self) {
    if (self == NULL) {
        return NULL;
    }
    return &self->module;
}

watch_gesture_type_t touch_gesture_get(touch_gesture_t *self) {
    if (self == NULL) {
        return WATCH_GESTURE_NONE;
    }
    const watch_gesture_type_t g = self->last_gesture;
    self->last_gesture = WATCH_GESTURE_NONE;
    return g;
}

edge_status_t touch_gesture_process_raw(touch_gesture_t *self, const touch_raw_info_t *info) {
    if (self == NULL || info == NULL) {
        return EDGE_EINVAL;
    }
    if (!info->is_valid) {
        return EDGE_EINVAL;
    }

    if (info->hardware_gesture != 0u) {
        if (self->gesture_released) {
            const bool is_continuous =
                (info->hardware_gesture == 0x01u || info->hardware_gesture == 0x02u ||
                 info->hardware_gesture == 0x03u || info->hardware_gesture == 0x04u ||
                 info->hardware_gesture == 0x0Cu);

            if (is_continuous) {
                if (info->touching) {
                    self->last_gesture = convert_hardware_gesture(info->hardware_gesture);
                    self->gesture_released = false;
                }
            } else {
                self->last_gesture = convert_hardware_gesture(info->hardware_gesture);
            }
        }
    }

    if (!info->touching) {
        self->gesture_released = true;
    }

    self->last_x = info->x;
    self->last_y = info->y;
    self->touching = info->touching;

    return EDGE_OK;
}
