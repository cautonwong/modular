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

// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_read_touch(void *self, touch_raw_info_t *out_info) {
    const watch_host_glue_state_t *state = (const watch_host_glue_state_t *)self;
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

void watch_host_make_touch_port(touch_input_if_t *out, watch_host_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->read_touch = glue_read_touch;
    out->sleep = glue_touch_sleep;
}

/* Display power port adapter */
// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_set_brightness(void *self, uint8_t percent) {
    watch_host_glue_state_t *state = (watch_host_glue_state_t *)self;
    if (state == NULL) {
        return EDGE_EINVAL;
    }
    state->display_brightness = percent;
    return EDGE_OK;
}

// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_display_sleep(void *self, bool enable) {
    watch_host_glue_state_t *state = (watch_host_glue_state_t *)self;
    if (state == NULL) {
        return EDGE_EINVAL;
    }
    state->display_sleep = enable;
    return EDGE_OK;
}

void watch_host_make_display_port(watch_power_display_if_t *out, watch_host_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->set_brightness = glue_set_brightness;
    out->sleep = glue_display_sleep;
}

/* PPG sensor port adapter */
// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_read_ppg(void *self, uint16_t *out_hrs, uint16_t *out_als) {
    watch_host_glue_state_t *state = (watch_host_glue_state_t *)self;
    if (state == NULL || out_hrs == NULL) {
        return EDGE_EINVAL;
    }
    *out_hrs = state->ppg_sample_val;
    if (out_als != NULL) {
        *out_als = 120u;
    }
    state->ppg_read_count++;
    return EDGE_OK;
}

void watch_host_make_ppg_port(ppg_sensor_if_t *out, watch_host_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->read_sample = glue_read_ppg;
}

/* IMU sensor port adapter */
// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_read_imu(void *self, int16_t *out_x, int16_t *out_y, int16_t *out_z) {
    const watch_host_glue_state_t *state = (const watch_host_glue_state_t *)self;
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

void watch_host_make_imu_port(imu_sensor_if_t *out, watch_host_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->read_accel = glue_read_imu;
}

/* RTC clock port adapter */
// cppcheck-suppress constParameterCallback ; signature fixed by the consumer port
static edge_status_t glue_get_rtc_counter(void *self, uint32_t *out_counter) {
    const watch_host_glue_state_t *state = (const watch_host_glue_state_t *)self;
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

void watch_host_make_rtc_port(rtc_clock_if_t *out, watch_host_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->get_counter = glue_get_rtc_counter;
    out->get_tick_frequency = glue_get_tick_frequency;
}

/* BLE GATT server port adapter */
static edge_status_t glue_gatt_notify(void *self, uint16_t char_handle, const uint8_t *data,
                                      size_t len) {
    watch_host_glue_state_t *state = (watch_host_glue_state_t *)self;
    if (state == NULL) {
        return EDGE_EINVAL;
    }
    state->last_ble_handle = char_handle;
    state->last_ble_len = len;
    if (data != NULL && len <= sizeof(state->last_ble_data)) {
        copy_bytes(state->last_ble_data, data, len);
    }
    state->ble_notify_count++;
    return EDGE_OK;
}

void watch_host_make_gatt_port(ble_gatt_server_if_t *out, watch_host_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->notify = glue_gatt_notify;
}

/* BLE Time Sink port adapter */
static edge_status_t glue_set_time(void *self, uint16_t year, uint8_t month, uint8_t day,
                                   uint8_t hour, uint8_t minute, uint8_t second) {
    watch_host_glue_state_t *state = (watch_host_glue_state_t *)self;
    if (state == NULL || state->time_app == NULL) {
        return EDGE_EINVAL;
    }
    return watch_time_set(state->time_app, year, month, day, hour, minute, second);
}

void watch_host_make_time_sink_port(ble_time_sink_if_t *out, watch_host_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    out->self = state;
    out->set_time = glue_set_time;
}
