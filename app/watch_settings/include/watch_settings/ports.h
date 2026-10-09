#ifndef APP_WATCH_SETTINGS_PORTS_H
#define APP_WATCH_SETTINGS_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum watch_face_type {
    WATCH_FACE_DIGITAL = 0,
    WATCH_FACE_ANALOG = 1,
    WATCH_FACE_PINETIME_STYLE = 2,
    WATCH_FACE_CASIO = 3,
    WATCH_FACE_TERMINAL = 4,
    WATCH_FACE_INFINEAT = 5,
    WATCH_FACE_PRIDE = 6,
} watch_face_type_t;

typedef enum clock_format_type {
    CLOCK_FORMAT_24H = 0,
    CLOCK_FORMAT_12H = 1,
} clock_format_type_t;

typedef enum notification_mode {
    NOTIF_ON = 0,
    NOTIF_OFF = 1,
    NOTIF_SLEEP = 2,
} notification_mode_t;

typedef enum chimes_mode {
    CHIMES_NONE = 0,
    CHIMES_HOURS = 1,
    CHIMES_HALF_HOURS = 2,
} chimes_mode_t;

#define WAKE_MODE_FLAG_SINGLE_TAP (1u << 0)
#define WAKE_MODE_FLAG_DOUBLE_TAP (1u << 1)
#define WAKE_MODE_FLAG_RAISE_WRIST (1u << 2)
#define WAKE_MODE_FLAG_SHAKE (1u << 3)
#define WAKE_MODE_FLAG_LOWER_WRIST (1u << 4)

typedef enum weather_format_type {
    WEATHER_FORMAT_METRIC = 0,
    WEATHER_FORMAT_IMPERIAL = 1,
} weather_format_type_t;

typedef enum pride_flag_type {
    PRIDE_FLAG_GAY = 0,
    PRIDE_FLAG_TRANS = 1,
    PRIDE_FLAG_BI = 2,
    PRIDE_FLAG_LESBIAN = 3,
} pride_flag_type_t;

typedef enum pts_gauge_style {
    PTS_GAUGE_FULL = 0,
    PTS_GAUGE_HALF = 1,
    PTS_GAUGE_NUMERIC = 2,
} pts_gauge_style_t;

typedef struct pts_settings {
    uint8_t color_time;
    uint8_t color_bar;
    uint8_t color_bg;
    pts_gauge_style_t gauge_style;
    bool weather_enable;
} pts_settings_t;

typedef struct infineat_settings {
    bool show_side_cover;
    int32_t color_index;
} infineat_settings_t;

#define SETTINGS_FORMAT_VERSION 0x000Au

typedef struct watch_settings_data {
    uint32_t version;
    uint32_t steps_goal;
    uint32_t screen_timeout_ms;
    bool always_on_display;
    clock_format_type_t clock_format;
    weather_format_type_t weather_format;
    notification_mode_t notification_mode;
    watch_face_type_t watch_face;
    chimes_mode_t chimes_mode;
    pts_settings_t pts;
    pride_flag_type_t pride_flag;
    infineat_settings_t infineat;
    uint8_t wake_mode_flags;
    uint16_t shake_wake_threshold;
    uint8_t brightness_level;
    uint16_t heart_rate_background_period_s;
} watch_settings_data_t;

/* Consumer-defined port for persisting watch settings to flash/storage */
typedef struct watch_settings_store_if {
    void *self;
    edge_status_t (*load)(void *self, watch_settings_data_t *out_data);
    edge_status_t (*save)(void *self, const watch_settings_data_t *data);
} watch_settings_store_if_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_WATCH_SETTINGS_PORTS_H */
