#include "imu/lsm6ds3.h"

#include <stddef.h>

/*
 * applications/imu/lsm6ds3.c's own two sensitivities: CTRL1_XL is at sixteen g and CTRL2_G at two
 * thousand degrees a second, which is what the driver writes into them.
 */
#define LSM6DS3_GYRO_DPS_PER_LSB (70.0f / 1000.0f) /* 70 mdps per least significant bit */
#define LSM6DS3_ACCEL_G_PER_LSB (0.488f / 1000.0f) /* 0.488 mg per least significant bit */

/*
 * The accelerometer and the gyroscope share one output-rate encoding, from 12.5 to 6660 hertz.
 */
static const struct {
    uint16_t hz;
    uint8_t code;
} odr_ladder[] = {
    {13, LSM6DS3_ACC_GYRO_ODR_XL_13Hz},     {26, LSM6DS3_ACC_GYRO_ODR_XL_26Hz},
    {52, LSM6DS3_ACC_GYRO_ODR_XL_52Hz},     {104, LSM6DS3_ACC_GYRO_ODR_XL_104Hz},
    {208, LSM6DS3_ACC_GYRO_ODR_XL_208Hz},   {416, LSM6DS3_ACC_GYRO_ODR_XL_416Hz},
    {833, LSM6DS3_ACC_GYRO_ODR_XL_833Hz},   {1660, LSM6DS3_ACC_GYRO_ODR_XL_1660Hz},
    {3330, LSM6DS3_ACC_GYRO_ODR_XL_3330Hz}, {6660, LSM6DS3_ACC_GYRO_ODR_XL_6660Hz},
};
#define LSM6DS3_ODR_LADDER_N (sizeof(odr_ladder) / sizeof(odr_ladder[0]))

/* The lowest rung whose rate reaches what was asked for, capped at the variant's own maximum. */
static uint8_t odr_index(uint16_t rate_hz, uint16_t max_hz) {
    const uint16_t target = rate_hz < max_hz ? rate_hz : max_hz;
    uint8_t i = 0u;
    while (i < LSM6DS3_ODR_LADDER_N - 1u && odr_ladder[i].hz < target) {
        i++;
    }
    return i;
}

static bool read_reg(imu_device_t *dev, uint8_t reg, uint8_t *res) {
    return imu_transport_read_reg(dev->transport, dev->dev_addr, reg, res, 1u);
}

static bool write_reg(imu_device_t *dev, uint8_t reg, uint8_t value) {
    return imu_transport_write_reg(dev->transport, dev->dev_addr, reg, &value, 1u);
}

static bool read_gyro_accel(imu_device_t *dev, uint8_t *res) {
    return imu_transport_read_reg(dev->transport, dev->dev_addr, LSM6DS3_ACC_GYRO_OUTX_L_G, res,
                                  12u);
}

/*
 * applications/imu/lsm6ds3.c:104-265, the whole of configure. It drives two variants: the TR-C,
 * whose gyroscope reaches the accelerometer's 6660 hertz, and the legacy part, whose gyroscope
 * stops at 1660 and has no filter of its own. What the reference prints while probing is not
 * printed here - a driver has no console, and the two facts it reports are the address and the
 * identity, both of which are visible in what this returns.
 */
