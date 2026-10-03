#include "ble_services/ble_services.h"
#include "edge/events.h"

edge_status_t cts_decode_datetime(const uint8_t *buf, size_t len, cts_datetime_t *out_dt) {
    if (buf == NULL || out_dt == NULL || len < 10u) {
        return EDGE_EINVAL;
    }

    out_dt->year = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8u);
    out_dt->month = buf[2];
    out_dt->day_of_month = buf[3];
    out_dt->hours = buf[4];
    out_dt->minutes = buf[5];
    out_dt->seconds = buf[6];
    out_dt->day_of_week = buf[7];
    out_dt->fractions256 = buf[8];
    out_dt->adjust_reason = buf[9];

    if (out_dt->month < 1u || out_dt->month > 12u || out_dt->day_of_month < 1u ||
        out_dt->day_of_month > 31u || out_dt->hours > 23u || out_dt->minutes > 59u ||
        out_dt->seconds > 59u) {
        return EDGE_EINVAL;
    }

    return EDGE_OK;
}

edge_status_t cts_encode_datetime(const cts_datetime_t *dt, uint8_t *out_buf, size_t max_len,
                                  size_t *out_len) {
    if (dt == NULL || out_buf == NULL || max_len < 10u) {
        return EDGE_EINVAL;
    }

    out_buf[0] = (uint8_t)(dt->year & 0xFFu);
    out_buf[1] = (uint8_t)((dt->year >> 8u) & 0xFFu);
    out_buf[2] = dt->month;
    out_buf[3] = dt->day_of_month;
    out_buf[4] = dt->hours;
    out_buf[5] = dt->minutes;
    out_buf[6] = dt->seconds;
    out_buf[7] = dt->day_of_week;
    out_buf[8] = dt->fractions256;
    out_buf[9] = dt->adjust_reason;

    if (out_len != NULL) {
        *out_len = 10u;
    }
    return EDGE_OK;
}

edge_status_t cts_decode_local_time(const uint8_t *buf, size_t len, cts_timezone_t *out_tz) {
    if (buf == NULL || out_tz == NULL || len < 2u) {
        return EDGE_EINVAL;
    }

    out_tz->timezone = (int8_t)buf[0];
    out_tz->dst_offset = (int8_t)buf[1];
    return EDGE_OK;
}

edge_status_t cts_encode_local_time(const cts_timezone_t *tz, uint8_t *out_buf, size_t max_len,
                                    size_t *out_len) {
    if (tz == NULL || out_buf == NULL || max_len < 2u) {
        return EDGE_EINVAL;
    }

    out_buf[0] = (uint8_t)tz->timezone;
    out_buf[1] = (uint8_t)tz->dst_offset;

    if (out_len != NULL) {
        *out_len = 2u;
    }
    return EDGE_OK;
}

edge_status_t hrs_encode_measurement(uint8_t bpm, uint8_t *out_buf, size_t max_len,
                                     size_t *out_len) {
    if (out_buf == NULL || max_len < 2u) {
        return EDGE_EINVAL;
    }

    out_buf[0] = 0u; /* Flags: 8-bit format */
    out_buf[1] = bpm;

    if (out_len != NULL) {
        *out_len = 2u;
    }
    return EDGE_OK;
}

edge_status_t bas_encode_battery_level(uint8_t percent, uint8_t *out_buf, size_t max_len,
                                       size_t *out_len) {
    if (out_buf == NULL || max_len < 1u) {
        return EDGE_EINVAL;
    }

    if (percent > 100u) {
        percent = 100u;
    }

    out_buf[0] = percent;

    if (out_len != NULL) {
        *out_len = 1u;
    }
    return EDGE_OK;
}

