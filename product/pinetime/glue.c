#include <stddef.h>
#include <stdint.h>
static void copy_bytes(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
}
#include "glue.h"

void pinetime_glue_init(pinetime_glue_state_t *state) {
    if (state == NULL) {
        return;
    }
    *state = (__typeof__(*state)){0};
    state->display_brightness = 100u;
    state->display_on = true;
    state->ppg_sample = 10000u;
    state->imu_sample = (bma421_accel_t){.x = 0, .y = 0, .z = 1024};
    state->battery_mv = 3900u;
    state->battery_pct = 80u;
    state->battery_charging = false;
    state->battery_power_present = false;
    state->validator_word = 0xFFFFFFFFu;

    state->alarm_data = (alarm_settings_t){
        .version = ALARM_FORMAT_VERSION,
        .hours = 7,
        .minutes = 0,
        .recurrence = ALARM_RECUR_DAILY,
        .is_enabled = false,
    };

    state->settings_data = (watch_settings_data_t){
        .version = SETTINGS_FORMAT_VERSION,
        .steps_goal = 10000u,
        .screen_timeout_ms = 15000u,
        .always_on_display = false,
        .clock_format = CLOCK_FORMAT_24H,
        .notification_mode = NOTIF_ON,
        .watch_face = WATCH_FACE_DIGITAL,
        .chimes_mode = CHIMES_NONE,
        .wake_mode_flags = WAKE_MODE_FLAG_RAISE_WRIST,
        .shake_wake_threshold = 200u,
        .brightness_level = 2u,
        .heart_rate_background_period_s = 600u,
    };
}

/* Touch Input Port */
// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_read_touch(void *self, touch_raw_info_t *out_info) {
    const pinetime_glue_state_t *state = (const pinetime_glue_state_t *)self;
    if (state == NULL || out_info == NULL) {
        return EDGE_EINVAL;
    }
    out_info->touching = state->touch_pressed;
    out_info->x = state->touch_x;
    out_info->y = state->touch_y;
    out_info->hardware_gesture = state->touch_gesture;
    out_info->is_valid = true;
    return EDGE_OK;
}

// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_touch_sleep(void *self, bool enable) {
    (void)self;
    (void)enable;
    return EDGE_OK;
}

void pinetime_make_touch_port(touch_input_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->read_touch = glue_read_touch;
    out->sleep = glue_touch_sleep;
}

/* Display Power Port */
// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_set_brightness(void *self, uint8_t percent) {
    pinetime_glue_state_t *state = (pinetime_glue_state_t *)self;
    if (state == NULL) {
        return EDGE_EINVAL;
    }
    state->display_brightness = percent;
    return EDGE_OK;
}

// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_display_sleep(void *self, bool enable) {
    pinetime_glue_state_t *state = (pinetime_glue_state_t *)self;
    if (state == NULL) {
        return EDGE_EINVAL;
    }
    state->display_on = !enable;
    return EDGE_OK;
}

void pinetime_make_display_power_port(watch_power_display_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->set_brightness = glue_set_brightness;
    out->sleep = glue_display_sleep;
}

/* Battery Power Port */
// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_read_battery(void *self, uint16_t *out_mv, uint8_t *out_pct,
                                       bool *out_charging, bool *out_present) {
    const pinetime_glue_state_t *state = (const pinetime_glue_state_t *)self;
    if (state == NULL) {
        return EDGE_EINVAL;
    }
    if (out_mv != NULL) {
        *out_mv = state->battery_mv;
    }
    if (out_pct != NULL) {
        *out_pct = state->battery_pct;
    }
    if (out_charging != NULL) {
        *out_charging = state->battery_charging;
    }
    if (out_present != NULL) {
        *out_present = state->battery_power_present;
    }
    return EDGE_OK;
}

void pinetime_make_battery_power_port(watch_power_battery_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->read_status = glue_read_battery;
}

/* PPG Sensor Port */
// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_read_ppg(void *self, uint16_t *out_hrs, uint16_t *out_als) {
    const pinetime_glue_state_t *state = (const pinetime_glue_state_t *)self;
    if (state == NULL || out_hrs == NULL) {
        return EDGE_EINVAL;
    }
    *out_hrs = state->ppg_sample;
    if (out_als != NULL) {
        *out_als = 100u;
    }
    return EDGE_OK;
}