static bool configure(imu_device_t *dev, imu_filter_t filter, bool use_mag) {
    (void)use_mag;

    uint8_t rxb[1];

    dev->dev_addr = LSM6DS3_ACC_GYRO_ADDR_A;
    bool ok = read_reg(dev, LSM6DS3_ACC_GYRO_WHO_AM_I_REG, rxb);
    if (!ok || (rxb[0] != 0x69u && rxb[0] != 0x6Au && rxb[0] != 0x6Cu)) {
        dev->dev_addr = LSM6DS3_ACC_GYRO_ADDR_B;
        ok = read_reg(dev, LSM6DS3_ACC_GYRO_WHO_AM_I_REG, rxb);
        if (!ok || (rxb[0] != 0x69u && rxb[0] != 0x6Au && rxb[0] != 0x6Cu)) {
            return false;
        }
    }

    bool is_trc = false;
    if (rxb[0] == 0x6Au) {
        is_trc = true;
        dev->variant = "TR-C";
    }

    const uint16_t gyro_max = is_trc ? 6660u : 1660u;

    /*
     * In a data-ready mode the gyroscope's own signal drives one read per sample, so both parts run
     * at the quantised sample rate. Polling runs the accelerometer as fast as it goes so that a
     * read is always of the freshest sample; the TR-C gyroscope does the same, while the legacy
     * one, having no filter of its own, tracks the sample rate instead of oversampling.
     */
    uint8_t accel_odr;
    uint8_t gyro_odr;
    if (dev->use_drdy) {
        const uint8_t idx = odr_index(dev->sample_rate_hz, gyro_max);
        accel_odr = gyro_odr = odr_ladder[idx].code;
        dev->sample_rate_hz = odr_ladder[idx].hz; /* the real output rate is the quantised one */
    } else {
        accel_odr = gyro_odr = odr_ladder[LSM6DS3_ODR_LADDER_N - 1u].code;
        if (!is_trc) {
            gyro_odr = odr_ladder[odr_index(dev->sample_rate_hz, gyro_max)].code;
        }
    }

    /* The accelerometer's resolution and rate, then its filtering. */
    uint8_t regv = LSM6DS3_ACC_GYRO_FS_XL_16g | accel_odr;

#define LSM6DS3TRC_BW0_XL 0x1u
#define LSM6DS3TRC_LPF1_BW_SEL 0x2u
    if (is_trc) {
        /* The analog low-pass is always at four hundred hertz. */
        regv |= LSM6DS3TRC_BW0_XL;
        /* Running at the sample rate, the digital filter anti-aliases: half the rate or a quarter.
         */
        if (dev->use_drdy && filter == IMU_FILTER_MEDIUM) {
            regv |= LSM6DS3TRC_LPF1_BW_SEL;
        }
    } else {
        /*
         * The legacy part's analog anti-alias is a set of absolute cutoffs; at the six-thousand-six
         * hundred hertz poll rate the automatic one is not used, so it is chosen by hand here and
         * enabled in CTRL4_C below.
         */
        const uint16_t scaled_rate = filter == IMU_FILTER_HIGH     ? dev->sample_rate_hz / 2u
                                     : filter == IMU_FILTER_MEDIUM ? dev->sample_rate_hz
                                                                   : dev->sample_rate_hz * 2u;
        if (scaled_rate <= 208u) {
            regv |= LSM6DS3_ACC_GYRO_BW_XL_50Hz;
        } else if (scaled_rate <= 416u) {
            regv |= LSM6DS3_ACC_GYRO_BW_XL_100Hz;
        } else if (scaled_rate <= 833u) {
            regv |= LSM6DS3_ACC_GYRO_BW_XL_200Hz;
        }
        /* Anything faster keeps the widest cutoff, whose register value is zero. */
    }

    ok = ok && write_reg(dev, LSM6DS3_ACC_GYRO_CTRL1_XL, regv);

    if (is_trc) {
#define LSM6DS3TRC_LPF2_XL_EN 0x80u
#define LSM6DS3TRC_HPCF_XL_ODR9 0x40u
#define LSM6DS3TRC_HPCF_XL_ODR50 0x00u
#define LSM6DS3TRC_HPCF_XL_ODR100 0x20u
        regv = 0u;
        if (dev->use_drdy) {
            /* At the sample rate the highest setting is the tighter second filter instead. */
            if (filter == IMU_FILTER_HIGH) {
                regv |= LSM6DS3TRC_LPF2_XL_EN | LSM6DS3TRC_HPCF_XL_ODR9;
            }
        } else {
            /* Polling, so the second filter anti-aliases the sub-sampled stream. */
            if (filter == IMU_FILTER_MEDIUM) {
                regv |= LSM6DS3TRC_LPF2_XL_EN | LSM6DS3TRC_HPCF_XL_ODR50;
            } else if (filter == IMU_FILTER_HIGH) {
                regv |= LSM6DS3TRC_LPF2_XL_EN | LSM6DS3TRC_HPCF_XL_ODR100;
            }
        }
        ok = ok && write_reg(dev, LSM6DS3_ACC_GYRO_CTRL8_XL, regv);
    }

    /* The gyroscope's resolution and rate, then its own filter for the TR-C. */
    regv = LSM6DS3_ACC_GYRO_FS_G_2000dps | gyro_odr;
    ok = ok && write_reg(dev, LSM6DS3_ACC_GYRO_CTRL2_G, regv);

    if (is_trc) {
#define LSM6DS3TRC_FTYPE_L 0x00u
#define LSM6DS3TRC_FTYPE_M 0x01u
#define LSM6DS3TRC_FTYPE_H 0x02u
        if (filter == IMU_FILTER_LOW) {
            regv = LSM6DS3TRC_FTYPE_L;
        } else if (filter == IMU_FILTER_MEDIUM) {
            regv = LSM6DS3TRC_FTYPE_M;
        } else {
            regv = LSM6DS3TRC_FTYPE_H;
        }
        ok = ok && write_reg(dev, LSM6DS3_ACC_GYRO_CTRL6_G, regv);
    }

    /* CTRL4_C, whose meaning differs between the two variants. */
    if (is_trc) {
        regv = LSM6DS3_ACC_GYRO_LPF1_SEL_G_ENABLED;
    } else {
        regv = LSM6DS3_ACC_GYRO_BW_SCAL_ODR_ENABLED;
    }
#define LSM6DS3_ACC_GYRO_DRDY_MASK                                                                 \
    0x08u /* hold the data-ready signal off until the filters settle */
    if (dev->use_drdy) {
        regv |= LSM6DS3_ACC_GYRO_DRDY_MASK;
    }
    ok = ok && write_reg(dev, LSM6DS3_ACC_GYRO_CTRL4_C, regv);

    /* Block update and register auto-increment. */
    regv = LSM6DS3_ACC_GYRO_BDU_BLOCK_UPDATE | LSM6DS3_ACC_GYRO_IF_INC_ENABLED;
    ok = ok && write_reg(dev, LSM6DS3_ACC_GYRO_CTRL3_C, regv);

    return ok;
}

