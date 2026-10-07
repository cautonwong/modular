#ifndef IMU_DEVICE_H
#define IMU_DEVICE_H

#include "imu/transport.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * datatypes.h:852-855, the filter setting, in its own order: three values and no "none".
 */
typedef enum imu_filter {
    IMU_FILTER_LOW = 0,
    IMU_FILTER_MEDIUM,
    IMU_FILTER_HIGH,
} imu_filter_t;

/*
 * A device: what a driver can do, and what the registry above it needs to know. The reference's own
 * device.h, whose comments say the same things - the variant is filled in when a driver detects
 * one, the address is the I2C one and ignored by SPI, and use_drdy is resolved before configure
 * runs.
 */
typedef struct imu_device imu_device_t;

typedef struct imu_device_interface {
    /* Human-readable model name for diagnostics. */
    const char *name;
    /* Probe the device and write its register init sequence; false on failure. */
    bool (*configure)(imu_device_t *dev, imu_filter_t filter, bool use_mag);
    /* Read one sample into engineering units; false if the read failed. */
    bool (*read_sample)(imu_device_t *dev, float accel[3], float gyro[3], float mag[3]);
    /* The failure policy - a re-initialisation, say. NULL means retry after a short pause. */
    void (*on_read_fail)(imu_device_t *dev);
    /* Enable the data-ready signal on the device's own pin. NULL means a timed read. */
    void (*enable_drdy_output)(imu_device_t *dev, bool enable);
} imu_device_interface_t;

struct imu_device {
    const imu_device_interface_t *interface;
    imu_transport_t *transport;
    /* The model variant a driver recognised, or NULL when it recognised none. */
    const char *variant;
    /* The I2C address this device answers at; ignored by the SPI transports. */
    uint8_t dev_addr;
    /* The rate the read loop runs at, which a driver's configure may override with its own. */
    uint16_t sample_rate_hz;
    /* True when the read loop will be driven by the device's data-ready pin rather than by time. */
    bool use_drdy;
    void *priv;
};

static inline bool imu_device_configure(imu_device_t *dev, imu_filter_t filter, bool use_mag) {
    return dev->interface->configure(dev, filter, use_mag);
}

static inline bool imu_device_read_sample(imu_device_t *dev, float accel[3], float gyro[3],
                                          float mag[3]) {
    return dev->interface->read_sample(dev, accel, gyro, mag);
}

static inline void imu_device_on_read_fail(imu_device_t *dev) {
    if (dev->interface->on_read_fail != (void *)0) {
        dev->interface->on_read_fail(dev);
    }
}

static inline void imu_device_enable_drdy_output(imu_device_t *dev, bool enable) {
    if (dev->interface->enable_drdy_output != (void *)0) {
        dev->interface->enable_drdy_output(dev, enable);
    }
}

#endif /* IMU_DEVICE_H */