void pinetime_make_ppg_port(ppg_sensor_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->read_sample = glue_read_ppg;
}

/* IMU Sensor Port */
// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_read_imu(void *self, int16_t *out_x, int16_t *out_y, int16_t *out_z) {
    const pinetime_glue_state_t *state = (const pinetime_glue_state_t *)self;
    if (state == NULL) {
        return EDGE_EINVAL;
    }
    if (out_x != NULL) {
        *out_x = state->imu_sample.x;
    }
    if (out_y != NULL) {
        *out_y = state->imu_sample.y;
    }
    if (out_z != NULL) {
        *out_z = state->imu_sample.z;
    }
    return EDGE_OK;
}

void pinetime_make_imu_port(imu_sensor_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->read_accel = glue_read_imu;
}

/* RTC Clock Port */
// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_get_rtc_counter(void *self, uint32_t *out_counter) {
    const pinetime_glue_state_t *state = (const pinetime_glue_state_t *)self;
    if (state == NULL || out_counter == NULL) {
        return EDGE_EINVAL;
    }
    *out_counter = state->rtc_ticks;
    return EDGE_OK;
}

// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static uint32_t glue_get_tick_frequency(void *self) {
    (void)self;
    return 1000u;
}

void pinetime_make_rtc_port(rtc_clock_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->get_counter = glue_get_rtc_counter;
    out->get_tick_frequency = glue_get_tick_frequency;
}

/* BLE GATT Server Port */
static edge_status_t glue_gatt_notify(void *self, uint16_t char_handle, const uint8_t *data,
                                      size_t len) {
    pinetime_glue_state_t *state = (pinetime_glue_state_t *)self;
    if (state == NULL) {
        return EDGE_EINVAL;
    }
    state->last_gatt_handle = char_handle;
    state->last_gatt_len = len;
    if (data != NULL && len <= sizeof(state->last_gatt_data)) {
        copy_bytes(state->last_gatt_data, data, len);
    }
    state->gatt_notify_count++;
    return EDGE_OK;
}

void pinetime_make_gatt_port(ble_gatt_server_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->notify = glue_gatt_notify;
}

/* BLE Time Sink Port */
static edge_status_t glue_set_time(void *self, uint16_t year, uint8_t month, uint8_t day,
                                   uint8_t hour, uint8_t minute, uint8_t second) {
    pinetime_glue_state_t *state = (pinetime_glue_state_t *)self;
    if (state == NULL || state->time_app == NULL) {
        return EDGE_EINVAL;
    }
    return watch_time_set(state->time_app, year, month, day, hour, minute, second);
}

void pinetime_make_time_sink_port(ble_time_sink_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->set_time = glue_set_time;
}

/* Alarm Storage & Alert Ports */
// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_alarm_load(void *self, alarm_settings_t *out_settings) {
    const pinetime_glue_state_t *state = (const pinetime_glue_state_t *)self;
    if (state == NULL || out_settings == NULL) {
        return EDGE_EINVAL;
    }
    *out_settings = state->alarm_data;
    return EDGE_OK;
}

// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_alarm_save(void *self, const alarm_settings_t *settings) {
    pinetime_glue_state_t *state = (pinetime_glue_state_t *)self;
    if (state == NULL || settings == NULL) {
        return EDGE_EINVAL;
    }
    state->alarm_data = *settings;
    return EDGE_OK;
}

void pinetime_make_alarm_storage_port(alarm_storage_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->load = glue_alarm_load;
    out->save = glue_alarm_save;
}

// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_alarm_alert_start(void *self) {
    (void)self;
    return EDGE_OK;
}

// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_alarm_alert_stop(void *self) {
    (void)self;
    return EDGE_OK;
}

void pinetime_make_alarm_alert_port(alarm_alert_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->start_alert = glue_alarm_alert_start;
    out->stop_alert = glue_alarm_alert_stop;
}

/* Timer Ports */
// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static uint32_t glue_timer_get_tick_ms(void *self) {
    pinetime_glue_state_t *state = (pinetime_glue_state_t *)self;
    return state != NULL ? state->rtc_ticks : 0u;
}

