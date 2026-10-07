#ifndef IMU_BMI160_WRAPPER_H
#define IMU_BMI160_WRAPPER_H

#include "imu/device.h"

/*
 * Bosch's own driver, vendored whole: eight thousand seven hundred lines that the reference keeps
 * in imu/BMI160_driver and that are copied here rather than reimplemented. What it needs from a
 * bus, it asks for through two callbacks the wrapper below supplies.
 */
#include "bmi160.h"

#include <stdint.h>

/*
 * The interface is one of Bosch's own: BMI160_I2C_INTF or BMI160_SPI_INTF, which decides the
 * address the device answers at - an I2C slave address, or zero for SPI.
 *
 * The reference keeps the sensor structure in a file-scope static, because Bosch's read and write
 * callbacks carry no context of their own; the structure is the caller's here, as every other
 * driver in this port expects, while the transport still has to be one at a time for the reason the
 * reference gives.
 */
imu_device_t bmi160_device(imu_transport_t *transport, uint8_t interface,
                           struct bmi160_dev *sensor);

#endif /* IMU_BMI160_WRAPPER_H */
