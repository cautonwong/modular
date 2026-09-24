#include "edge/errors.h"
#include "vesc_comm/vesc_comm.h"
#include <string.h>

static void buffer_append_int16(uint8_t *buffer, int16_t number, size_t *index) {
    buffer[(*index)++] = (uint8_t)(number >> 8);
    buffer[(*index)++] = (uint8_t)number;
}

static void buffer_append_int32(uint8_t *buffer, int32_t number, size_t *index) {
    buffer[(*index)++] = (uint8_t)(number >> 24);
    buffer[(*index)++] = (uint8_t)(number >> 16);
    buffer[(*index)++] = (uint8_t)(number >> 8);
    buffer[(*index)++] = (uint8_t)number;
}

static void buffer_append_uint32(uint8_t *buffer, uint32_t number, size_t *index) {
    buffer[(*index)++] = (uint8_t)(number >> 24);
    buffer[(*index)++] = (uint8_t)(number >> 16);
    buffer[(*index)++] = (uint8_t)(number >> 8);
    buffer[(*index)++] = (uint8_t)number;
}

static void buffer_append_float16(uint8_t *buffer, float number, float scale, size_t *index) {
    buffer_append_int16(buffer, (int16_t)(number * scale), index);
}

static void buffer_append_float32(uint8_t *buffer, float number, float scale, size_t *index) {
    buffer_append_int32(buffer, (int32_t)(number * scale), index);
}

static int32_t buffer_get_int32(const uint8_t *buffer, size_t *index) {
    int32_t res =
        (int32_t)(((uint32_t)buffer[*index] << 24) | ((uint32_t)buffer[*index + 1] << 16) |
                  ((uint32_t)buffer[*index + 2] << 8) | (uint32_t)buffer[*index + 3]);
    *index += 4;
    return res;
}

static float buffer_get_float32(const uint8_t *buffer, float scale, size_t *index) {
    return (float)buffer_get_int32(buffer, index) / scale;
}

