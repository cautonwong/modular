#include "motor_config/motor_config.h"
#include <string.h>

static void append_uint16(uint8_t *buf, uint16_t val, size_t *idx) {
    buf[(*idx)++] = (uint8_t)(val >> 8);
    buf[(*idx)++] = (uint8_t)val;
}

static void append_uint32(uint8_t *buf, uint32_t val, size_t *idx) {
    buf[(*idx)++] = (uint8_t)(val >> 24);
    buf[(*idx)++] = (uint8_t)(val >> 16);
    buf[(*idx)++] = (uint8_t)(val >> 8);
    buf[(*idx)++] = (uint8_t)val;
}

static void append_float(uint8_t *buf, float val, float scale, size_t *idx) {
    int32_t scaled = (int32_t)(val * scale);
    append_uint32(buf, (uint32_t)scaled, idx);
}

static uint16_t get_uint16(const uint8_t *buf, size_t *idx) {
    uint16_t val = (uint16_t)(((uint16_t)buf[*idx] << 8) | (uint16_t)buf[*idx + 1]);
    *idx += 2;
    return val;
}

static uint32_t get_uint32(const uint8_t *buf, size_t *idx) {
    uint32_t val = ((uint32_t)buf[*idx] << 24) | ((uint32_t)buf[*idx + 1] << 16) |
                   ((uint32_t)buf[*idx + 2] << 8) | ((uint32_t)buf[*idx + 3]);
    *idx += 4;
    return val;
}

static float get_float(const uint8_t *buf, float scale, size_t *idx) {
    int32_t val = (int32_t)get_uint32(buf, idx);
    return (float)val / scale;
}

