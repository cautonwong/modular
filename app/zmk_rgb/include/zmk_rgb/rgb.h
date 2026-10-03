#ifndef ZMK_RGB_H
#define ZMK_RGB_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zmk_rgb_effect {
    ZMK_RGB_EFFECT_SOLID = 0,
    ZMK_RGB_EFFECT_BREATHE = 1,
    ZMK_RGB_EFFECT_SPECTRUM = 2,
    ZMK_RGB_EFFECT_SWIRL = 3,
    ZMK_RGB_EFFECT_COUNT = 4,
};

typedef struct zmk_rgb_driver_if {
    void *self;
    edge_status_t (*update_rgb)(void *self, bool on, uint16_t hue, uint8_t sat, uint8_t val,
                                uint8_t effect);
} zmk_rgb_driver_if_t;

typedef struct zmk_rgb_app {
    edge_module_t module;
    const zmk_rgb_driver_if_t *driver;

    bool on;
    uint16_t hue;       /* 0 .. 359 */
    uint8_t saturation; /* 0 .. 100 */
    uint8_t brightness; /* 0 .. 100 */
    uint8_t effect;     /* enum zmk_rgb_effect */
    uint8_t speed;      /* 1 .. 5 */
} zmk_rgb_app_t;

void zmk_rgb_construct(zmk_rgb_app_t *self, uint32_t module_id, uint32_t priority,
                       const zmk_rgb_driver_if_t *driver);

edge_status_t zmk_rgb_init(zmk_rgb_app_t *self);
edge_status_t zmk_rgb_shutdown(zmk_rgb_app_t *self);

edge_status_t zmk_rgb_toggle(zmk_rgb_app_t *self);
edge_status_t zmk_rgb_on(zmk_rgb_app_t *self);
edge_status_t zmk_rgb_off(zmk_rgb_app_t *self);

edge_status_t zmk_rgb_set_hue(zmk_rgb_app_t *self, uint16_t hue);
edge_status_t zmk_rgb_set_saturation(zmk_rgb_app_t *self, uint8_t saturation);
edge_status_t zmk_rgb_set_brightness(zmk_rgb_app_t *self, uint8_t brightness);
edge_status_t zmk_rgb_set_effect(zmk_rgb_app_t *self, uint8_t effect);
edge_status_t zmk_rgb_next_effect(zmk_rgb_app_t *self);

#ifdef __cplusplus
}
#endif

#endif /* ZMK_RGB_H */
