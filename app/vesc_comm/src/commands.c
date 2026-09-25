#include "edge/errors.h"
#include "vesc_comm/vesc_comm.h"
#include "vesc_comm_internal.h"
#include <math.h>
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

/**
 * The "auto" encoding: IEEE-754 single precision bit pattern, mantissa normalised
 * to [0.5, 1) and exponent biased by 126. Copied from the reference's
 * util/buffer.c; infra/vesc_buffer carries the same algorithm, and
 * tests/test_app_vesc_comm.c cross-checks the two so they cannot drift apart
 * (an app may not depend on infra, so this cannot simply be reused).
 */
static void buffer_append_float32_auto(uint8_t *buffer, float number, size_t *index) {
    if (fabsf(number) < 1.5e-38) {
        number = 0.0f;
    }

    int e = 0;
    float sig = frexpf(number, &e);
    float sig_abs = fabsf(sig);
    uint32_t sig_i = 0u;

    if (sig_abs >= 0.5f) {
        sig_i = (uint32_t)((sig_abs - 0.5f) * 2.0f * 8388608.0f);
        e += 126;
    }

    uint32_t res = ((uint32_t)(e & 0xFF) << 23) | (sig_i & 0x7FFFFFu);
    if (sig < 0.0f) {
        res |= 1u << 31;
    }

    buffer_append_uint32(buffer, res, index);
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

/*
 * Send what was built in the caller-provided reply buffer. The size check lives
 * here and nowhere else: a handler that grows a reply past the buffer is refused
 * rather than allowed to run off the end of the struct.
 */
static edge_status_t send_reply(vesc_comm_t *self, size_t len) {
    if (len > sizeof(self->cmd_reply_buf)) {
        return EDGE_ENOSPC;
    }
    return vesc_comm_send_packet(self, self->cmd_reply_buf, len);
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
        uint8_t *resp = self->cmd_reply_buf;
        size_t hw_len = strlen(id->hw_name) + 1;
        size_t fw_len = strlen(id->fw_name) + 1;
        if (hw_len + fw_len > sizeof(self->cmd_reply_buf) - 27u) {
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

        return send_reply(self, resp_len);
    }

    case COMM_GET_VALUES:
    case COMM_GET_VALUES_SELECTIVE: {
        uint32_t mask = VESC_VALUES_MASK_ALL;
        if (cmd_id == COMM_GET_VALUES_SELECTIVE) {
            if (len < 5) {
                return EDGE_EINVAL;
            }
            mask = ((uint32_t)data[1] << 24) | ((uint32_t)data[2] << 16) |
                   ((uint32_t)data[3] << 8) | (uint32_t)data[4];
        }

        vesc_values_t val;
        memset(&val, 0, sizeof(val));
        if (self->motor == (void *)0 || self->motor->get_values == (void *)0) {
            return EDGE_EINVAL;
        }
        edge_status_t st = self->motor->get_values(self->motor->self, mask, &val);
        if (st != EDGE_OK) {
            return st;
        }

        /*
         * Mask ladder from the reference's COMM_GET_VALUES. GET_VALUES and
         * GET_VALUES_SELECTIVE are one code path there, with the mask all-ones for
         * the former; SELECTIVE echoes the mask it was given, GET_VALUES does not.
         */
        uint8_t *resp = self->cmd_reply_buf;
        size_t resp_len = 0;
        resp[resp_len++] = cmd_id;
        if (cmd_id == COMM_GET_VALUES_SELECTIVE) {
            buffer_append_uint32(resp, mask, &resp_len);
        }

        if (mask & (1u << 0)) {
            buffer_append_float16(resp, val.temp_mos, 1e1f, &resp_len);
        }
        if (mask & (1u << 1)) {
            buffer_append_float16(resp, val.temp_motor, 1e1f, &resp_len);
        }
        if (mask & (1u << 2)) {
            buffer_append_float32(resp, val.current_motor, 1e2f, &resp_len);
        }
        if (mask & (1u << 3)) {
            buffer_append_float32(resp, val.current_in, 1e2f, &resp_len);
        }
        if (mask & (1u << 4)) {
            buffer_append_float32(resp, val.id, 1e2f, &resp_len);
        }
        if (mask & (1u << 5)) {
            buffer_append_float32(resp, val.iq, 1e2f, &resp_len);
        }
        if (mask & (1u << 6)) {
            buffer_append_float16(resp, val.duty_now, 1e3f, &resp_len);
        }
        if (mask & (1u << 7)) {
            buffer_append_float32(resp, val.rpm, 1e0f, &resp_len);
        }
        if (mask & (1u << 8)) {
            buffer_append_float16(resp, val.v_in, 1e1f, &resp_len);
        }
        if (mask & (1u << 9)) {
            buffer_append_float32(resp, val.amp_hours, 1e4f, &resp_len);
        }
        if (mask & (1u << 10)) {
            buffer_append_float32(resp, val.amp_hours_charged, 1e4f, &resp_len);
        }
        if (mask & (1u << 11)) {
            buffer_append_float32(resp, val.watt_hours, 1e4f, &resp_len);
        }
        if (mask & (1u << 12)) {
            buffer_append_float32(resp, val.watt_hours_charged, 1e4f, &resp_len);
        }
        if (mask & (1u << 13)) {
            buffer_append_int32(resp, val.tachometer, &resp_len);
        }
        if (mask & (1u << 14)) {
            buffer_append_int32(resp, val.tachometer_abs, &resp_len);
        }
        if (mask & (1u << 15)) {
            resp[resp_len++] = (uint8_t)val.fault_code;
        }
        if (mask & (1u << 16)) {
            buffer_append_float32(resp, val.pid_pos_now, 1e6f, &resp_len);
        }
        if (mask & (1u << 17)) {
            resp[resp_len++] = val.controller_id;
        }
        if (mask & (1u << 18)) {
            buffer_append_float16(resp, val.temp_mos_1, 1e1f, &resp_len);
            buffer_append_float16(resp, val.temp_mos_2, 1e1f, &resp_len);
            buffer_append_float16(resp, val.temp_mos_3, 1e1f, &resp_len);
        }
        if (mask & (1u << 19)) {
            buffer_append_float32(resp, val.vd, 1e3f, &resp_len);
        }
        if (mask & (1u << 20)) {
            buffer_append_float32(resp, val.vq, 1e3f, &resp_len);
        }
        if (mask & (1u << 21)) {
            resp[resp_len++] = val.status;
        }

        return send_reply(self, resp_len);
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

    case COMM_GET_MCCONF: {
        if (self->config == (void *)0 || self->config->get_mcconf == (void *)0) {
            return EDGE_ENOTSUP;
        }
        /*
         * Reference commands_send_mcconf(): the packet is the command id followed by the
         * reference's own mc_configuration stream - 488 bytes, no framing of ours. The
         * reference keeps the old foc_offsets_* when asked for the defaults; this port has
         * no separate default variant yet (COMM_GET_MCCONF_DEFAULT is still unhandled, see
         * docs/bldc-migration.md).
         */
        size_t stream_len = 0u;
        edge_status_t st = self->config->get_mcconf(self->config->self, self->cmd_reply_buf + 1u,
                                                    sizeof(self->cmd_reply_buf) - 1u, &stream_len);
        if (st != EDGE_OK) {
            return st;
        }
        self->cmd_reply_buf[0] = COMM_GET_MCCONF;
        return send_reply(self, stream_len + 1u);
    }

    case COMM_SET_MCCONF: {
        if (self->config == (void *)0 || self->config->set_mcconf == (void *)0) {
            return EDGE_ENOTSUP;
        }
        /* In this port's codec `data` carries the command id (ind starts at 1), so the
         * stream begins at data + 1; the reference's `data` excludes it. */
        return self->config->set_mcconf(self->config->self, data + 1u, len - 1u);
    }

    case COMM_GET_APPCONF: {
        if (self->config == (void *)0 || self->config->get_appconf == (void *)0) {
            return EDGE_ENOTSUP;
        }
        size_t stream_len = 0u;
        edge_status_t st = self->config->get_appconf(self->config->self, self->cmd_reply_buf + 1u,
                                                     sizeof(self->cmd_reply_buf) - 1u, &stream_len);
        if (st != EDGE_OK) {
            return st;
        }
        self->cmd_reply_buf[0] = COMM_GET_APPCONF;
        return send_reply(self, stream_len + 1u);
    }

    case COMM_SET_APPCONF: {
        if (self->config == (void *)0 || self->config->set_appconf == (void *)0) {
            return EDGE_ENOTSUP;
        }
        return self->config->set_appconf(self->config->self, data + 1u, len - 1u);
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

    case COMM_SET_HANDBRAKE: {
        if (len < 5) {
            return EDGE_EINVAL;
        }
        /* Reference comm/commands.c:515: float32 scaled by 1e3 - amps, not a ratio, and
         * not 1e5 as the (relative) current commands use. timeout_reset() in the
         * reference is not reproduced; this module has no comm watchdog yet. */
        float current = buffer_get_float32(data, 1e3f, &ind);
        if (self->motor && self->motor->set_handbrake) {
            return self->motor->set_handbrake(self->motor->self, current);
        }
        return EDGE_OK;
    }

    case COMM_SET_CURRENT_REL: {
        if (len < 5) {
            return EDGE_EINVAL;
        }
        /* Reference comm/commands.c:1212: float32 scaled by 1e5, forwarded to
         * mc_interface_set_current_rel. The reference also calls timeout_reset() here
         * (and in every other command handler); this module has no comm watchdog yet,
         * so that side effect is not reproduced. */
        float rel = buffer_get_float32(data, 1e5f, &ind);
        if (self->motor && self->motor->set_current_rel) {
            return self->motor->set_current_rel(self->motor->self, rel);
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

    case COMM_GET_STATS: {
        if (len < 3) {
            return EDGE_EINVAL;
        }
        /* The request mask is 16 bits; the reply echoes it as 32 (reference:
         * comm/commands.c COMM_GET_STATS reads uint16 and appends uint32). */
        uint16_t mask = (uint16_t)(((uint16_t)data[1] << 8) | (uint16_t)data[2]);

        if (self->motor == (void *)0 || self->motor->get_stats == (void *)0) {
            return EDGE_EINVAL;
        }
        vesc_stats_t st_val;
        memset(&st_val, 0, sizeof(st_val));
        edge_status_t st = self->motor->get_stats(self->motor->self, &st_val);
        if (st != EDGE_OK) {
            return st;
        }

        uint8_t *resp = self->cmd_reply_buf;
        size_t resp_len = 0;
        resp[resp_len++] = COMM_GET_STATS;
        buffer_append_uint32(resp, (uint32_t)mask, &resp_len);

        /* Stats go out float32_auto, not float32: no scale factor. */
        if (mask & (1u << 0)) {
            buffer_append_float32_auto(resp, st_val.speed_avg, &resp_len);
        }
        if (mask & (1u << 1)) {
            buffer_append_float32_auto(resp, st_val.speed_max, &resp_len);
        }
        if (mask & (1u << 2)) {
            buffer_append_float32_auto(resp, st_val.power_avg, &resp_len);
        }
        if (mask & (1u << 3)) {
            buffer_append_float32_auto(resp, st_val.power_max, &resp_len);
        }
        if (mask & (1u << 4)) {
            buffer_append_float32_auto(resp, st_val.current_avg, &resp_len);
        }
        if (mask & (1u << 5)) {
            buffer_append_float32_auto(resp, st_val.current_max, &resp_len);
        }
        if (mask & (1u << 6)) {
            buffer_append_float32_auto(resp, st_val.temp_mos_avg, &resp_len);
        }
        if (mask & (1u << 7)) {
            buffer_append_float32_auto(resp, st_val.temp_mos_max, &resp_len);
        }
        if (mask & (1u << 8)) {
            buffer_append_float32_auto(resp, st_val.temp_motor_avg, &resp_len);
        }
        if (mask & (1u << 9)) {
            buffer_append_float32_auto(resp, st_val.temp_motor_max, &resp_len);
        }
        if (mask & (1u << 10)) {
            buffer_append_float32_auto(resp, st_val.count_time, &resp_len);
        }

        return send_reply(self, resp_len);
    }

    case COMM_RESET_STATS: {
        /* The first payload byte after the id is an "ack" flag; the reply is sent
         * only when it is set (reference: comm/commands.c COMM_RESET_STATS). */
        uint8_t ack = (len > 1) ? data[1] : 0u;

        if (self->motor == (void *)0 || self->motor->reset_stats == (void *)0) {
            return EDGE_EINVAL;
        }
        edge_status_t st = self->motor->reset_stats(self->motor->self);
        if (st != EDGE_OK) {
            return st;
        }

        if (ack == 0u) {
            return EDGE_OK;
        }

        uint8_t *resp = self->cmd_reply_buf;
        resp[0] = COMM_RESET_STATS;
        return send_reply(self, 1u);
    }

    case COMM_GET_DECODED_PPM: {
        if (self->app_status == (void *)0 || self->app_status->get_decoded_ppm == (void *)0) {
            return EDGE_EINVAL;
        }
        float level = 0.0f;
        float pulse_us = 0.0f;
        edge_status_t st =
            self->app_status->get_decoded_ppm(self->app_status->self, &level, &pulse_us);
        if (st != EDGE_OK) {
            return st;
        }

        uint8_t *resp = self->cmd_reply_buf;
        size_t resp_len = 0;
        resp[resp_len++] = COMM_GET_DECODED_PPM;
        /* Reference: decoded level and pulse length, int32 scaled by 1e6. */
        buffer_append_int32(resp, (int32_t)(level * 1000000.0), &resp_len);
        buffer_append_int32(resp, (int32_t)(pulse_us * 1000000.0), &resp_len);

        return send_reply(self, resp_len);
    }

    case COMM_GET_DECODED_ADC: {
        if (self->app_status == (void *)0 || self->app_status->get_decoded_adc == (void *)0) {
            return EDGE_EINVAL;
        }
        float level = 0.0f;
        float voltage = 0.0f;
        float level2 = 0.0f;
        float voltage2 = 0.0f;
        edge_status_t st = self->app_status->get_decoded_adc(self->app_status->self, &level,
                                                             &voltage, &level2, &voltage2);
        if (st != EDGE_OK) {
            return st;
        }

        uint8_t *resp = self->cmd_reply_buf;
        size_t resp_len = 0;
        resp[resp_len++] = COMM_GET_DECODED_ADC;
        buffer_append_int32(resp, (int32_t)(level * 1000000.0), &resp_len);
        buffer_append_int32(resp, (int32_t)(voltage * 1000000.0), &resp_len);
        buffer_append_int32(resp, (int32_t)(level2 * 1000000.0), &resp_len);
        buffer_append_int32(resp, (int32_t)(voltage2 * 1000000.0), &resp_len);

        return send_reply(self, resp_len);
    }

    case COMM_ALIVE:
        return EDGE_OK;

    default:
        return EDGE_ENOTSUP;
    }
}
