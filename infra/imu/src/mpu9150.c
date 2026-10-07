#include "imu/mpu9150.h"

#include <string.h>

/* applications/imu/mpu9150.c:28-36's own settings. */
#define MPU9150_MAG_DIV 10
#define MPU9150_FAIL_DELAY_US 1000u
#define MPU9150_MAX_IDENTICAL_READS 5
#define MPU9150_ADDR1 0x68u
#define MPU9150_ADDR2 0x69u
#define MPU9150_MAG_ADDR 0x0Cu /* the AK8975 magnetometer, reached over the I2C bypass */

static bool read_reg(imu_device_t *dev, uint8_t addr, uint8_t reg, uint8_t *rx, size_t len) {
    return imu_transport_read_reg(dev->transport, addr, reg, rx, len);
}

static bool write_reg(imu_device_t *dev, uint8_t addr, uint8_t reg, uint8_t value) {
    return imu_transport_write_reg(dev->transport, addr, reg, &value, 1u);
}

/* applications/imu/mpu9150.c:53-71: fourteen bytes, with the temperature word skipped between. */
static bool get_raw_accel_gyro(imu_device_t *dev, int16_t *accel_gyro) {
    uint8_t rxb[14];

    if (!read_reg(dev, dev->dev_addr, MPU9150_ACCEL_XOUT_H, rxb, sizeof(rxb))) {
        return false;
    }

    for (int i = 0; i < 3; i++) {
        accel_gyro[i] = (int16_t)(((uint16_t)rxb[2u * (size_t)i] << 8) + rxb[2u * (size_t)i + 1u]);
    }
    /* The angular rate, with the temperature register at index 3 skipped. */
    for (int i = 4; i < 7; i++) {
        accel_gyro[i - 1] =
            (int16_t)(((uint16_t)rxb[2u * (size_t)i] << 8) + rxb[2u * (size_t)i + 1u]);
    }

    return true;
}

/* applications/imu/mpu9150.c:73-86: the magnetometer's bytes are the other way round. */
static bool get_raw_mag(imu_device_t *dev, int16_t *mag) {
    uint8_t rxb[6];

    if (!read_reg(dev, MPU9150_MAG_ADDR, MPU9150_HXL, rxb, sizeof(rxb))) {
        return false;
    }

    for (int i = 0; i < 3; i++) {
        mag[i] = (int16_t)(((uint16_t)rxb[2u * (size_t)i + 1u] << 8) + rxb[2u * (size_t)i]);
    }

    /* Start the measurement that the next iteration will read. */
    return write_reg(dev, MPU9150_MAG_ADDR, MPU9150_CNTL, 0x01u);
}

/* applications/imu/mpu9150.c:88-113, the whole of reset_init_mpu. */
static void reset_init_mpu(imu_device_t *dev) {
    mpu9150_state_t *st = dev->priv;

    imu_transport_recover(dev->transport);

    /* The clock source is the gyroscope's x axis; an address that does not answer is tried the
     * other way round, which is the reference's own two-address fallback. */
    if (!write_reg(dev, dev->dev_addr, MPU9150_PWR_MGMT_1, 0x01u)) {
        dev->dev_addr = (dev->dev_addr == MPU9150_ADDR1) ? MPU9150_ADDR2 : MPU9150_ADDR1;
        if (!write_reg(dev, dev->dev_addr, MPU9150_PWR_MGMT_1, 0x01u)) {
            return;
        }
    }

    const bool ok =
        /* The accelerometer's full scale at sixteen g, */
        write_reg(dev, dev->dev_addr, MPU9150_ACCEL_CONFIG,
                  (uint8_t)(MPU9150_ACCEL_FS_16 << MPU9150_ACONFIG_AFS_SEL_BIT)) &&
        /* the gyroscope's at two thousand degrees a second, */
        write_reg(dev, dev->dev_addr, MPU9150_GYRO_CONFIG,
                  (uint8_t)(MPU9150_GYRO_FS_2000 << MPU9150_GCONFIG_FS_SEL_BIT)) &&
        /* and its low-pass filter at two hundred and fifty-six hertz, a millisecond of delay. */
        write_reg(dev, dev->dev_addr, MPU9150_CONFIG, MPU9150_DLPF_BW_256);

    /* The bypass is what lets the magnetometer be reached directly. */
    if (ok && st->use_magnetometer) {
        (void)write_reg(dev, dev->dev_addr, MPU9150_INT_PIN_CFG, 0x02u);
    }
}