void pinetime_make_timer_clock_port(timer_clock_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->get_tick_ms = glue_timer_get_tick_ms;
}

void pinetime_make_stopwatch_clock_port(stopwatch_clock_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->get_tick_ms = glue_timer_get_tick_ms;
}

// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_timer_alert_start(void *self) {
    (void)self;
    return EDGE_OK;
}

// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_timer_alert_stop(void *self) {
    (void)self;
    return EDGE_OK;
}

void pinetime_make_timer_alert_port(timer_alert_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->start_alert = glue_timer_alert_start;
    out->stop_alert = glue_timer_alert_stop;
}

/* Watch Settings Store Port */
// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_settings_load(void *self, watch_settings_data_t *out_settings) {
    const pinetime_glue_state_t *state = (const pinetime_glue_state_t *)self;
    if (state == NULL || out_settings == NULL) {
        return EDGE_EINVAL;
    }
    *out_settings = state->settings_data;
    return EDGE_OK;
}

// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_settings_save(void *self, const watch_settings_data_t *settings) {
    pinetime_glue_state_t *state = (pinetime_glue_state_t *)self;
    if (state == NULL || settings == NULL) {
        return EDGE_EINVAL;
    }
    state->settings_data = *settings;
    return EDGE_OK;
}

void pinetime_make_settings_store_port(watch_settings_store_if_t *out,
                                       pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->load = glue_settings_load;
    out->save = glue_settings_save;
}

/* Weather Time Port */
// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static uint64_t glue_weather_get_time_epoch(void *self) {
    const pinetime_glue_state_t *state = (const pinetime_glue_state_t *)self;
    return state != NULL ? (uint64_t)(state->rtc_ticks / 1000u) : 0u;
}

// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static uint32_t glue_weather_get_minute_of_day(void *self) {
    const pinetime_glue_state_t *state = (const pinetime_glue_state_t *)self;
    if (state != NULL && state->time_app != NULL) {
        watch_datetime_t dt = watch_time_get(state->time_app);
        return (uint32_t)dt.hour * 60u + (uint32_t)dt.minute;
    }
    return 720u;
}

void pinetime_make_weather_time_port(ble_weather_time_port_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->get_timestamp_sec = glue_weather_get_time_epoch;
    out->get_minute_of_day = glue_weather_get_minute_of_day;
}

/* Music Transport Port */
static edge_status_t glue_music_send_command(void *self, uint8_t event_byte) {
    (void)self;
    (void)event_byte;
    return EDGE_OK;
}

static uint64_t glue_music_get_tick_ms(void *self) {
    pinetime_glue_state_t *state = (pinetime_glue_state_t *)self;
    return state != NULL ? (uint64_t)state->rtc_ticks : 0u;
}

void pinetime_make_music_transport_port(ble_music_transport_port_t *out,
                                        pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->send_event = glue_music_send_command;
    out->get_tick_ms = glue_music_get_tick_ms;
}

/* Notifications Call Port */
static edge_status_t glue_notif_call_response(void *self, uint8_t response_code) {
    (void)self;
    (void)response_code;
    return EDGE_OK;
}

void pinetime_make_notif_call_port(ble_notifications_call_port_t *out,
                                   pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->send_call_response = glue_notif_call_response;
}

/* Motion Notify Port */
static edge_status_t glue_motion_notify_steps(void *self, uint32_t step_count) {
    pinetime_glue_state_t *state = (pinetime_glue_state_t *)self;
    if (state == NULL) {
        return EDGE_EINVAL;
    }
    uint8_t buf[4] = {
        (uint8_t)(step_count & 0xFFu),
        (uint8_t)((step_count >> 8) & 0xFFu),
        (uint8_t)((step_count >> 16) & 0xFFu),
        (uint8_t)((step_count >> 24) & 0xFFu),
    };
    return glue_gatt_notify(state, 0x3401u, buf, sizeof(buf));
}

