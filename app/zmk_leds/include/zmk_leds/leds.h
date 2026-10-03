#ifndef ZMK_LEDS_H
#define ZMK_LEDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZMK_LEDS_MAX_INDICATORS 8

typedef enum {
    ZMK_LED_IND_NUM_LOCK = 1 << 0,
    ZMK_LED_IND_CAPS_LOCK = 1 << 1,
    ZMK_LED_IND_SCROLL_LOCK = 1 << 2,
    ZMK_LED_IND_COMPOSE = 1 << 3,
    ZMK_LED_IND_KANA = 1 << 4,
} zmk_led_indicator_mask_t;

typedef struct zmk_led_config {
    uint8_t led_index;
    uint8_t indicator_mask;
    uint8_t active_brightness;
    uint8_t inactive_brightness;
    uint8_t disconnected_brightness;
    bool on_while_idle;
} zmk_led_config_t;

typedef struct zmk_leds_hw_if {
    void *self;
    edge_status_t (*set_brightness)(void *self, uint8_t led_index, uint8_t brightness_pct);
} zmk_leds_hw_if_t;

typedef struct zmk_leds_app {
    edge_module_t module;
    zmk_leds_hw_if_t hw;
    zmk_led_config_t configs[ZMK_LEDS_MAX_INDICATORS];
    uint8_t config_count;
    uint8_t active_indicators;
    uint8_t activity_state;
    bool endpoint_connected;
    bool usb_powered;
    bool pm_suspended;
} zmk_leds_app_t;

void zmk_leds_construct(zmk_leds_app_t *app, uint32_t module_id, uint8_t priority,
                        const zmk_leds_hw_if_t *hw);

edge_status_t zmk_leds_init(zmk_leds_app_t *app);

edge_status_t zmk_leds_add_indicator(zmk_leds_app_t *app, const zmk_led_config_t *config);

edge_status_t zmk_leds_update(zmk_leds_app_t *app);

edge_status_t zmk_leds_on_hid_indicators(zmk_leds_app_t *app, uint8_t indicators);

edge_status_t zmk_leds_on_activity_state(zmk_leds_app_t *app, uint8_t activity_state);

edge_status_t zmk_leds_on_endpoint_status(zmk_leds_app_t *app, bool connected, bool is_usb_powered);

#ifdef __cplusplus
}
#endif

#endif