static edge_status_t ble_services_on_event(edge_module_t *module, const edge_event_t *event) {
    ble_services_app_t *self = (ble_services_app_t *)edge_module_data(module);
    if (self == NULL || event == NULL) {
        return EDGE_EINVAL;
    }

    switch (event->id) {
    case EDGE_EVT_WATCH_HEART_RATE: {
        self->current_bpm = (uint8_t)event->arg0;
        if (self->is_connected && self->hrs_notify_enabled && self->gatt_server != NULL &&
            self->gatt_server->notify != NULL) {
            uint8_t buf[2];
            size_t len = 0;
            (void)hrs_encode_measurement(self->current_bpm, buf, sizeof(buf), &len);
            (void)self->gatt_server->notify(self->gatt_server->self, self->hrs_val_handle, buf,
                                            len);
        }
        break;
    }

    case EDGE_EVT_WATCH_BATTERY: {
        self->battery_percent = (uint8_t)event->arg0;
        if (self->is_connected && self->bas_notify_enabled && self->gatt_server != NULL &&
            self->gatt_server->notify != NULL) {
            uint8_t buf[1];
            size_t len = 0;
            (void)bas_encode_battery_level(self->battery_percent, buf, sizeof(buf), &len);
            (void)self->gatt_server->notify(self->gatt_server->self, self->bas_val_handle, buf,
                                            len);
        }
        break;
    }

    case EDGE_EVT_WATCH_BLE_CONNECTED:
        self->is_connected = true;
        break;

    case EDGE_EVT_WATCH_BLE_DISCONNECTED:
        self->is_connected = false;
        self->hrs_notify_enabled = false;
        self->bas_notify_enabled = false;
        break;

    default:
        break;
    }

    return EDGE_OK;
}

static edge_status_t ble_services_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t ble_services_power_off(edge_module_t *module) {
    ble_services_app_t *self = (ble_services_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return ble_services_shutdown(self);
}

void ble_services_construct(ble_services_app_t *self, uint32_t module_id, uint32_t priority,
                            const ble_gatt_server_if_t *gatt_server,
                            const ble_time_sink_if_t *time_sink) {
    if (self == NULL) {
        return;
    }

    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 1u,
        .budget = 0u,
        .next_due = 0u,
        .poll = ble_services_poll,
        .on_event = ble_services_on_event,
        .power_off = ble_services_power_off,
        .private_data = self,
    };
    self->gatt_server = gatt_server;
    self->time_sink = time_sink;

    self->hrs_val_handle = 0x0010u;
    self->bas_val_handle = 0x0020u;
    self->cts_time_val_handle = 0x0030u;
    self->cts_local_time_val_handle = 0x0031u;
    self->ias_alert_val_handle = 0x0040u;
    self->dis_mfr_val_handle = 0x0050u;
    self->dis_model_val_handle = 0x0051u;
    self->dis_serial_val_handle = 0x0052u;
    self->dis_fw_val_handle = 0x0053u;
    self->dis_hw_val_handle = 0x0054u;
    self->dis_sw_val_handle = 0x0055u;

    self->alert_sink = NULL;
    self->is_connected = false;
    self->hrs_notify_enabled = false;
    self->bas_notify_enabled = false;

    self->current_bpm = 0u;
    self->battery_percent = 100u;
    self->current_alert_level = 0u;
    self->current_dt.year = 2026u;
    self->current_dt.month = 1u;
    self->current_dt.day_of_month = 1u;
    self->current_dt.hours = 0u;
    self->current_dt.minutes = 0u;
    self->current_dt.seconds = 0u;
    self->current_dt.day_of_week = 4u;
    self->current_dt.fractions256 = 0u;
    self->current_dt.adjust_reason = 0u;
    self->current_tz.timezone = 0;
    self->current_tz.dst_offset = 0;
}

void ble_services_set_alert_sink(ble_services_app_t *self, const ble_alert_sink_if_t *alert_sink) {
    if (self != NULL) {
        self->alert_sink = alert_sink;
    }
}

edge_status_t ble_services_init(ble_services_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->is_connected = false;
    self->hrs_notify_enabled = false;
    self->bas_notify_enabled = false;
    return EDGE_OK;
}

edge_status_t ble_services_shutdown(ble_services_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->is_connected = false;
    self->hrs_notify_enabled = false;
    self->bas_notify_enabled = false;
    return EDGE_OK;
}

void ble_services_set_connected(ble_services_app_t *self, bool connected) {
    if (self == NULL) {
        return;
    }
    self->is_connected = connected;
    if (!connected) {
        self->hrs_notify_enabled = false;
        self->bas_notify_enabled = false;
    }
}

