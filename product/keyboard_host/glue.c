#include "glue.h"

static edge_status_t glue_press_key(void *self, uint8_t keycode, uint8_t modifiers) {
    hid_report_builder_t *builder = (hid_report_builder_t *)self;
    if (modifiers != 0) {
        hid_report_add_modifiers(builder, modifiers);
    }
    return hid_report_press_keycode(builder, keycode);
}

static edge_status_t glue_release_key(void *self, uint8_t keycode, uint8_t modifiers) {
    hid_report_builder_t *builder = (hid_report_builder_t *)self;
    if (modifiers != 0) {
        hid_report_remove_modifiers(builder, modifiers);
    }
    return hid_report_release_keycode(builder, keycode);
}

static edge_status_t glue_press_consumer(void *self, uint16_t code) {
    hid_report_builder_t *builder = (hid_report_builder_t *)self;
    return hid_report_press_consumer(builder, code);
}

static edge_status_t glue_release_consumer(void *self, uint16_t code) {
    hid_report_builder_t *builder = (hid_report_builder_t *)self;
    return hid_report_release_consumer(builder, code);
}

static edge_status_t glue_press_mouse(void *self, uint8_t button) {
    hid_report_builder_t *builder = (hid_report_builder_t *)self;
    return hid_report_press_mouse_button(builder, button);
}

static edge_status_t glue_release_mouse(void *self, uint8_t button) {
    hid_report_builder_t *builder = (hid_report_builder_t *)self;
    return hid_report_release_mouse_button(builder, button);
}

void product_keyboard_make_behavior_hid(zmk_behavior_hid_if_t *out, hid_report_builder_t *builder) {
    if (out == NULL || builder == NULL) {
        return;
    }
    *out = (zmk_behavior_hid_if_t){
        .self = builder,
        .press_key = glue_press_key,
        .release_key = glue_release_key,
        .press_consumer_key = glue_press_consumer,
        .release_consumer_key = glue_release_consumer,
        .press_mouse_button = glue_press_mouse,
        .release_mouse_button = glue_release_mouse,
    };
}

static edge_status_t glue_invoke_binding(void *self, uint16_t behavior_id, uint32_t param1,
                                         uint32_t param2, bool pressed, uint32_t timestamp_ms) {
    zmk_behavior_app_t *behavior = (zmk_behavior_app_t *)self;
    return zmk_behavior_invoke(behavior, behavior_id, param1, param2, pressed, timestamp_ms);
}

void product_keyboard_make_keymap_behavior(zmk_keymap_behavior_if_t *out,
                                           zmk_behavior_app_t *behavior) {
    if (out == NULL || behavior == NULL) {
        return;
    }
    *out = (zmk_keymap_behavior_if_t){
        .self = behavior,
        .invoke_binding = glue_invoke_binding,
    };
}

static edge_status_t glue_post_position(void *self, uint32_t position, bool pressed,
                                        uint32_t timestamp_ms) {
    zmk_keymap_app_t *keymap = (zmk_keymap_app_t *)self;
    return zmk_keymap_on_position_state_change(keymap, position, pressed, timestamp_ms);
}

void product_keyboard_make_matrix_sink(zmk_matrix_event_sink_if_t *out, zmk_keymap_app_t *keymap) {
    if (out == NULL || keymap == NULL) {
        return;
    }
    *out = (zmk_matrix_event_sink_if_t){
        .self = keymap,
        .post_position_event = glue_post_position,
    };
}
