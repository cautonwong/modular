#ifndef APP_PAINT_H
#define APP_PAINT_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "paint/ports.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAINT_COLOR_COUNT 8
#define PAINT_BRUSH_WIDTH 10
#define PAINT_BRUSH_HEIGHT 10

typedef struct paint_app {
    edge_module_t module;
    const paint_display_if_t *display_port;
    const paint_motor_if_t *motor_port;

    uint8_t color_index;
    uint16_t current_color_rgb565;
    uint32_t stroke_count;
} paint_app_t;

void paint_construct(paint_app_t *self, uint32_t module_id, uint32_t priority,
                     const paint_display_if_t *display, const paint_motor_if_t *motor);

edge_status_t paint_init(paint_app_t *self);
edge_status_t paint_shutdown(paint_app_t *self);

void paint_cycle_color(paint_app_t *self);
edge_status_t paint_draw_point(paint_app_t *self, uint16_t x, uint16_t y);
edge_status_t paint_clear_canvas(paint_app_t *self);
uint8_t paint_get_color_index(const paint_app_t *self);
uint16_t paint_get_color_rgb565(const paint_app_t *self);
uint32_t paint_get_stroke_count(const paint_app_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_PAINT_H */
