#ifndef APP_WATCH_POWER_H
#define APP_WATCH_POWER_H

#include "edge/module.h"
#include "ports.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCH_INACTIVITY_DIM_MS 10000u
#define WATCH_DIM_TO_SLEEP_MS 5000u

typedef enum watch_power_state {
    WATCH_POWER_AWAKE = 0,
    WATCH_POWER_DIMMED = 1,
    WATCH_POWER_SLEEPING = 2,
    WATCH_POWER_CHARGING = 3,
} watch_power_state_t;

typedef struct watch_power {
    edge_module_t module;
    const watch_power_display_if_t *display;
    const watch_power_battery_if_t *battery;
    watch_power_state_t state;
    uint32_t inactivity_timer_ms;
    uint8_t user_brightness_percent;
    bool is_charging;
    bool is_power_present;
    uint32_t poll_count;
    uint32_t wake_lock_count;
} watch_power_t;

void watch_power_construct(watch_power_t *self, uint32_t module_id, uint32_t priority,
                           const watch_power_display_if_t *display,
                           const watch_power_battery_if_t *battery);
edge_status_t watch_power_init(watch_power_t *self);
edge_status_t watch_power_deinit(watch_power_t *self);
edge_module_t *watch_power_module(watch_power_t *self);

void watch_power_reset_inactivity(watch_power_t *self);
void watch_power_wake_up(watch_power_t *self);
void watch_power_go_to_sleep(watch_power_t *self);
void watch_power_acquire_wake_lock(watch_power_t *self);
void watch_power_release_wake_lock(watch_power_t *self);
bool watch_power_is_wake_locked(const watch_power_t *self);
edge_status_t watch_power_update(watch_power_t *self, uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif
