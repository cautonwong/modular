#include <stddef.h>
#include <stdint.h>
static void copy_bytes(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
}
#include "ble_weather/ble_weather.h"
#include "edge/events.h"
#include "edge/modules.h"

#define BLE_WEATHER_MSG_CURRENT 0u
#define BLE_WEATHER_MSG_FORECAST 1u

static uint64_t read_u64_le(const uint8_t *p) {
    uint64_t v = 0;
    for (size_t i = 0; i < 8u; i++) {
        v |= ((uint64_t)p[i]) << (i * 8u);
    }
    return v;
}

static int16_t read_i16_le(const uint8_t *p) {
    uint16_t v = (uint16_t)p[0] | ((uint16_t)p[1] << 8u);
    return (int16_t)v;
}

int16_t ble_weather_celsius(int16_t raw_temp) {
    if (raw_temp >= 0) {
        return (raw_temp + 50) / 100;
    }
    return (raw_temp - 50) / 100;
}

int16_t ble_weather_fahrenheit(int16_t raw_temp) {
    int32_t f_scaled = (int32_t)raw_temp * 9 / 5 + 3200;
    if (f_scaled >= 0) {
        return (int16_t)((f_scaled + 50) / 100);
    }
    return (int16_t)((f_scaled - 50) / 100);
}

static edge_status_t weather_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

void ble_weather_init(ble_weather_t *self, const ble_weather_time_port_t *time_port,
                      edge_event_sink_t *event_sink) {
    ble_weather_construct(self, EDGE_MOD_BLE_WEATHER, 2u, time_port, event_sink);
}

void ble_weather_construct(ble_weather_t *self, uint32_t module_id, uint32_t priority,
                           const ble_weather_time_port_t *time_port,
                           edge_event_sink_t *event_sink) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 1u,
        .budget = 0u,
        .next_due = 0u,
        .poll = weather_poll,
        .on_event = NULL,
        .power_off = NULL,
        .private_data = self,
    };
    if (time_port != NULL) {
        self->time_port = *time_port;
    }
    self->event_sink = event_sink;
    self->current.sunrise = -1;
    self->current.sunset = -1;
}

edge_status_t ble_weather_process_packet(ble_weather_t *self, const uint8_t *data, size_t len) {
    if (self == NULL || data == NULL || len < 2u) {
        return EDGE_EINVAL;
    }

    uint8_t msg_type = data[0];
    uint8_t version = data[1];

    if (msg_type == BLE_WEATHER_MSG_CURRENT) {
        if (len < 49u) {
            return EDGE_EINVAL;
        }
        self->current.timestamp = read_u64_le(&data[2]);
        self->current.temp_raw = read_i16_le(&data[10]);
        self->current.min_temp_raw = read_i16_le(&data[12]);
        self->current.max_temp_raw = read_i16_le(&data[14]);
        copy_bytes(self->current.location, &data[16], BLE_WEATHER_LOCATION_MAX_LEN);
        self->current.location[BLE_WEATHER_LOCATION_MAX_LEN] = '\0';
        self->current.icon_id = data[48];

        self->current.sunrise = -1;
        self->current.sunset = -1;
        if (version >= 1u && len >= 53u) {
            int16_t sr = read_i16_le(&data[49]);
            int16_t ss = read_i16_le(&data[51]);
            if ((sr >= -2 && sr <= 1439) && (ss >= -2 && ss <= 1439)) {
                self->current.sunrise = sr;
                self->current.sunset = ss;
            }
        }
        self->current.valid = true;

        if (self->event_sink != NULL) {
            edge_event_t ev = {
                .id = EDGE_EVT_WATCH_WEATHER_UPDATED,
                .source = EDGE_MOD_BLE_WEATHER,
                .arg0 = (uint32_t)(uint16_t)self->current.temp_raw,
                .arg1 = 0u,
                .timestamp = 0u,
            };
            edge_event_sink_push_isr(self->event_sink, &ev);
        }
        return EDGE_OK;
    } else if (msg_type == BLE_WEATHER_MSG_FORECAST) {
        if (len < 11u) {
            return EDGE_EINVAL;
        }
        self->forecast.timestamp = read_u64_le(&data[2]);
        uint8_t nb_days = data[10];
        if (nb_days > BLE_WEATHER_MAX_FORECAST_DAYS) {
            nb_days = BLE_WEATHER_MAX_FORECAST_DAYS;
        }
        self->forecast.nb_days = nb_days;

        size_t expected_len = 11u + (size_t)nb_days * 5u;
        if (len < expected_len) {
            return EDGE_EINVAL;
        }

        for (uint8_t i = 0; i < BLE_WEATHER_MAX_FORECAST_DAYS; i++) {
            if (i < nb_days) {
                size_t offset = 11u + (size_t)i * 5u;
                self->forecast.days[i].min_temp_raw = read_i16_le(&data[offset]);
                self->forecast.days[i].max_temp_raw = read_i16_le(&data[offset + 2u]);
                self->forecast.days[i].icon_id = data[offset + 4u];
                self->forecast.days[i].valid = true;
            } else {
                self->forecast.days[i].valid = false;
            }
        }
        self->forecast.valid = true;

        if (self->event_sink != NULL) {
            edge_event_t ev = {
                .id = EDGE_EVT_WATCH_WEATHER_UPDATED,
                .source = EDGE_MOD_BLE_WEATHER,
                .arg0 = (uint32_t)nb_days,
                .arg1 = 0u,
                .timestamp = 0u,
            };
            edge_event_sink_push_isr(self->event_sink, &ev);
        }
        return EDGE_OK;
    }

    return EDGE_ENOTSUP;
}

bool ble_weather_get_current(const ble_weather_t *self, ble_weather_current_t *out_current) {
    if (self == NULL || !self->current.valid) {
        return false;
    }
    if (out_current != NULL) {
        *out_current = self->current;
    }
    return true;
}

bool ble_weather_get_forecast(const ble_weather_t *self, ble_weather_forecast_t *out_forecast) {
    if (self == NULL || !self->forecast.valid) {
        return false;
    }
    if (out_forecast != NULL) {
        *out_forecast = self->forecast;
    }
    return true;
}

bool ble_weather_is_night(const ble_weather_t *self) {
    if (self == NULL || !self->current.valid) {
        return false;
    }
    if (self->current.sunrise == -1 || self->current.sunset == -1) {
        return false;
    }
    if (self->current.sunrise == -2) {
        return true; /* Polar night */
    }
    if (self->time_port.get_minute_of_day == NULL) {
        return false;
    }
    uint32_t current_min = self->time_port.get_minute_of_day(self->time_port.self);
    if (self->current.sunset == -2) {
        return current_min < (uint32_t)self->current.sunrise;
    }
    return (current_min < (uint32_t)self->current.sunrise ||
            current_min >= (uint32_t)self->current.sunset);
}
