#ifndef ZMK_BATTERY_H
#define ZMK_BATTERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zmk_battery_hw_if {
    void *self;
    edge_status_t (*read_millivolts)(void *self, uint16_t *out_mv);
    bool (*is_charging)(void *self);
} zmk_battery_hw_if_t;

typedef struct zmk_battery_event_sink_if {
    void *self;
    edge_status_t (*post_battery_changed)(void *self, uint8_t percentage, bool is_charging);
} zmk_battery_event_sink_if_t;

typedef struct zmk_battery_app {
    edge_module_t module;
    zmk_battery_hw_if_t hw;
    zmk_battery_event_sink_if_t event_sink;
    uint8_t last_percentage;
    uint16_t last_mv;
    bool last_charging;
} zmk_battery_app_t;

void zmk_battery_construct(zmk_battery_app_t *app, uint32_t module_id, uint8_t priority,
                           const zmk_battery_hw_if_t *hw,
                           const zmk_battery_event_sink_if_t *event_sink);

edge_status_t zmk_battery_init(zmk_battery_app_t *app);

uint8_t zmk_battery_mv_to_pct(uint16_t mv);

edge_status_t zmk_battery_sample(zmk_battery_app_t *app);

uint8_t zmk_battery_get_percentage(const zmk_battery_app_t *app);

uint16_t zmk_battery_get_millivolts(const zmk_battery_app_t *app);

bool zmk_battery_is_charging(const zmk_battery_app_t *app);

#ifdef __cplusplus
}
#endif

#endif