void ble_services_set_hrs_notify(ble_services_app_t *self, bool enabled) {
    if (self != NULL) {
        self->hrs_notify_enabled = enabled;
    }
}

void ble_services_set_bas_notify(ble_services_app_t *self, bool enabled) {
    if (self != NULL) {
        self->bas_notify_enabled = enabled;
    }
}

static edge_status_t copy_str(const char *src, uint8_t *out_buf, size_t max_len, size_t *out_len) {
    if (src == NULL || out_buf == NULL) {
        return EDGE_EINVAL;
    }
    size_t slen = 0;
    while (src[slen] != '\0') {
        slen++;
    }
    if (max_len < slen) {
        return EDGE_ENOSPC;
    }
    for (size_t i = 0; i < slen; i++) {
        out_buf[i] = (uint8_t)src[i];
    }
    if (out_len != NULL) {
        *out_len = slen;
    }
    return EDGE_OK;
}

edge_status_t ble_services_handle_gatt_read(ble_services_app_t *self, uint16_t attr_handle,
                                            uint8_t *out_buf, size_t max_len, size_t *out_len) {
    if (self == NULL || out_buf == NULL) {
        return EDGE_EINVAL;
    }

    if (attr_handle == self->hrs_val_handle) {
        return hrs_encode_measurement(self->current_bpm, out_buf, max_len, out_len);
    }
    if (attr_handle == self->bas_val_handle) {
        return bas_encode_battery_level(self->battery_percent, out_buf, max_len, out_len);
    }
    if (attr_handle == self->cts_time_val_handle) {
        return cts_encode_datetime(&self->current_dt, out_buf, max_len, out_len);
    }
    if (attr_handle == self->cts_local_time_val_handle) {
        return cts_encode_local_time(&self->current_tz, out_buf, max_len, out_len);
    }
    if (attr_handle == self->dis_mfr_val_handle) {
        return copy_str("Pine64", out_buf, max_len, out_len);
    }
    if (attr_handle == self->dis_model_val_handle) {
        return copy_str("PineTime", out_buf, max_len, out_len);
    }
    if (attr_handle == self->dis_serial_val_handle) {
        return copy_str("00000001", out_buf, max_len, out_len);
    }
    if (attr_handle == self->dis_fw_val_handle) {
        return copy_str("1.14.0", out_buf, max_len, out_len);
    }
    if (attr_handle == self->dis_hw_val_handle) {
        return copy_str("1.0.0", out_buf, max_len, out_len);
    }
    if (attr_handle == self->dis_sw_val_handle) {
        return copy_str("modular-1.0", out_buf, max_len, out_len);
    }

    return EDGE_ENOENT;
}

edge_status_t ble_services_handle_gatt_write(ble_services_app_t *self, uint16_t attr_handle,
                                             const uint8_t *data, size_t len) {
    if (self == NULL || data == NULL) {
        return EDGE_EINVAL;
    }

    if (attr_handle == self->cts_time_val_handle) {
        cts_datetime_t dt;
        edge_status_t status = cts_decode_datetime(data, len, &dt);
        if (status != EDGE_OK) {
            return status;
        }

        self->current_dt = dt;
        if (self->time_sink != NULL && self->time_sink->set_time != NULL) {
            return self->time_sink->set_time(self->time_sink->self, dt.year, dt.month,
                                             dt.day_of_month, dt.hours, dt.minutes, dt.seconds);
        }
        return EDGE_OK;
    }

    if (attr_handle == self->cts_local_time_val_handle) {
        cts_timezone_t tz;
        edge_status_t status = cts_decode_local_time(data, len, &tz);
        if (status != EDGE_OK) {
            return status;
        }
        self->current_tz = tz;
        return EDGE_OK;
    }

    if (attr_handle == self->ias_alert_val_handle) {
        if (len < 1u) {
            return EDGE_EINVAL;
        }
        self->current_alert_level = data[0];
        if (self->alert_sink != NULL && self->alert_sink->on_alert_level != NULL) {
            return self->alert_sink->on_alert_level(self->alert_sink->self,
                                                    self->current_alert_level);
        }
        return EDGE_OK;
    }

    return EDGE_ENOENT;
}