/* applications/imu/lsm6ds3.c:267-278: twelve bytes, the gyroscope's three axes first. */
static bool read_sample(imu_device_t *dev, float accel[3], float gyro[3], float mag[3]) {
    uint8_t rxb[12];

    if (!read_gyro_accel(dev, rxb)) {
        return false;
    }

    for (int i = 0; i < 3; i++) {
        const size_t o = 2u * (size_t)i;
        gyro[i] = (float)((int16_t)((rxb[o + 1u] << 8) | rxb[o])) * LSM6DS3_GYRO_DPS_PER_LSB;
        accel[i] =
            (float)((int16_t)((rxb[o + 6u + 1u] << 8) | rxb[o + 6u])) * LSM6DS3_ACCEL_G_PER_LSB;
        mag[i] = 0.0f;
    }

    return true;
}

/* applications/imu/lsm6ds3.c:280-283: the data-ready signal on the device's first interrupt pin. */
static void enable_drdy_output(imu_device_t *dev, bool enable) {
    (void)write_reg(dev, LSM6DS3_ACC_GYRO_INT1_CTRL,
                    enable ? LSM6DS3_ACC_GYRO_INT1_DRDY_G_ENABLED : 0u);
}

static const imu_device_interface_t lsm6ds3_interface = {
    .name = "LSM6DS3",
    .configure = configure,
    .read_sample = read_sample,
    .on_read_fail = NULL, /* the reference has none for this one, and retries after a pause */
    .enable_drdy_output = enable_drdy_output,
};

imu_device_t lsm6ds3_device(imu_transport_t *transport) {
    return (imu_device_t){.interface = &lsm6ds3_interface, .transport = transport};
}
