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

    case COMM_TERMINAL_CMD: {
        if (self->ops == (void *)0 || self->ops->terminal_cmd == (void *)0) {
            return EDGE_ENOTSUP;
        }
        /*
         * Reference: terminal_process_string((char *)data), with the payload being the
         * command line the sender NUL-terminated. The reference runs that on its blocking
         * thread so the comm path is not stalled; this port is single-threaded and
         * cooperative, where running it inline is the equivalent and the caller's budget is
         * what limits it.
         */
        if (len < 2u) {
            return EDGE_EINVAL;
        }
        return self->ops->terminal_cmd(self->ops->self, (const char *)(data + 1u));
    }

    case COMM_DETECT_MOTOR_R_L: {
        /*
         * Reference comm/commands.c:2126-2165. The request carries nothing; the reply is the
         * command id, the resistance, the inductance and the difference between the two axes, each
         * in its own scale. A fault leaves the first two at zero, which is the reference's own
         * handling of a failed run.
         */
        if (self->ops == (void *)0 || self->ops->detect_r_l == (void *)0) {
            return EDGE_ENOTSUP;
        }

        vesc_detect_r_l_result_t measured;
        memset(&measured, 0, sizeof(measured));
        if (self->ops->detect_r_l(self->ops->self, &measured) != EDGE_OK || !measured.valid) {
            measured.r_ohm = 0.0f;
            measured.l_uh = 0.0f;
        }

        uint8_t *const reply = self->cmd_reply_buf;
        size_t out = 0;
        reply[out++] = COMM_DETECT_MOTOR_R_L;
        buffer_append_float32(reply, measured.r_ohm, 1e6, &out);
        buffer_append_float32(reply, measured.l_uh, 1e3, &out);
        buffer_append_float32(reply, measured.ld_lq_diff_uh, 1e3, &out);
        return send_reply(self, out);
    }

    case COMM_DETECT_MOTOR_FLUX_LINKAGE: {
        /*
         * Reference comm/commands.c:2165-2185. The request is the current, the minimum rpm, the
         * duty to spin up to and the resistance, at the scales its handler reads them at; the reply
         * is the command id and the linkage, and nothing else - unlike the open-loop variant, which
         * also sends the three encoder values. The caller's own rule is the last line of the
         * reference's handler: a measurement that did not succeed is sent as zero.
         */
        if (self->ops == (void *)0 || self->ops->detect_flux_linkage == (void *)0) {
            return EDGE_ENOTSUP;
        }
        if (len < 16u) {
            return EDGE_EINVAL;
        }

        size_t index = 1;
        const float current = buffer_get_float32(data, 1e3, &index);
        const float min_rpm = buffer_get_float32(data, 1e3, &index);
        const float duty = buffer_get_float32(data, 1e3, &index);
        const float resistance = buffer_get_float32(data, 1e6, &index);

        float linkage = 0.0f;
        if (self->ops->detect_flux_linkage(self->ops->self, current, min_rpm, duty, resistance,
                                           &linkage) != EDGE_OK) {
            linkage = 0.0f;
        }

        uint8_t *const reply = self->cmd_reply_buf;
        size_t out = 0;
        reply[out++] = COMM_DETECT_MOTOR_FLUX_LINKAGE;
        buffer_append_float32(reply, linkage, 1e7, &out);
        return send_reply(self, out);
    }

    case COMM_DETECT_MOTOR_FLUX_LINKAGE_OPENLOOP: {
        /*
         * Reference comm/commands.c:2296-2322. The request is the current, the electrical speed
         * per second, the duty to spin up to and the resistance; the inductance is optional and is
         * read only when the packet is long enough to hold it. The reply is the command id followed
         * by the linkage the caller settled on, and then the three encoder values the sensored
         * measurement fills in.
         *
         * Those three have no source here, and the reference at this point is reading its own
         * uninitialised locals for them - a real defect on its side that this port does not
         * reproduce: it sends zero, one and false, and says so in the confluence view.
         */
        if (self->ops == (void *)0 || self->ops->detect_flux_linkage_openloop == (void *)0) {
            return EDGE_ENOTSUP;
        }
        if (len < 16u) {
            return EDGE_EINVAL;
        }

        size_t index = 1;
        const float current = buffer_get_float32(data, 1e3, &index);
        const float erpm_per_sec = buffer_get_float32(data, 1e3, &index);
        const float duty = buffer_get_float32(data, 1e3, &index);
        const float resistance = buffer_get_float32(data, 1e6, &index);
        float inductance = 0.0f;
        if (len >= (uint32_t)index + 4u) {
            inductance = buffer_get_float32(data, 1e8, &index);
        }

        vesc_detect_flux_result_t flux;
        memset(&flux, 0, sizeof(flux));
        if (self->ops->detect_flux_linkage_openloop(self->ops->self, current, duty, erpm_per_sec,
                                                    resistance, inductance, &flux) != EDGE_OK) {
            flux.valid = false;
            flux.linkage_wb = 0.0f;
        }

        /* The caller's own rules (:2304-2314): a fault means zero, a measurement with too few
         * undriven samples is replaced by the undriven one, and an untrusted result is zero. */
        float linkage = flux.linkage_wb;
        if (!flux.valid) {
            linkage = 0.0f;
        } else if (flux.undriven_samples > 60.0f) {
            linkage = flux.linkage_undriven_wb;
        }

        uint8_t *const reply = self->cmd_reply_buf;
        size_t out = 0;
        reply[out++] = COMM_DETECT_MOTOR_FLUX_LINKAGE_OPENLOOP;
        buffer_append_float32(reply, linkage, 1e7, &out);
        buffer_append_float32(reply, 0.0f, 1e6, &out);
        buffer_append_float32(reply, 0.0f, 1e6, &out);
        reply[out++] = 0u;
        return send_reply(self, out);
    }

    case COMM_FORWARD_CAN: {
        if (self->ops == (void *)0 || self->ops->forward_can == (void *)0) {
            return EDGE_ENOTSUP;
        }
        /*
         * Reference: comm_can_send_buffer(data[0], data + 1, len - 1, 0) - the first payload
         * byte is the target controller id and the rest is what gets forwarded, with `send`
         * zero. The reference's dual-motor branch selects the second motor's configuration
         * instead of forwarding; this port has one motor, so the single-motor path is the
         * whole behaviour here.
         */
        if (len < 2u) {
            return EDGE_EINVAL;
        }
        return self->ops->forward_can(self->ops->self, data[1], data + 2u, len - 2u);
    }

    case COMM_GET_VALUES_SETUP:
    case COMM_GET_VALUES_SETUP_SELECTIVE: {
        if (self->motor == (void *)0 || self->motor->get_setup_values == (void *)0) {
            return EDGE_ENOTSUP;
        }

        vesc_setup_values_t setup;
        edge_status_t st = self->motor->get_setup_values(self->motor->self, &setup);
        if (st != EDGE_OK) {
            return st;
        }

        /*
         * Reference comm/commands.c:797-885. The reply is the command id, then - for the
         * selective variant only - the mask echoed from the request, then one field per set bit
         * with the reference's own encoding. The bit table and the scales are the whole content
         * of this command, so they are transcribed rather than derived.
         */
        uint8_t *resp = self->cmd_reply_buf;
        size_t n = 0u;
        resp[n++] = cmd_id;

        uint32_t mask = 0xFFFFFFFFu;
        if (cmd_id == COMM_GET_VALUES_SETUP_SELECTIVE) {
            if (len < 5u) {
                return EDGE_EINVAL;
            }
            size_t ind = 1u;
            /* The codec has buffer_get_int32 and not buffer_get_uint32; the four bytes are the
             * same big-endian word either way, so the cast is exact - including a mask whose top
             * bit is set, which reads as -1 and casts back to 0xFFFFFFFF. */
            mask = (uint32_t)buffer_get_int32(data, &ind);
            buffer_append_uint32(resp, mask, &n);
        }

        if (mask & (1u << 0)) {
            buffer_append_float16(resp, setup.temp_mos, 1e1f, &n);
        }
        if (mask & (1u << 1)) {
            buffer_append_float16(resp, setup.temp_motor, 1e1f, &n);
        }
        if (mask & (1u << 2)) {
            buffer_append_float32(resp, setup.current_tot, 1e2f, &n);
        }
        if (mask & (1u << 3)) {
            buffer_append_float32(resp, setup.current_in_tot, 1e2f, &n);
        }
        if (mask & (1u << 4)) {
            buffer_append_float16(resp, setup.duty_now, 1e3f, &n);
        }
        if (mask & (1u << 5)) {
            buffer_append_float32(resp, setup.rpm, 1e0f, &n);
        }
        if (mask & (1u << 6)) {
            buffer_append_float32(resp, setup.speed_m_s, 1e3f, &n);
        }
        if (mask & (1u << 7)) {
            buffer_append_float16(resp, setup.v_in, 1e1f, &n);
        }
        if (mask & (1u << 8)) {
            buffer_append_float16(resp, setup.battery_level, 1e3f, &n);
        }
        if (mask & (1u << 9)) {
            buffer_append_float32(resp, setup.ah_tot, 1e4f, &n);
        }
        if (mask & (1u << 10)) {
            buffer_append_float32(resp, setup.ah_charge_tot, 1e4f, &n);
        }
        if (mask & (1u << 11)) {
            buffer_append_float32(resp, setup.wh_tot, 1e4f, &n);
        }
        if (mask & (1u << 12)) {
            buffer_append_float32(resp, setup.wh_charge_tot, 1e4f, &n);
        }
        if (mask & (1u << 13)) {
            buffer_append_float32(resp, setup.distance_m, 1e3f, &n);
        }
        if (mask & (1u << 14)) {
            buffer_append_float32(resp, setup.distance_abs_m, 1e3f, &n);
        }
        if (mask & (1u << 15)) {
            buffer_append_float32(resp, setup.pid_pos_deg, 1e6f, &n);
        }
        if (mask & (1u << 16)) {
            resp[n++] = setup.fault;
        }
        if (mask & (1u << 17)) {
            resp[n++] = setup.controller_id;
        }
        if (mask & (1u << 18)) {
            resp[n++] = setup.num_vescs;
        }
        if (mask & (1u << 19)) {
            buffer_append_float32(resp, setup.wh_batt_left, 1e3f, &n);
        }
        if (mask & (1u << 20)) {
            buffer_append_uint32(resp, setup.odometer_m, &n);
        }
        if (mask & (1u << 21)) {
            buffer_append_uint32(resp, setup.uptime_ms, &n);
        }

        return send_reply(self, n);
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
         * stream begins at data + 1; the reference's `data` excludes it. A successful
         * apply is acknowledged with the packet's own id, one byte, as the reference does. */
        edge_status_t st = self->config->set_mcconf(self->config->self, data + 1u, len - 1u);
        if (st != EDGE_OK) {
            return st;
        }
        self->cmd_reply_buf[0] = COMM_SET_MCCONF;
        return send_reply(self, 1u);
    }

    case COMM_GET_MCCONF_DEFAULT: {
        if (self->config == (void *)0 || self->config->get_mcconf_default == (void *)0) {
            return EDGE_ENOTSUP;
        }
        size_t stream_len = 0u;
        edge_status_t st =
            self->config->get_mcconf_default(self->config->self, self->cmd_reply_buf + 1u,
                                             sizeof(self->cmd_reply_buf) - 1u, &stream_len);
        if (st != EDGE_OK) {
            return st;
        }
        self->cmd_reply_buf[0] = COMM_GET_MCCONF_DEFAULT;
        return send_reply(self, stream_len + 1u);
    }

    case COMM_GET_APPCONF_DEFAULT: {
        if (self->config == (void *)0 || self->config->get_appconf_default == (void *)0) {
            return EDGE_ENOTSUP;
        }
        size_t stream_len = 0u;
        edge_status_t st =
            self->config->get_appconf_default(self->config->self, self->cmd_reply_buf + 1u,
                                              sizeof(self->cmd_reply_buf) - 1u, &stream_len);
        if (st != EDGE_OK) {
            return st;
        }
        self->cmd_reply_buf[0] = COMM_GET_APPCONF_DEFAULT;
        return send_reply(self, stream_len + 1u);
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
        edge_status_t st = self->config->set_appconf(self->config->self, data + 1u, len - 1u);
        if (st != EDGE_OK) {
            return st;
        }
        self->cmd_reply_buf[0] = COMM_SET_APPCONF;
        return send_reply(self, 1u);
    }

    case COMM_SET_APPCONF_NO_STORE: {
        if (self->config == (void *)0 || self->config->set_appconf_nostore == (void *)0) {
            return EDGE_ENOTSUP;
        }
        edge_status_t st =
            self->config->set_appconf_nostore(self->config->self, data + 1u, len - 1u);
        if (st != EDGE_OK) {
            return st;
        }
        /* The reference acknowledges with the packet's own id here too. */
        self->cmd_reply_buf[0] = COMM_SET_APPCONF_NO_STORE;
        return send_reply(self, 1u);
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

    case COMM_DETECT_MOTOR_PARAM: {
        /*
         * Reference comm/commands.c:2096-2123: three scaled floats in, and out an int32 of the
         * cycle integrator's ceiling at a thousandth, an int32 of the coupling factor at the same
         * scale, the eight table bytes copied as they are, and one byte of the reference's own
         * count of the readings that came up short.
         *
         * A run that did not pass zeroes the two numbers and nothing else (:2111-2114): the table
         * and the count are what the procedure left behind, which is why they are its outputs
         * rather than values this handler sets. The DC-offset calibration the reference's
         * all-in-one command does first is the product's, around its own callback, as it is for the
         * other detections here.
         */
        if (len < 13u) {
            return EDGE_EINVAL;
        }

        size_t ind = 1u; /* past the command id, as every handler's own offset is */
        const float current = buffer_get_float32(data, 1e3f, &ind);
        const float min_rpm = buffer_get_float32(data, 1e3f, &ind);
        const float low_duty = buffer_get_float32(data, 1e3f, &ind);

        if (self->ops == (void *)0 || self->ops->detect_motor_param == (void *)0) {
            return EDGE_ENOTSUP;
        }

        float int_limit = 0.0f;
        float coupling_k = 0.0f;
        uint8_t table[8];
        int32_t hall_res = 0;
        memset(table, 0, sizeof(table));

        const edge_status_t status = self->ops->detect_motor_param(
            self->ops->self, current, min_rpm, low_duty, &int_limit, &coupling_k, table, &hall_res);
        if (status == EDGE_EINVAL || status == EDGE_ENOTSUP) {
            return status;
        }
        if (status != EDGE_OK) {
            int_limit = 0.0f;
            coupling_k = 0.0f;
        }

        size_t out = 0;
        self->cmd_reply_buf[out++] = COMM_DETECT_MOTOR_PARAM;
        buffer_append_int32(self->cmd_reply_buf, (int32_t)(int_limit * 1000.0f), &out);
        buffer_append_int32(self->cmd_reply_buf, (int32_t)(coupling_k * 1000.0f), &out);
        memcpy(&self->cmd_reply_buf[out], table, 8u);
        out += 8u;
        self->cmd_reply_buf[out++] = (uint8_t)hall_res;
        return send_reply(self, out);
    }

    case COMM_DETECT_APPLY_ALL_FOC: {
        /*
         * Reference comm/commands.c:2328-2347: a flag and five scaled floats in, an int16 out, and
         * the run itself behind the product. Its first act there is a DC-offset calibration
         * (conf_general.c:1747, mcpwm_foc_dc_cal) over the current sensor, which is the one piece
         * this layer cannot reach - the phase currents and their offsets belong to the product's
         * own current port - so the whole run, calibration included, is its callback. A product
         * that has not implemented it gets ENOTSUP rather than half a run's numbers.
         */
        if (len < 22u) {
            return EDGE_EINVAL;
        }

        const bool detect_can = data[ind++] != 0u;
        const float max_power_loss = buffer_get_float32(data, 1e3f, &ind);
        const float min_current_in = buffer_get_float32(data, 1e3f, &ind);
        const float max_current_in = buffer_get_float32(data, 1e3f, &ind);
        const float openloop_rpm = buffer_get_float32(data, 1e3f, &ind);
        const float sl_erpm = buffer_get_float32(data, 1e3f, &ind);

        if (self->ops == (void *)0 || self->ops->detect_apply_all_foc == (void *)0) {
            return EDGE_ENOTSUP;
        }

        int16_t result = -1;
        const edge_status_t status = self->ops->detect_apply_all_foc(
            self->ops->self, detect_can, max_power_loss, min_current_in, max_current_in,
            openloop_rpm, sl_erpm, &result);
        /*
         * The reference answers this command whatever the run did: the int16 it sends back is the
         * run's own result, which is what its -1 initial value is for. Only a port-level refusal
         * - this layer having nothing to call - is its own answer rather than the run's.
         */
        if (status == EDGE_EINVAL || status == EDGE_ENOTSUP) {
            return status;
        }

        size_t out = 0;
        self->cmd_reply_buf[out++] = COMM_DETECT_APPLY_ALL_FOC;
        buffer_append_int16(self->cmd_reply_buf, result, &out);
        return send_reply(self, out);
    }

    case COMM_DETECT_HALL_FOC: {
        /*
         * Reference comm/commands.c:2238-2276: a current in at a thousandth, eight bytes of hall
         * table and one byte of verdict out. The reference stages a configuration of its own for
         * the run and puts the old one back; that is the product's here, around its own callback,
         * because the sensor port the gate is written on is its hardware. A product whose port is
         * not a hall one answers ENOTSUP, and the reply is the table of two hundred and fifty-fives
         * the reference sends in the same case.
         */
        if (len < 5u) {
            return EDGE_EINVAL;
        }

        size_t ind = 1u; /* past the command id, as every handler's own offset is */
        const float current = buffer_get_float32(data, 1e3f, &ind);

        uint8_t table[8];
        memset(table, 0, sizeof(table));
        bool result = false;

        if (self->ops == (void *)0 || self->ops->detect_hall_foc == (void *)0) {
            return EDGE_ENOTSUP;
        }

        const edge_status_t status =
            self->ops->detect_hall_foc(self->ops->self, current, table, &result);
        if (status == EDGE_EINVAL) {
            return status;
        }

        size_t out = 0;
        self->cmd_reply_buf[out++] = COMM_DETECT_HALL_FOC;
        if (status == EDGE_OK) {
            memcpy(&self->cmd_reply_buf[out], table, 8u);
            out += 8u;
            self->cmd_reply_buf[out++] = result ? 0u : 1u;
        } else {
            memset(&self->cmd_reply_buf[out], 255, 8u);
            out += 8u;
            self->cmd_reply_buf[out++] = 1u;
        }
        return send_reply(self, out);
    }

    case COMM_REBOOT:
        /*
         * Reference comm/commands.c:695-698: the backup block is stored and the machine resets, and
         * no reply goes out - the connection simply ends. Both halves are the product's, and in
         * that order, so the call carries both: a product that has neither a store nor a reset line
         * has nothing to do with this command and says so.
         */
        if (self->ops == (void *)0 || self->ops->reboot == (void *)0) {
            return EDGE_ENOTSUP;
        }
        return self->ops->reboot(self->ops->self);

    case COMM_JUMP_TO_BOOTLOADER:
        /*
         * Reference comm/commands.c:304-306, which reaches flash_helper_jump_to_bootloader: the
         * motor is released, the serial ports stopped, the watchdog slowed and the interrupt system
         * disabled before the jump, and no reply is sent. Its ..._ALL_CAN sibling forwards the
         * command to the bus first, which needs a bus that can send; that half arrives with the CAN
         * status receive path's siblings rather than here.
         */
        if (self->ops == (void *)0 || self->ops->jump_to_bootloader == (void *)0) {
            return EDGE_ENOTSUP;
        }
        return self->ops->jump_to_bootloader(self->ops->self);

    default:
        return EDGE_ENOTSUP;
    }
}
