#ifndef WATCH_UI_H
#define WATCH_UI_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WATCH_UI_STACK_DEPTH 10u
#define WATCH_UI_LAUNCHER_PAGES 3u
#define WATCH_UI_APPS_PER_PAGE 4u

typedef enum watch_screen_id {
    WATCH_SCREEN_WATCHFACE = 0,
    WATCH_SCREEN_NOTIFICATIONS = 1,
    WATCH_SCREEN_QUICK_SETTINGS = 2,
    WATCH_SCREEN_APP_LAUNCHER = 3,
    WATCH_SCREEN_HEART_RATE = 4,
    WATCH_SCREEN_WEATHER = 5,
    WATCH_SCREEN_MUSIC = 6,
    WATCH_SCREEN_NAV = 7,
    WATCH_SCREEN_STOPWATCH = 8,
    WATCH_SCREEN_ALARM = 9,
    WATCH_SCREEN_TIMER = 10,
    WATCH_SCREEN_STEPS = 11,
    WATCH_SCREEN_SETTINGS = 12,
    WATCH_SCREEN_FLASHLIGHT = 13,
    WATCH_SCREEN_METRONOME = 14,
    WATCH_SCREEN_CALCULATOR = 15,
    WATCH_SCREEN_DICE = 16,
    WATCH_SCREEN_PASSKEY = 17,
    WATCH_SCREEN_PADDLE = 18,
    WATCH_SCREEN_TWOS = 19,
    WATCH_SCREEN_PAINT = 20,
    WATCH_SCREEN_MOTION = 21,
    WATCH_SCREEN_BATTERY_INFO = 22,
    WATCH_SCREEN_SYSINFO = 23,
    WATCH_SCREEN_FW_UPDATE = 24,
    WATCH_SCREEN_FW_VALIDATE = 25,
    WATCH_SCREEN_ERROR = 26,
    WATCH_SCREEN_NOTIF_PREVIEW = 27,
    WATCH_SCREEN_SETTING_WATCHFACE = 28,
    WATCH_SCREEN_SETTING_TIME_FORMAT = 29,
    WATCH_SCREEN_SETTING_WEATHER_FORMAT = 30,
    WATCH_SCREEN_SETTING_WAKEUP = 31,
    WATCH_SCREEN_SETTING_HEART_RATE = 32,
    WATCH_SCREEN_SETTING_DISPLAY = 33,
    WATCH_SCREEN_SETTING_STEPS = 34,
    WATCH_SCREEN_SETTING_SET_DATE_TIME = 35,
    WATCH_SCREEN_SETTING_CHIMES = 36,
    WATCH_SCREEN_SETTING_SHAKE = 37,
    WATCH_SCREEN_SETTING_BLUETOOTH = 38,
    WATCH_SCREEN_SETTING_OTA = 39,
} watch_screen_id_t;

typedef enum watchface_style {
    WATCHFACE_STYLE_DIGITAL = 0,
    WATCHFACE_STYLE_ANALOG = 1,
    WATCHFACE_STYLE_PINETIME = 2,
    WATCHFACE_STYLE_CASIO = 3,
    WATCHFACE_STYLE_TERMINAL = 4,
    WATCHFACE_STYLE_INFINEAT = 5,
    WATCHFACE_STYLE_PRIDE = 6,
} watchface_style_t;

typedef enum watch_ui_gesture {
    WATCH_UI_GESTURE_NONE = 0,
    WATCH_UI_GESTURE_SWIPE_UP = 1,
    WATCH_UI_GESTURE_SWIPE_DOWN = 2,
    WATCH_UI_GESTURE_SWIPE_LEFT = 3,
    WATCH_UI_GESTURE_SWIPE_RIGHT = 4,
    WATCH_UI_GESTURE_TAP = 5,
    WATCH_UI_GESTURE_DOUBLE_TAP = 6,
    WATCH_UI_GESTURE_LONG_PRESS = 7,
} watch_ui_gesture_t;

typedef enum watch_ui_button_action {
    WATCH_UI_BUTTON_CLICK = 0,
    WATCH_UI_BUTTON_DOUBLE_CLICK = 1,
    WATCH_UI_BUTTON_LONG_PRESS = 2,
    WATCH_UI_BUTTON_LONGER_PRESS = 3,
} watch_ui_button_action_t;

typedef struct watch_ui {
    edge_module_t module;
    const watch_ui_display_port_t *display;
    const watch_ui_status_port_t *status_port;
    const edge_event_sink_t *event_sink;

    watch_screen_id_t current_screen;
    watchface_style_t watchface_style;

    watch_screen_id_t return_stack[WATCH_UI_STACK_DEPTH];
    uint8_t stack_ptr;

    uint8_t launcher_page;
    bool screen_on;
    uint32_t refresh_count;
} watch_ui_t;

void watch_ui_construct(watch_ui_t *self, uint32_t module_id, uint32_t priority,
                        const watch_ui_display_port_t *display,
                        const watch_ui_status_port_t *status_port,
                        const edge_event_sink_t *event_sink);

edge_status_t watch_ui_init(watch_ui_t *self, const watch_ui_display_port_t *display,
                            const watch_ui_status_port_t *status_port,
                            const edge_event_sink_t *event_sink);

watch_screen_id_t watch_ui_get_current_screen(const watch_ui_t *self);
void watch_ui_set_watchface_style(watch_ui_t *self, watchface_style_t style);
watchface_style_t watch_ui_get_watchface_style(const watch_ui_t *self);

edge_status_t watch_ui_load_screen(watch_ui_t *self, watch_screen_id_t screen);
edge_status_t watch_ui_return_previous(watch_ui_t *self);

edge_status_t watch_ui_on_touch_gesture(watch_ui_t *self, watch_ui_gesture_t gesture);

edge_status_t watch_ui_on_button_pressed(watch_ui_t *self);
edge_status_t watch_ui_on_button_event(watch_ui_t *self, watch_ui_button_action_t event);
edge_status_t watch_ui_refresh(watch_ui_t *self);

#ifdef __cplusplus
}
#endif

#endif /* WATCH_UI_H */