static edge_status_t glue_motion_notify_raw(void *self, int16_t x, int16_t y, int16_t z) {
    pinetime_glue_state_t *state = (pinetime_glue_state_t *)self;
    if (state == NULL) {
        return EDGE_EINVAL;
    }
    uint8_t buf[6] = {
        (uint8_t)(x & 0xFF),        (uint8_t)((x >> 8) & 0xFF), (uint8_t)(y & 0xFF),
        (uint8_t)((y >> 8) & 0xFF), (uint8_t)(z & 0xFF),        (uint8_t)((z >> 8) & 0xFF),
    };
    return glue_gatt_notify(state, 0x3402u, buf, sizeof(buf));
}

void pinetime_make_motion_notify_port(ble_motion_notify_port_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->notify_step_count = glue_motion_notify_steps;
    out->notify_motion_values = glue_motion_notify_raw;
}

/* BLE FS Ports */
static edge_status_t glue_fs_send_response(void *self, const uint8_t *data, size_t len) {
    return glue_gatt_notify(self, 0x3501u, data, len);
}

void pinetime_make_fs_tx_port(ble_fs_tx_port_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->send_response = glue_fs_send_response;
}

static edge_status_t glue_fs_open(void *self, const char *path, uint32_t flags) {
    (void)self;
    (void)path;
    (void)flags;
    return EDGE_OK;
}

static edge_status_t glue_fs_close(void *self) {
    (void)self;
    return EDGE_OK;
}

static edge_status_t glue_fs_read(void *self, uint32_t offset, uint8_t *buf, uint32_t size,
                                  uint32_t *read_len) {
    (void)self;
    (void)offset;
    (void)buf;
    (void)size;
    if (read_len != NULL) {
        *read_len = 0;
    }
    return EDGE_OK;
}

static edge_status_t glue_fs_write(void *self, uint32_t offset, const uint8_t *buf, uint32_t size) {
    (void)self;
    (void)offset;
    (void)buf;
    (void)size;
    return EDGE_OK;
}

static edge_status_t glue_fs_delete(void *self, const char *path) {
    (void)self;
    (void)path;
    return EDGE_OK;
}

static edge_status_t glue_fs_dir_open(void *self, const char *path) {
    (void)self;
    (void)path;
    return EDGE_OK;
}

static edge_status_t glue_fs_dir_read(void *self, char *entry_name, uint32_t max_len,
                                      uint32_t *file_size, bool *is_dir) {
    (void)self;
    (void)entry_name;
    (void)max_len;
    (void)file_size;
    (void)is_dir;
    return EDGE_ENOENT;
}

static edge_status_t glue_fs_dir_close(void *self) {
    (void)self;
    return EDGE_OK;
}

static edge_status_t glue_fs_dir_create(void *self, const char *path) {
    (void)self;
    (void)path;
    return EDGE_OK;
}

static edge_status_t glue_fs_rename(void *self, const char *old_path, const char *new_path) {
    (void)self;
    (void)old_path;
    (void)new_path;
    return EDGE_OK;
}

static edge_status_t glue_fs_free_space(void *self, uint32_t *free_bytes) {
    (void)self;
    if (free_bytes != NULL) {
        *free_bytes = 1048576u;
    }
    return EDGE_OK;
}

void pinetime_make_fs_storage_port(ble_fs_storage_port_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->file_open = glue_fs_open;
    out->file_close = glue_fs_close;
    out->file_read = glue_fs_read;
    out->file_write = glue_fs_write;
    out->file_delete = glue_fs_delete;
    out->dir_open = glue_fs_dir_open;
    out->dir_read = glue_fs_dir_read;
    out->dir_close = glue_fs_dir_close;
    out->dir_create = glue_fs_dir_create;
    out->file_rename = glue_fs_rename;
    out->get_free_space = glue_fs_free_space;
}

/* BLE DFU Ports */
static edge_status_t glue_dfu_erase(void *self, uint32_t offset, uint32_t length) {
    (void)self;
    (void)offset;
    (void)length;
    return EDGE_OK;
}

static edge_status_t glue_dfu_write(void *self, uint32_t offset, const uint8_t *data,
                                    uint32_t size) {
    (void)self;
    (void)offset;
    (void)data;
    (void)size;
    return EDGE_OK;
}

static edge_status_t glue_dfu_read(void *self, uint32_t offset, uint8_t *data, uint32_t size) {
    (void)self;
    (void)offset;
    (void)data;
    (void)size;
    return EDGE_OK;
}

