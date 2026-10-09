#ifndef ZMK_BACKLIGHT_H
#define ZMK_BACKLIGHT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZMK_BACKLIGHT_BRT_MAX 100
#define ZMK_BACKLIGHT_DEFAULT_STEP 10

typedef struct zmk_backlight_hw_if {
    void *self;
    edge_status_t (*set_brightness)(void *self, uint8_t brightness_pct);
} zmk_backlight_hw_if_t;

typedef struct zmk_backlight_app {
    edge_module_t module;
    zmk_backlight_hw_if_t hw;
    uint8_t brightness;
    uint8_t step;
    bool on;
    bool auto_off_idle;
    bool is_sleeping;
} zmk_backlight_app_t;

void zmk_backlight_construct(zmk_backlight_app_t *app, uint32_t module_id, uint8_t priority,
                             const zmk_backlight_hw_if_t *hw);

edge_status_t zmk_backlight_init(zmk_backlight_app_t *app);

edge_status_t zmk_backlight_on(zmk_backlight_app_t *app);

edge_status_t zmk_backlight_off(zmk_backlight_app_t *app);

edge_status_t zmk_backlight_toggle(zmk_backlight_app_t *app);

edge_status_t zmk_backlight_set_brt(zmk_backlight_app_t *app, uint8_t brightness);

edge_status_t zmk_backlight_adjust_brt(zmk_backlight_app_t *app, int8_t direction);

edge_status_t zmk_backlight_cycle_brt(zmk_backlight_app_t *app);

uint8_t zmk_backlight_get_brt(const zmk_backlight_app_t *app);

bool zmk_backlight_is_on(const zmk_backlight_app_t *app);

edge_status_t zmk_backlight_on_activity_state(zmk_backlight_app_t *app, uint8_t activity_state);

#ifdef __cplusplus
}
#endif

#endif
