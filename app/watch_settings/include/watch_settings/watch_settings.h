#ifndef APP_WATCH_SETTINGS_H
#define APP_WATCH_SETTINGS_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "watch_settings/ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct watch_settings_app {
    edge_module_t module;
    const watch_settings_store_if_t *store;

    watch_settings_data_t data;
    bool is_dirty;
    bool ble_radio_enabled;
} watch_settings_app_t;

void watch_settings_construct(watch_settings_app_t *self, uint32_t module_id, uint32_t priority,
                              const watch_settings_store_if_t *store);

edge_status_t watch_settings_init(watch_settings_app_t *self);
edge_status_t watch_settings_shutdown(watch_settings_app_t *self);
edge_status_t watch_settings_save(watch_settings_app_t *self);

void watch_settings_set_watch_face(watch_settings_app_t *self, watch_face_type_t face);
watch_face_type_t watch_settings_get_watch_face(const watch_settings_app_t *self);

void watch_settings_set_clock_format(watch_settings_app_t *self, clock_format_type_t format);
clock_format_type_t watch_settings_get_clock_format(const watch_settings_app_t *self);

void watch_settings_set_notification_mode(watch_settings_app_t *self, notification_mode_t mode);
notification_mode_t watch_settings_get_notification_mode(const watch_settings_app_t *self);

void watch_settings_set_screen_timeout(watch_settings_app_t *self, uint32_t timeout_ms);
uint32_t watch_settings_get_screen_timeout(const watch_settings_app_t *self);

void watch_settings_set_always_on_display(watch_settings_app_t *self, bool enable);
bool watch_settings_get_always_on_display(const watch_settings_app_t *self);

void watch_settings_set_steps_goal(watch_settings_app_t *self, uint32_t goal);
uint32_t watch_settings_get_steps_goal(const watch_settings_app_t *self);

void watch_settings_set_wake_mode(watch_settings_app_t *self, uint8_t wake_flag, bool enable);
bool watch_settings_is_wake_mode_enabled(const watch_settings_app_t *self, uint8_t wake_flag);

void watch_settings_set_brightness(watch_settings_app_t *self, uint8_t level);
uint8_t watch_settings_get_brightness(const watch_settings_app_t *self);

void watch_settings_set_weather_format(watch_settings_app_t *self, weather_format_type_t format);
weather_format_type_t watch_settings_get_weather_format(const watch_settings_app_t *self);

void watch_settings_set_chimes_mode(watch_settings_app_t *self, chimes_mode_t mode);
chimes_mode_t watch_settings_get_chimes_mode(const watch_settings_app_t *self);

void watch_settings_set_pts_settings(watch_settings_app_t *self, const pts_settings_t *pts);
pts_settings_t watch_settings_get_pts_settings(const watch_settings_app_t *self);

void watch_settings_set_pride_flag(watch_settings_app_t *self, pride_flag_type_t flag);
pride_flag_type_t watch_settings_get_pride_flag(const watch_settings_app_t *self);

void watch_settings_set_infineat_settings(watch_settings_app_t *self,
                                          const infineat_settings_t *infineat);
infineat_settings_t watch_settings_get_infineat_settings(const watch_settings_app_t *self);

void watch_settings_set_shake_wake_threshold(watch_settings_app_t *self, uint16_t threshold);
uint16_t watch_settings_get_shake_wake_threshold(const watch_settings_app_t *self);

void watch_settings_set_ble_enabled(watch_settings_app_t *self, bool enabled);
bool watch_settings_get_ble_enabled(const watch_settings_app_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_WATCH_SETTINGS_H */