void pinetime_make_dfu_flash_port(ble_dfu_flash_port_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->erase_range = glue_dfu_erase;
    out->write_chunk = glue_dfu_write;
    out->read_chunk = glue_dfu_read;
}

static edge_status_t glue_dfu_send_resp(void *self, uint8_t req_opcode, uint8_t error_code) {
    (void)self;
    (void)req_opcode;
    (void)error_code;
    return EDGE_OK;
}

static edge_status_t glue_dfu_send_prn(void *self, uint32_t bytes_received) {
    (void)self;
    (void)bytes_received;
    return EDGE_OK;
}

void pinetime_make_dfu_notify_port(ble_dfu_notify_port_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->send_response = glue_dfu_send_resp;
    out->send_prn = glue_dfu_send_prn;
}

// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_dfu_system_reset(void *self) {
    (void)self;
    board_pinetime_system_reset();
    return EDGE_OK;
}

void pinetime_make_dfu_system_port(ble_dfu_system_port_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->system_reset = glue_dfu_system_reset;
}

/* Firmware Validator Port */
// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_val_read_word(void *self, uint32_t address, uint32_t *value) {
    const pinetime_glue_state_t *state = (const pinetime_glue_state_t *)self;
    if (state == NULL || value == NULL) {
        return EDGE_EINVAL;
    }
    (void)address;
    *value = state->validator_word;
    return EDGE_OK;
}

// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_val_write_word(void *self, uint32_t address, uint32_t value) {
    pinetime_glue_state_t *state = (pinetime_glue_state_t *)self;
    if (state == NULL) {
        return EDGE_EINVAL;
    }
    (void)address;
    state->validator_word = value;
    return EDGE_OK;
}

void pinetime_make_validator_port(firmware_validator_port_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->read_word = glue_val_read_word;
    out->write_word = glue_val_write_word;
    out->system_reset = glue_dfu_system_reset;
}

/* Watch UI Ports */
static edge_status_t glue_ui_set_brightness(void *self, uint8_t level) {
    return glue_set_brightness(self, level);
}

static edge_status_t glue_ui_clear_screen(void *self, uint16_t color) {
    (void)self;
    (void)color;
    return EDGE_OK;
}

static edge_status_t glue_ui_draw_bitmap(void *self, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                         const uint16_t *pixels) {
    (void)self;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)pixels;
    return EDGE_OK;
}

static edge_status_t glue_ui_set_power(void *self, bool display_on) {
    return glue_display_sleep(self, !display_on);
}

void pinetime_make_ui_display_port(watch_ui_display_port_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->set_brightness = glue_ui_set_brightness;
    out->clear_screen = glue_ui_clear_screen;
    out->draw_bitmap = glue_ui_draw_bitmap;
    out->set_power_mode = glue_ui_set_power;
}

static edge_status_t glue_ui_get_status(void *self, watch_ui_status_data_t *out_status) {
    pinetime_glue_state_t *state = (pinetime_glue_state_t *)self;
    if (state == NULL || out_status == NULL) {
        return EDGE_EINVAL;
    }
    *out_status = (__typeof__(*out_status)){0};
    if (state->time_app != NULL) {
        watch_datetime_t dt = watch_time_get(state->time_app);
        out_status->hour = dt.hour;
        out_status->minute = dt.minute;
        out_status->second = dt.second;
        out_status->day = dt.day;
        out_status->month = dt.month;
        out_status->year = dt.year;
        out_status->day_of_week = dt.day_of_week;
    }
    out_status->battery_percent = state->battery_pct;
    out_status->battery_charging = state->battery_charging;
    out_status->ble_connected = true;
    if (state->hr_app != NULL) {
        out_status->heart_rate_bpm = heart_rate_get_bpm(state->hr_app);
    }
    if (state->step_app != NULL) {
        out_status->step_count = step_counter_get_steps(state->step_app);
    }

    out_status->weather_temp_c = 22;
    return EDGE_OK;
}

void pinetime_make_ui_status_port(watch_ui_status_port_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->get_status_data = glue_ui_get_status;
}

static edge_status_t glue_metronome_vibrate(void *self, uint32_t duration_ms) {
    (void)self;
    (void)duration_ms;
    return EDGE_OK;
}