/* applications/imu/mpu9150.c:115-141: probe and identify. The filter is not used by this driver. */
static bool configure(imu_device_t *dev, imu_filter_t filter, bool use_mag) {
    (void)filter;

    mpu9150_state_t *st = dev->priv;
    memset(st, 0, sizeof(*st));
    st->use_magnetometer = use_mag;
    st->mag_cnt = MPU9150_MAG_DIV; /* so the first sample reads the magnetometer */

    dev->dev_addr = MPU9150_ADDR1;
    reset_init_mpu(dev);

    uint8_t who = 0u;
    (void)read_reg(dev, dev->dev_addr, MPU9150_WHO_AM_I, &who, 1u);
    switch (who) {
    case 0x68u:
        dev->variant = "9150"; /* shared with the MPU-6050 die */
        return true;
    case 0x71u:
        dev->variant = "9250";
        return true;
    case 0x73u:
        dev->variant = "9255";
        return true;
    case 0x69u:
        return true; /* accepted historically, and not a documented identity: no variant is named */
    default:
        return false;
    }
}

/* applications/imu/mpu9150.c:143-192: one sample, with the stuck-sensor guard and the
 * magnetometer's own decimation. */
static bool read_sample(imu_device_t *dev, float accel[3], float gyro[3], float mag[3]) {
    mpu9150_state_t *st = dev->priv;
    int16_t raw[6];

    if (!get_raw_accel_gyro(dev, raw)) {
        return false;
    }

    /* A stuck sensor keeps returning the same bytes, and a run of them is treated as a failure. */
    bool identical = true;
    for (int i = 0; i < 6; i++) {
        if (raw[i] != st->prev_raw[i]) {
            identical = false;
            break;
        }
    }
    st->identical_reads = identical ? st->identical_reads + 1u : 0u;
    if (st->identical_reads >= MPU9150_MAX_IDENTICAL_READS) {
        return false;
    }
    memcpy(st->prev_raw, raw, sizeof(st->prev_raw));

    accel[0] = (float)raw[0] * 16.0f / 32768.0f;
    accel[1] = (float)raw[1] * 16.0f / 32768.0f;
    accel[2] = (float)raw[2] * 16.0f / 32768.0f;

    gyro[0] = (float)raw[3] * 2000.0f / 32768.0f;
    gyro[1] = (float)raw[4] * 2000.0f / 32768.0f;
    gyro[2] = (float)raw[5] * 2000.0f / 32768.0f;

    if (st->use_magnetometer) {
        /* The magnetometer runs at a tenth of the sample rate, and the last reading is reused in
         * between - so what is delivered lags the other two, as the reference's own note says. */
        mag[0] = (float)st->mag_raw[0] * 1200.0f / 4096.0f;
        mag[1] = (float)st->mag_raw[1] * 1200.0f / 4096.0f;
        mag[2] = (float)st->mag_raw[2] * 1200.0f / 4096.0f;

        if (++st->mag_cnt >= MPU9150_MAG_DIV) {
            st->mag_cnt = 0u;
            if (!get_raw_mag(dev, st->mag_raw)) {
                reset_init_mpu(dev);
            }
        }
    } else {
        mag[0] = 0.0f;
        mag[1] = 0.0f;
        mag[2] = 0.0f;
    }

    return true;
}

/* applications/imu/mpu9150.c:194-197: wait a millisecond, then start the device over. */
static void on_read_fail(imu_device_t *dev) {
    imu_transport_delay_us(dev->transport, MPU9150_FAIL_DELAY_US);
    reset_init_mpu(dev);
}

static const imu_device_interface_t mpu9150_interface = {
    .name = "MPU9X50",
    .configure = configure,
    .read_sample = read_sample,
    .on_read_fail = on_read_fail,
};

imu_device_t mpu9150_device(imu_transport_t *transport, mpu9150_state_t *state) {
    return (imu_device_t){.interface = &mpu9150_interface, .transport = transport, .priv = state};
}
