#ifndef ZMK_PM_H
#define ZMK_PM_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "zmk_pm/ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zmk_activity_state {
    ZMK_ACTIVITY_ACTIVE = 0,
    ZMK_ACTIVITY_IDLE = 1,
    ZMK_ACTIVITY_SLEEP = 2,
};

typedef struct zmk_pm_config {
    uint32_t idle_timeout_ms;  /* e.g. 30000ms */
    uint32_t sleep_timeout_ms; /* e.g. 900000ms */
} zmk_pm_config_t;

typedef struct zmk_pm_app {
    edge_module_t module;
    const zmk_pm_sink_if_t *sink;

    zmk_pm_config_t config;
    uint8_t current_state;
    uint32_t last_activity_time_ms;
    uint32_t current_time_ms;
} zmk_pm_app_t;

void zmk_pm_construct(zmk_pm_app_t *self, uint32_t module_id, uint32_t priority,
                      const zmk_pm_config_t *config, const zmk_pm_sink_if_t *sink);

edge_status_t zmk_pm_init(zmk_pm_app_t *self);
edge_status_t zmk_pm_shutdown(zmk_pm_app_t *self);

void zmk_pm_notify_activity(zmk_pm_app_t *self, uint32_t timestamp_ms);

edge_status_t zmk_pm_tick(zmk_pm_app_t *self, uint32_t timestamp_ms);

uint8_t zmk_pm_get_activity_state(const zmk_pm_app_t *self);

#ifdef __cplusplus
}
#endif

#endif /* ZMK_PM_H */
