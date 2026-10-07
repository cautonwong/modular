#ifndef LSM6DSV32X_H
#define LSM6DSV32X_H

/*
 * The reference's own header for this one is twenty-seven lines: a guard, its device type and the
 * factory. Its register map is not here either - it is written out at the top of its source file,
 * and that is where this port keeps it too.
 */

#include "imu/device.h"

#include <stdbool.h>
#include <stdint.h>

imu_device_t lsm6dsv32x_device(imu_transport_t *transport);

#endif /* LSM6DSV32X_H */