void pinetime_make_metronome_motor_port(metronome_motor_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->run_duration_ms = glue_metronome_vibrate;
}

static uint32_t glue_dice_get_random(void *self) {
    pinetime_glue_state_t *state = (pinetime_glue_state_t *)self;
    if (state != NULL) {
        state->rtc_ticks = (state->rtc_ticks * 1103515245u + 12345u) & 0x7FFFFFFFu;
        return state->rtc_ticks;
    }
    return 42u;
}

void pinetime_make_dice_entropy_port(dice_entropy_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->get_entropy_seed = glue_dice_get_random;
}

static edge_status_t glue_dice_vibrate(void *self) {
    (void)self;
    return EDGE_OK;
}

void pinetime_make_dice_motor_port(dice_motor_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->vibrate = glue_dice_vibrate;
}

static edge_status_t glue_flashlight_set_brightness(void *self, uint8_t percent) {
    pinetime_glue_state_t *state = (pinetime_glue_state_t *)self;
    if (state == NULL) {
        return EDGE_EINVAL;
    }
    state->display_brightness = percent;
    return EDGE_OK;
}

static edge_status_t glue_flashlight_set_color(void *self, bool is_white) {
    pinetime_glue_state_t *state = (pinetime_glue_state_t *)self;
    if (state == NULL) {
        return EDGE_EINVAL;
    }
    state->screen_white = is_white;
    return EDGE_OK;
}

void pinetime_make_flashlight_display_port(flashlight_display_if_t *out,
                                           pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->set_brightness = glue_flashlight_set_brightness;
    out->set_screen_color = glue_flashlight_set_color;
}

static edge_status_t glue_flashlight_set_wake_lock(void *self, bool locked) {
    pinetime_glue_state_t *state = (pinetime_glue_state_t *)self;
    if (state == NULL) {
        return EDGE_EINVAL;
    }
    state->wake_locked = locked;
    return EDGE_OK;
}

void pinetime_make_flashlight_system_port(flashlight_system_if_t *out,
                                          pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->set_wake_lock = glue_flashlight_set_wake_lock;
}

static edge_status_t glue_paddle_vibrate(void *self, uint16_t duration_ms) {
    (void)self;
    (void)duration_ms;
    return EDGE_OK;
}

void pinetime_make_game_paddle_motor_port(paddle_motor_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->vibrate = glue_paddle_vibrate;
}

static uint32_t glue_paddle_get_random(void *self) {
    pinetime_glue_state_t *state = (pinetime_glue_state_t *)self;
    if (state != NULL) {
        state->rtc_ticks = (state->rtc_ticks * 1103515245u + 12345u) & 0x7FFFFFFFu;
        return state->rtc_ticks;
    }
    return 42u;
}

void pinetime_make_game_paddle_entropy_port(paddle_entropy_if_t *out,
                                            pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->get_random = glue_paddle_get_random;
}

static uint32_t glue_twos_get_random(void *self) {
    pinetime_glue_state_t *state = (pinetime_glue_state_t *)self;
    if (state != NULL) {
        state->rtc_ticks = (state->rtc_ticks * 1103515245u + 12345u) & 0x7FFFFFFFu;
        return state->rtc_ticks;
    }
    return 42u;
}

void pinetime_make_game_twos_entropy_port(twos_entropy_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->get_random = glue_twos_get_random;
}

static edge_status_t glue_paint_fill_rect(void *self, int16_t x, int16_t y, uint16_t w, uint16_t h,
                                          uint16_t rgb565) {
    (void)self;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)rgb565;
    return EDGE_OK;
}

static edge_status_t glue_paint_clear(void *self, uint16_t rgb565) {
    (void)self;
    (void)rgb565;
    return EDGE_OK;
}

void pinetime_make_paint_display_port(paint_display_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->fill_rect = glue_paint_fill_rect;
    out->clear = glue_paint_clear;
}

static edge_status_t glue_paint_vibrate(void *self, uint16_t duration_ms) {
    (void)self;
    (void)duration_ms;
    return EDGE_OK;
}

void pinetime_make_paint_motor_port(paint_motor_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->vibrate = glue_paint_vibrate;
}
