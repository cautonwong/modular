#ifndef APP_TIMER_H
#define APP_TIMER_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "timer/ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct watch_timer_status {
    uint32_t distance_to_expiry_ms;
    bool expired;
} watch_timer_status_t;

typedef struct watch_timer_app {
    edge_module_t module;
    const timer_clock_if_t *clock;
    const timer_alert_if_t *alert;

    bool is_active;
    bool is_triggered;
    uint32_t expiry_tick_ms;
    uint32_t duration_ms;
    bool alert_emitted;
} watch_timer_app_t;

void watch_timer_construct(watch_timer_app_t *self, uint32_t module_id, uint32_t priority,
                           const timer_clock_if_t *clock, const timer_alert_if_t *alert);
void timer_construct(watch_timer_app_t *self, uint32_t module_id, uint32_t priority,
                     const timer_clock_if_t *clock, const timer_alert_if_t *alert);

edge_status_t watch_timer_init(watch_timer_app_t *self);
edge_status_t watch_timer_shutdown(watch_timer_app_t *self);

void watch_timer_start(watch_timer_app_t *self, uint32_t duration_ms);
void watch_timer_stop(watch_timer_app_t *self);
void watch_timer_reset_expired(watch_timer_app_t *self);

edge_status_t watch_timer_get_status(const watch_timer_app_t *self,
                                     watch_timer_status_t *out_status);

bool watch_timer_is_running(const watch_timer_app_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_TIMER_H */