static uint16_t calc_crc(const uint8_t *buf, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)buf[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

void motor_config_set_defaults(mc_configuration_t *mcconf, app_configuration_t *appconf) {
    if (mcconf != (void *)0) {
        memset(mcconf, 0, sizeof(*mcconf));
        mcconf->motor_type = MC_MOTOR_TYPE_FOC;
        mcconf->current_min = -60.0f;
        mcconf->current_max = 60.0f;
        mcconf->in_current_min = -20.0f;
        mcconf->in_current_max = 50.0f;
        mcconf->current_min_scale = 1.0f;
        mcconf->current_max_scale = 1.0f;
        mcconf->v_in_min = 8.0f;
        mcconf->v_in_max = 55.0f;
        mcconf->rpm_min = -100000.0f;
        mcconf->rpm_max = 100000.0f;
        mcconf->foc_current_kp = 0.03f;
        mcconf->foc_current_ki = 30.0f;
        mcconf->foc_f_sw = 25000.0f;
        mcconf->foc_motor_r = 0.015f;
        mcconf->foc_motor_l = 0.000020f;
        mcconf->foc_motor_flux_linkage = 0.005f;
        mcconf->foc_observer_gain = 2.0e6f;
        mcconf->temp_fet_max = 85.0f;
        mcconf->temp_motor_max = 85.0f;
    }

    if (appconf != (void *)0) {
        memset(appconf, 0, sizeof(*appconf));
        appconf->controller_id = 1;
        appconf->timeout_msec = 1000;
        appconf->timeout_brake_current = 0.0f;
        appconf->can_baud_rate = 500000;
    }
}

edge_status_t motor_config_validate(const mc_configuration_t *mcconf,
                                    const app_configuration_t *appconf) {
    if (mcconf == (void *)0 || appconf == (void *)0) {
        return EDGE_EINVAL;
    }

    if (mcconf->current_max <= 0.0f || mcconf->current_min >= 0.0f) {
        return EDGE_EINVAL;
    }
    if (mcconf->v_in_max <= mcconf->v_in_min || mcconf->v_in_min <= 0.0f) {
        return EDGE_EINVAL;
    }
    if (mcconf->foc_f_sw < 5000.0f || mcconf->foc_f_sw > 100000.0f) {
        return EDGE_EINVAL;
    }
    if (appconf->timeout_msec == 0) {
        return EDGE_EINVAL;
    }

    return EDGE_OK;
}

edge_status_t motor_config_serialize(const mc_configuration_t *mcconf,
                                     const app_configuration_t *appconf, uint8_t *buffer,
                                     size_t buf_size, size_t *out_len) {
    if (mcconf == (void *)0 || appconf == (void *)0 || buffer == (void *)0 ||
        out_len == (void *)0) {
        return EDGE_EINVAL;
    }

    edge_status_t status = motor_config_validate(mcconf, appconf);
    if (status != EDGE_OK) {
        return status;
    }

    if (buf_size < MOTOR_CONFIG_BUFFER_SIZE) {
        return EDGE_ENOSPC;
    }

    size_t idx = 0;
    append_uint32(buffer, MOTOR_CONFIG_SIGNATURE, &idx);
    append_uint16(buffer, MOTOR_CONFIG_SCHEMA_VER, &idx);

    /* Reserve 2 bytes for payload_len and 2 bytes for CRC */
    size_t len_idx = idx;
    idx += 2;
    size_t crc_idx = idx;
    idx += 2;

    size_t payload_start = idx;

    /* Serialize MC Config */
    buffer[idx++] = (uint8_t)mcconf->motor_type;
    append_float(buffer, mcconf->current_min, 1e2f, &idx);
    append_float(buffer, mcconf->current_max, 1e2f, &idx);
    append_float(buffer, mcconf->in_current_min, 1e2f, &idx);
    append_float(buffer, mcconf->in_current_max, 1e2f, &idx);
    append_float(buffer, mcconf->current_min_scale, 1e4f, &idx);
    append_float(buffer, mcconf->current_max_scale, 1e4f, &idx);
    append_float(buffer, mcconf->v_in_min, 1e2f, &idx);
    append_float(buffer, mcconf->v_in_max, 1e2f, &idx);
    append_float(buffer, mcconf->rpm_min, 1e0f, &idx);
    append_float(buffer, mcconf->rpm_max, 1e0f, &idx);
    append_float(buffer, mcconf->foc_current_kp, 1e6f, &idx);
    append_float(buffer, mcconf->foc_current_ki, 1e4f, &idx);
    append_float(buffer, mcconf->foc_f_sw, 1e0f, &idx);
    append_float(buffer, mcconf->foc_motor_r, 1e6f, &idx);
    append_float(buffer, mcconf->foc_motor_l, 1e9f, &idx);
    append_float(buffer, mcconf->foc_motor_flux_linkage, 1e7f, &idx);
    append_float(buffer, mcconf->foc_observer_gain, 1e-1f, &idx);
    append_float(buffer, mcconf->temp_fet_max, 1e1f, &idx);
    append_float(buffer, mcconf->temp_motor_max, 1e1f, &idx);

    /* Serialize App Config */
    buffer[idx++] = appconf->controller_id;
    append_uint32(buffer, appconf->timeout_msec, &idx);
    append_float(buffer, appconf->timeout_brake_current, 1e2f, &idx);
    append_uint32(buffer, appconf->can_baud_rate, &idx);

    size_t payload_len = idx - payload_start;
    append_uint16(buffer, (uint16_t)payload_len, &len_idx);

    /* Compute & Store CRC */
    uint16_t crc = calc_crc(buffer + payload_start, payload_len);
    append_uint16(buffer, crc, &crc_idx);

    *out_len = idx;
    return EDGE_OK;
}

edge_status_t motor_config_deserialize(mc_configuration_t *mcconf, app_configuration_t *appconf,
                                       const uint8_t *buffer, size_t len) {
    if (mcconf == (void *)0 || appconf == (void *)0 || buffer == (void *)0) {
        return EDGE_EINVAL;
    }

    if (len < 10) {
        return EDGE_EINVAL;
    }

    size_t idx = 0;
    uint32_t sig = get_uint32(buffer, &idx);
    if (sig != MOTOR_CONFIG_SIGNATURE) {
        return EDGE_EINVAL;
    }

    uint16_t ver = get_uint16(buffer, &idx);
    if (ver != MOTOR_CONFIG_SCHEMA_VER) {
        return EDGE_EINVAL;
    }

    uint16_t payload_len = get_uint16(buffer, &idx);
    uint16_t stored_crc = get_uint16(buffer, &idx);

    if (idx + payload_len > len) {
        return EDGE_EINVAL;
    }

    size_t payload_start = idx;
    uint16_t computed_crc = calc_crc(buffer + payload_start, payload_len);

    if (stored_crc != computed_crc) {
        return EDGE_EINVAL;
    }

    /* Deserialize MC Config */
    mcconf->motor_type = (mc_motor_type_t)buffer[idx++];
    mcconf->current_min = get_float(buffer, 1e2f, &idx);
    mcconf->current_max = get_float(buffer, 1e2f, &idx);
    mcconf->in_current_min = get_float(buffer, 1e2f, &idx);
    mcconf->in_current_max = get_float(buffer, 1e2f, &idx);
    mcconf->current_min_scale = get_float(buffer, 1e4f, &idx);
    mcconf->current_max_scale = get_float(buffer, 1e4f, &idx);
    mcconf->v_in_min = get_float(buffer, 1e2f, &idx);
    mcconf->v_in_max = get_float(buffer, 1e2f, &idx);
    mcconf->rpm_min = get_float(buffer, 1e0f, &idx);
    mcconf->rpm_max = get_float(buffer, 1e0f, &idx);
    mcconf->foc_current_kp = get_float(buffer, 1e6f, &idx);
    mcconf->foc_current_ki = get_float(buffer, 1e4f, &idx);
    mcconf->foc_f_sw = get_float(buffer, 1e0f, &idx);
    mcconf->foc_motor_r = get_float(buffer, 1e6f, &idx);
    mcconf->foc_motor_l = get_float(buffer, 1e9f, &idx);
    mcconf->foc_motor_flux_linkage = get_float(buffer, 1e7f, &idx);
    mcconf->foc_observer_gain = get_float(buffer, 1e-1f, &idx);
    mcconf->temp_fet_max = get_float(buffer, 1e1f, &idx);
    mcconf->temp_motor_max = get_float(buffer, 1e1f, &idx);

    /* Deserialize App Config */
    appconf->controller_id = buffer[idx++];
    appconf->timeout_msec = get_uint32(buffer, &idx);
    appconf->timeout_brake_current = get_float(buffer, 1e2f, &idx);
    appconf->can_baud_rate = get_uint32(buffer, &idx);

    return motor_config_validate(mcconf, appconf);
}
