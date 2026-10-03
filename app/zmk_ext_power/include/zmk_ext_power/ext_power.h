#ifndef ZMK_EXT_POWER_H
#define ZMK_EXT_POWER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zmk_ext_power_hw_if {
    void *self;
    edge_status_t (*set_power)(void *self, bool enable);
} zmk_ext_power_hw_if_t;

typedef struct zmk_ext_power_event_sink_if {
    void *self;
    edge_status_t (*post_power_changed)(void *self, bool is_on);
} zmk_ext_power_event_sink_if_t;

typedef struct zmk_ext_power_app {
    edge_module_t module;
    zmk_ext_power_hw_if_t hw;
    zmk_ext_power_event_sink_if_t event_sink;
    bool enabled;
    bool auto_off_on_sleep;
    bool pm_sleeping;
} zmk_ext_power_app_t;

void zmk_ext_power_construct(zmk_ext_power_app_t *app, uint32_t module_id, uint8_t priority,
                             const zmk_ext_power_hw_if_t *hw,
                             const zmk_ext_power_event_sink_if_t *event_sink);

edge_status_t zmk_ext_power_init(zmk_ext_power_app_t *app);

edge_status_t zmk_ext_power_enable(zmk_ext_power_app_t *app);

edge_status_t zmk_ext_power_disable(zmk_ext_power_app_t *app);

edge_status_t zmk_ext_power_toggle(zmk_ext_power_app_t *app);

bool zmk_ext_power_is_enabled(const zmk_ext_power_app_t *app);

edge_status_t zmk_ext_power_on_activity_change(zmk_ext_power_app_t *app, uint8_t activity_state);

#ifdef __cplusplus
}
#endif

#endif
