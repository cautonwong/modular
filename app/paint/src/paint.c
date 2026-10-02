#include "paint/paint.h"

static const uint16_t s_palette[PAINT_COLOR_COUNT] = {
    0xF81Fu, /* Magenta */
    0x07E0u, /* Green */
    0xFFFFu, /* White */
    0xF800u, /* Red */
    0x07FFu, /* Cyan */
    0xFFE0u, /* Yellow */
    0x001Fu, /* Blue */
    0x0000u, /* Black */
};

static edge_status_t app_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t app_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    return EDGE_OK;
}

static edge_status_t app_power_off(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

void paint_construct(paint_app_t *self, uint32_t module_id, uint32_t priority,
                     const paint_display_if_t *display, const paint_motor_if_t *motor) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->module.module_id = module_id;
    self->module.priority = priority;
    self->module.period = 100u;
    self->module.budget = 0u;
    self->module.next_due = 0u;
    self->module.poll = app_poll;
    self->module.on_event = app_on_event;
    self->module.power_off = app_power_off;
    self->module.private_data = self;

    self->display_port = display;
    self->motor_port = motor;
    self->color_index = 2u;
    self->current_color_rgb565 = s_palette[2];
}

edge_status_t paint_init(paint_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->color_index = 2u; /* Default White */
    self->current_color_rgb565 = s_palette[2];
    self->stroke_count = 0;
    return EDGE_OK;
}

edge_status_t paint_shutdown(paint_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->stroke_count = 0;
    return EDGE_OK;
}

void paint_cycle_color(paint_app_t *self) {
    if (self == NULL) {
        return;
    }
    self->color_index = (uint8_t)((self->color_index + 1u) % PAINT_COLOR_COUNT);
    self->current_color_rgb565 = s_palette[self->color_index];
    if (self->motor_port != NULL && self->motor_port->vibrate != NULL) {
        self->motor_port->vibrate(self->motor_port->self, 35u);
    }
}

edge_status_t paint_draw_point(paint_app_t *self, uint16_t x, uint16_t y) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->stroke_count++;
    if (self->display_port != NULL && self->display_port->fill_rect != NULL) {
        int16_t top_left_x = (int16_t)(x - (PAINT_BRUSH_WIDTH / 2));
        int16_t top_left_y = (int16_t)(y - (PAINT_BRUSH_HEIGHT / 2));
        return self->display_port->fill_rect(self->display_port->self, top_left_x, top_left_y,
                                             PAINT_BRUSH_WIDTH, PAINT_BRUSH_HEIGHT,
                                             self->current_color_rgb565);
    }
    return EDGE_OK;
}

edge_status_t paint_clear_canvas(paint_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->stroke_count = 0;
    if (self->display_port != NULL && self->display_port->clear != NULL) {
        return self->display_port->clear(self->display_port->self, 0x0000u /* Black */);
    }
    return EDGE_OK;
}

uint8_t paint_get_color_index(const paint_app_t *self) {
    return self != NULL ? self->color_index : 0;
}

uint16_t paint_get_color_rgb565(const paint_app_t *self) {
    return self != NULL ? self->current_color_rgb565 : 0;
}

uint32_t paint_get_stroke_count(const paint_app_t *self) {
    return self != NULL ? self->stroke_count : 0;
}
