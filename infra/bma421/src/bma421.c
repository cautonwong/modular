#include "bma421/bma421.h"

edge_status_t bma421_unpack_accel(const uint8_t raw_bytes[6], int16_t range_scale_divisor,
                                  bma421_accel_t *out_accel) {
    if (raw_bytes == NULL || out_accel == NULL || range_scale_divisor <= 0) {
        return EDGE_EINVAL;
    }

    int16_t raw_x = (int16_t)(((uint16_t)raw_bytes[1] << 8u) | (uint16_t)raw_bytes[0]);
    int16_t raw_y = (int16_t)(((uint16_t)raw_bytes[3] << 8u) | (uint16_t)raw_bytes[2]);
    int16_t raw_z = (int16_t)(((uint16_t)raw_bytes[5] << 8u) | (uint16_t)raw_bytes[4]);

    /* PineTime orientation: X and Y swapped, scale by divisor */
    out_accel->x = raw_y / range_scale_divisor;
    out_accel->y = raw_x / range_scale_divisor;
    out_accel->z = raw_z / range_scale_divisor;

    return EDGE_OK;
}

edge_status_t bma421_unpack_step_counter(const uint8_t raw_4bytes[4], uint32_t *out_steps) {
    if (raw_4bytes == NULL || out_steps == NULL) {
        return EDGE_EINVAL;
    }
    *out_steps = (uint32_t)raw_4bytes[0] | ((uint32_t)raw_4bytes[1] << 8u) |
                 ((uint32_t)raw_4bytes[2] << 16u) | ((uint32_t)raw_4bytes[3] << 24u);
    return EDGE_OK;
}