edge_status_t vesc_comm_process_command(vesc_comm_t *self, const uint8_t *data, size_t len) {
    if (!self || !data || len == 0) {
        return EDGE_EINVAL;
    }

    uint8_t cmd_id = data[0];
    size_t ind = 1;

    switch (cmd_id) {
    case COMM_FW_VERSION: {
        const vesc_identity_t *id = self->identity;
        if (id == (void *)0 || id->hw_name == (void *)0 || id->fw_name == (void *)0 ||
            id->uuid == (void *)0) {
            return EDGE_EINVAL;
        }

        /* 1 id + 1 major + 1 minor + 12 uuid + 8 flags + 4 crc = 27 fixed bytes. */
        uint8_t resp[80];
        size_t hw_len = strlen(id->hw_name) + 1;
        size_t fw_len = strlen(id->fw_name) + 1;
        if (hw_len + fw_len > sizeof(resp) - 27u) {
            return EDGE_EINVAL;
        }

        size_t resp_len = 0;
        resp[resp_len++] = COMM_FW_VERSION;
        resp[resp_len++] = id->fw_version_major;
        resp[resp_len++] = id->fw_version_minor;

        memcpy(resp + resp_len, id->hw_name, hw_len);
        resp_len += hw_len;

        memcpy(resp + resp_len, id->uuid, 12u);
        resp_len += 12u;

        /* Field order from the reference's COMM_FW_VERSION. The nrf_flags byte in
         * the middle is easy to miss, and dropping it shifts everything after it. */
        resp[resp_len++] = id->pairing_done;
        resp[resp_len++] = id->fw_test_version;
        resp[resp_len++] = id->hw_type;
        resp[resp_len++] = id->custom_cfg_num;
        resp[resp_len++] = id->phase_filters;
        resp[resp_len++] = id->qmlui_hw;
        resp[resp_len++] = id->qmlui_app;
        resp[resp_len++] = id->nrf_flags;

        memcpy(resp + resp_len, id->fw_name, fw_len);
        resp_len += fw_len;

        buffer_append_uint32(resp, id->hw_crc, &resp_len);

        return vesc_comm_send_packet(self, resp, resp_len);
    }

    case COMM_GET_VALUES: {
        vesc_values_t val;
        memset(&val, 0, sizeof(val));
        if (self->motor && self->motor->get_values) {
            self->motor->get_values(self->motor->self, &val);
        }

        uint8_t resp[128];
        size_t resp_len = 0;
        resp[resp_len++] = COMM_GET_VALUES;

        buffer_append_float16(resp, val.temp_mos, 1e1f, &resp_len);
        buffer_append_float16(resp, val.temp_motor, 1e1f, &resp_len);
        buffer_append_float32(resp, val.current_motor, 1e2f, &resp_len);
        buffer_append_float32(resp, val.current_in, 1e2f, &resp_len);
        buffer_append_float32(resp, val.id, 1e2f, &resp_len);
        buffer_append_float32(resp, val.iq, 1e2f, &resp_len);
        buffer_append_float16(resp, val.duty_now, 1e3f, &resp_len);
        buffer_append_float32(resp, val.rpm, 1e0f, &resp_len);
        buffer_append_float16(resp, val.v_in, 1e1f, &resp_len);
        buffer_append_float32(resp, val.amp_hours, 1e4f, &resp_len);
        buffer_append_float32(resp, val.amp_hours_charged, 1e4f, &resp_len);
        buffer_append_float32(resp, val.watt_hours, 1e4f, &resp_len);
        buffer_append_float32(resp, val.watt_hours_charged, 1e4f, &resp_len);
        buffer_append_int32(resp, val.tachometer, &resp_len);
        buffer_append_int32(resp, val.tachometer_abs, &resp_len);
        resp[resp_len++] = (uint8_t)val.fault_code;
        buffer_append_float32(resp, val.pid_pos_now, 1e6f, &resp_len);
        /* Reference takes this from the app configuration, not a constant. */
        resp[resp_len++] = self->identity != (void *)0 ? self->identity->controller_id : 0u;

        return vesc_comm_send_packet(self, resp, resp_len);
    }

    case COMM_SET_DUTY: {
        if (len < 5) {
            return EDGE_EINVAL;
        }
        float duty = buffer_get_float32(data, 1e5f, &ind);
        if (self->motor && self->motor->set_duty) {
            return self->motor->set_duty(self->motor->self, duty);
        }
        return EDGE_OK;
    }

    case COMM_SET_CURRENT: {
        if (len < 5) {
            return EDGE_EINVAL;
        }
        float current = buffer_get_float32(data, 1e3f, &ind);
        if (self->motor && self->motor->set_current) {
            return self->motor->set_current(self->motor->self, current);
        }
        return EDGE_OK;
    }

    case COMM_SET_CURRENT_BRAKE: {
        if (len < 5) {
            return EDGE_EINVAL;
        }
        float current = buffer_get_float32(data, 1e3f, &ind);
        if (self->motor && self->motor->set_current_brake) {
            return self->motor->set_current_brake(self->motor->self, current);
        }
        return EDGE_OK;
    }

    case COMM_SET_RPM: {
        if (len < 5) {
            return EDGE_EINVAL;
        }
        float rpm = (float)buffer_get_int32(data, &ind);
        if (self->motor && self->motor->set_rpm) {
            return self->motor->set_rpm(self->motor->self, rpm);
        }
        return EDGE_OK;
    }

    case COMM_SET_POS: {
        if (len < 5) {
            return EDGE_EINVAL;
        }
        float pos = buffer_get_float32(data, 1e6f, &ind);
        if (self->motor && self->motor->set_pos) {
            return self->motor->set_pos(self->motor->self, pos);
        }
        return EDGE_OK;
    }

    case COMM_ALIVE:
        return EDGE_OK;

    default:
        return EDGE_ENOTSUP;
    }
}
