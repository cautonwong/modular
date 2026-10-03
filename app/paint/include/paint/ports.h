#ifndef APP_PAINT_PORTS_H
#define APP_PAINT_PORTS_H

#include "edge/errors.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct paint_display_if {
    void *self;
    edge_status_t (*fill_rect)(void *self, int16_t x, int16_t y, uint16_t w, uint16_t h,
                               uint16_t rgb565_color);
    edge_status_t (*clear)(void *self, uint16_t rgb565_color);
} paint_display_if_t;

typedef struct paint_motor_if {
    void *self;
    edge_status_t (*vibrate)(void *self, uint16_t duration_ms);
} paint_motor_if_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_PAINT_PORTS_H */
