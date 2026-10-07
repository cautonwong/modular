#include "imu/icm20948.h"

#include <stddef.h>

/* applications/imu/icm20948.c:21: this one answers at a single address. */
#define ICM20948_I2C_ADDR1 0x68u

/* The two pauses the reference takes with the RTOS, in microseconds. */
#define ICM20948_RESET_DELAY_US 1000u
#define ICM20948_FAIL_DELAY_US 10000u

static bool read_reg(imu_device_t *dev, uint8_t reg, uint8_t *rx, size_t len) {
    return imu_transport_read_reg(dev->transport, dev->dev_addr, reg, rx, len);
}

static bool write_reg(imu_device_t *dev, uint8_t reg, uint8_t value) {
    return imu_transport_write_reg(dev->transport, dev->dev_addr, reg, &value, 1u);
}

/*
 * applications/imu/icm20948.c:30-50, the whole of reset_init_icm: recover the bus, let the part
 * settle, then the clock source, the two full scales with their filters off, and back to the bank
 * the data registers live in. The reference does not check any of these writes and says so; neither
 * does this. Its magnetometer bypass is commented out there and is left out here.
 */
static bool reset_init_icm(imu_device_t *dev) {
    imu_transport_recover(dev->transport);
    imu_transport_delay_us(dev->transport, ICM20948_RESET_DELAY_US);

    /* The clock source, on the lowest bank. */
    (void)write_reg(dev, ICM20948_BANK_SEL, 0u << 4);
    (void)write_reg(dev, ICM20948_PWR_MGMT_1, 1u);

    /* The accelerometer at sixteen g, with its filter off. */
    (void)write_reg(dev, ICM20948_BANK_SEL, 2u << 4);
    (void)write_reg(dev, ICM20948_ACCEL_CONFIG, 0x06u);

    /* The gyroscope at two thousand degrees a second, likewise. */
    (void)write_reg(dev, ICM20948_BANK_SEL, 2u << 4);
    (void)write_reg(dev, ICM20948_GYRO_CONFIG_1, 0x06u);

    /* And back to the bank where the data registers are, so that they can be polled. */
    (void)write_reg(dev, ICM20948_BANK_SEL, 0u << 4);
    return true;
}

/* applications/imu/icm20948.c:52-57: the filter setting is not used by this driver, and neither is
 * the magnetometer flag. */
static bool configure(imu_device_t *dev, imu_filter_t filter, bool use_mag) {
    (void)filter;
    (void)use_mag;

    dev->dev_addr = ICM20948_I2C_ADDR1;
    return reset_init_icm(dev);
}

/*
 * applications/imu/icm20948.c:59-76: twelve bytes with the accelerometer's three axes first, which
 * is the other way round from the two drivers beside this one. The magnetometer is not read - the
 * reference leaves that as a note to itself - and the part is a six-axis one as far as this port is
 * concerned.
 */
static bool read_sample(imu_device_t *dev, float accel[3], float gyro[3], float mag[3]) {
    uint8_t rxb[12];

    if (!read_reg(dev, ICM20948_ACCEL_XOUT_H, rxb, sizeof(rxb))) {
        return false;
    }

    for (int i = 0; i < 3; i++) {
        const size_t o = 2u * (size_t)i;
        accel[i] = (float)((int16_t)((rxb[o] << 8) | rxb[o + 1u])) * 16.0f / 32768.0f;
        gyro[i] = (float)((int16_t)((rxb[o + 6u] << 8) | rxb[o + 6u + 1u])) * 2000.0f / 32768.0f;
        mag[i] = 0.0f;
    }

    return true;
}

/* applications/imu/icm20948.c:78-81: start the part over, then wait ten milliseconds. */
static void on_read_fail(imu_device_t *dev) {
    reset_init_icm(dev);
    imu_transport_delay_us(dev->transport, ICM20948_FAIL_DELAY_US);
}

static const imu_device_interface_t icm20948_interface = {
    .name = "ICM20948",
    .configure = configure,
    .read_sample = read_sample,
    .on_read_fail = on_read_fail,
    /* The reference gives this one no data-ready wiring. */
    .enable_drdy_output = NULL,
};

imu_device_t icm20948_device(imu_transport_t *transport) {
    return (imu_device_t){.interface = &icm20948_interface, .transport = transport};
}
