#ifndef ICM20948_H
#define ICM20948_H

/*
 * The register map below is the reference's own, emitted verbatim by
 * tools/gen_imu_register_maps.py.
 */

#include "imu/device.h"

#include <stdbool.h>
#include <stdint.h>

imu_device_t icm20948_device(imu_transport_t *transport);

#define ICM20948_BANK_SEL 0x7F

// Bank 0 registers
#define ICM20948_PWR_MGMT_1 0x06
#define ICM20948_PIN_CFG 0x0F
#define ICM20948_ACCEL_XOUT_H 0x2D

// Bank 2 registers
#define ICM20948_ACCEL_CONFIG 0x14
#define ICM20948_GYRO_CONFIG_1 0x01
#endif /* ICM20948_H */
