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

typedef struct imu_device_interface {
    /* Human-readable model name for diagnostics. */
    const char *name;
    /* Probe the device and write its register init sequence; false on failure. */
    bool (*configure)(void *self, imu_filter_t filter, bool use_mag);
    /* Read one sample into engineering units; false if the read failed. */
    bool (*read_sample)(void *self, float accel[3], float gyro[3], float mag[3]);
    /* The failure policy - a re-initialisation, say. NULL means retry after a short pause. */
    void (*on_read_fail)(void *self);
    /* Enable the data-ready signal on the device's own pin. NULL means a timed read. */
    void (*enable_drdy_output)(void *self, bool enable);
    void *self;
} imu_device_interface_t;

/*
 * The handle is an opaque, caller-owned storage block (#258). The layout - and the
 * four fields a driver's `configure` rewrites - live in src/imu_internal.h, so a
 * caller cannot desynchronise `dev_addr` / `sample_rate_hz` from the device they
 * describe. The factories still return it by value.
 */
/* 24 bytes of real state on a 32-bit target, 40 on an LP64 host; the static
 * assertions in src/imu_internal.h keep both honest. */
#if UINTPTR_MAX > 0xFFFFFFFFu
#define IMU_DEVICE_STORAGE_SIZE 48u
#else
#define IMU_DEVICE_STORAGE_SIZE 32u
#endif
#define IMU_DEVICE_STORAGE_ALIGN 8u

typedef struct imu_device {
    _Alignas(IMU_DEVICE_STORAGE_ALIGN) unsigned char storage[IMU_DEVICE_STORAGE_SIZE];
} imu_device_t;

/* The construction-time prefix of the handle. Publishing it is what lets the hot
 * path stay inline: a sample read costs no extra call, while the mutable state
 * (`dev_addr`, `sample_rate_hz`, ...) stays private. src/imu_internal.h asserts the
 * real struct starts with exactly these members. */
typedef struct imu_device_prefix {
    const imu_device_interface_t *interface;
    imu_transport_t *transport;
} imu_device_prefix_t;

void imu_device_construct(imu_device_t *dev, const imu_device_interface_t *interface,
                          imu_transport_t *transport, void *priv);

static inline const imu_device_prefix_t *imu_device_prefix(const imu_device_t *dev) {
    return (const imu_device_prefix_t *)(const void *)(uintptr_t)dev;
}

static inline const imu_device_interface_t *imu_device_interface(const imu_device_t *dev) {
    return dev != (const imu_device_t *)0 ? imu_device_prefix(dev)->interface : (const imu_device_interface_t *)0;
}

static inline bool imu_device_configure(imu_device_t *dev, imu_filter_t filter, bool use_mag) {
    return imu_device_prefix(dev)->interface->configure(dev, filter, use_mag);
}

static inline bool imu_device_read_sample(imu_device_t *dev, float accel[3], float gyro[3],
                                          float mag[3]) {
    return imu_device_prefix(dev)->interface->read_sample(dev, accel, gyro, mag);
}

static inline void imu_device_on_read_fail(imu_device_t *dev) {
    if (imu_device_prefix(dev)->interface->on_read_fail != (void *)0) {
        imu_device_prefix(dev)->interface->on_read_fail(dev);
    }
}

static inline void imu_device_enable_drdy_output(imu_device_t *dev, bool enable) {
    if (imu_device_prefix(dev)->interface->enable_drdy_output != (void *)0) {
        imu_device_prefix(dev)->interface->enable_drdy_output(dev, enable);
    }
}

/* What a caller may observe; the fields themselves stay private. */
const char *imu_device_variant(const imu_device_t *dev);
uint8_t imu_device_address(const imu_device_t *dev);
uint16_t imu_device_sample_rate(const imu_device_t *dev);
void imu_device_set_sample_rate(imu_device_t *dev, uint16_t hz);
bool imu_device_uses_drdy(const imu_device_t *dev);
void imu_device_set_use_drdy(imu_device_t *dev, bool use_drdy);
void *imu_device_priv(const imu_device_t *dev);

#endif /* IMU_DEVICE_H */
