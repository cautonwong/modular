#ifndef INFRA_BMA421_H
#define INFRA_BMA421_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BMA421_I2C_ADDR 0x18u
#define BMA421_CHIP_ID_REG 0x00u
#define BMA421_CHIP_ID_VAL 0x11u
#define BMA425_CHIP_ID_VAL 0x13u

#define BMA421_CMD_REG 0x7Eu
#define BMA421_CMD_SOFTRESET 0xB6u
#define BMA421_DATA_ACC_REG 0x12u /* 6 bytes: X_LSB, X_MSB, Y_LSB, Y_MSB, Z_LSB, Z_MSB */
#define BMA421_STEP_CNT_REG 0x1Eu /* 4 bytes: Step counter */

typedef struct bma421_accel {
    int16_t x;
    int16_t y;
    int16_t z;
} bma421_accel_t;

/**
 * Unpack 6 raw I2C bytes (LSB, MSB for X, Y, Z) and apply PineTime axis orientation
 * (swapping X and Y as required by the physical watch layout).
 */
edge_status_t bma421_unpack_accel(const uint8_t raw_bytes[6], int16_t range_scale_divisor,
                                  bma421_accel_t *out_accel);

/**
 * Unpack 4-byte step counter register buffer.
 */
edge_status_t bma421_unpack_step_counter(const uint8_t raw_4bytes[4], uint32_t *out_steps);

#ifdef __cplusplus
}
#endif

#endif /* INFRA_BMA421_H */
