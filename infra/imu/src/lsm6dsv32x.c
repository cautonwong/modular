#include "imu/lsm6dsv32x.h"

#include <stddef.h>

/*
 * The register map is written out here rather than in the header, which is where the reference
 * keeps it: its lsm6dsv32x.h is a guard, a device type and a factory, and nothing else.
 */
#define LSM6DSV32X_ADDR_A 0x6Au /* the address with SA0 low */
#define LSM6DSV32X_ADDR_B 0x6Bu /* and with SA0 high */

#define REG_INT1_CTRL 0x0Du
#define REG_WHO_AM_I 0x0Fu
#define REG_CTRL1 0x10u /* the accelerometer's operating mode and output rate */
#define REG_CTRL2 0x11u /* the gyroscope's */
#define REG_CTRL3 0x12u /* boot, block update, auto-increment, software reset */
#define REG_CTRL4 0x13u /* the data-ready mask and pulse, among others */
#define REG_CTRL6 0x15u /* the gyroscope's filter bandwidth and full scale */
#define REG_CTRL7 0x16u /* and its filter's enable */
#define REG_CTRL8 0x17u /* the accelerometer's second filter bandwidth and full scale */
#define REG_CTRL9 0x18u /* and that filter's enable */
#define REG_OUTX_L_G                                                                               \
    0x22u /* the gyroscope's three axes, then the accelerometer's: twelve bytes                    \
           */

#define WHO_AM_I_VAL 0x70u

#define CTRL3_BDU (1u << 6)
#define CTRL3_IF_INC (1u << 2)
#define CTRL4_DRDY_MASK (1u << 3) /* hold the data-ready signal off until the filters settle */
#define CTRL7_LPF1_G_EN (1u << 0)
#define CTRL8_MUST_SET (1u << 2)   /* this bit must be written as one */
#define CTRL9_LPF2_XL_EN (1u << 3) /* the low-pass path, with the high-pass slope left clear */
#define INT1_DRDY_G (1u << 1)      /* the gyroscope's data-ready on the first interrupt pin */

/*
 * The full-scale ranges, as register codes and the sensitivities that go with them, and then the
 * pair in force - which is what the reference says to change to switch between them.
 */
#define FS_G_4000DPS 0x0Cu
#define FS_G_2000DPS 0x04u
#define FS_XL_32G 0x03u
#define FS_XL_16G 0x02u
#define GYRO_LSB_4000DPS (140.0f / 1000.0f) /* 140 millidegrees a second per bit */
#define GYRO_LSB_2000DPS (70.0f / 1000.0f)  /* 70 */
#define ACCEL_LSB_32G (0.976f / 1000.0f)    /* 0.976 milligee per bit */
#define ACCEL_LSB_16G (0.488f / 1000.0f)    /* 0.488 */

#define FS_G_REG FS_G_4000DPS
#define FS_XL_REG FS_XL_32G
#define LSM6DSV32X_GYRO_DPS_PER_LSB GYRO_LSB_4000DPS
#define LSM6DSV32X_ACCEL_G_PER_LSB ACCEL_LSB_32G

/* Both parts share the ladder seven and a half hertz times two to the n, as the register nibbles
 * two through twelve; the eight hertz entry stands in for seven and a half, since a rate is a whole
 * number of hertz. */
static const struct {
    uint16_t hz;
    uint8_t code;
} odr_ladder[] = {
    {8, 0x2u},   {15, 0x3u},  {30, 0x4u},   {60, 0x5u},   {120, 0x6u},  {240, 0x7u},
    {480, 0x8u}, {960, 0x9u}, {1920, 0xAu}, {3840, 0xBu}, {7680, 0xCu},
};
#define LSM6DSV32X_ODR_LADDER_N (sizeof(odr_ladder) / sizeof(odr_ladder[0]))

static bool read_reg(imu_device_t *dev, uint8_t reg, uint8_t *res) {
    return imu_transport_read_reg(dev->transport, dev->dev_addr, reg, res, 1u);
}

static bool write_reg(imu_device_t *dev, uint8_t reg, uint8_t value) {
    return imu_transport_write_reg(dev->transport, dev->dev_addr, reg, &value, 1u);
}

/*
 * applications/imu/lsm6dsv32x.c:91-197, the whole of configure, for a six-axis part with no
 * magnetometer. The two console lines the reference prints while probing are not printed here; what
 * they report is the address and the identity, both of which are in what this returns.
 */
static bool configure(imu_device_t *dev, imu_filter_t filter, bool use_mag) {
    (void)use_mag;

    uint8_t id = 0u;
    dev->dev_addr = LSM6DSV32X_ADDR_A;
    bool ok = read_reg(dev, REG_WHO_AM_I, &id);
    if (!ok || id != WHO_AM_I_VAL) {
        dev->dev_addr = LSM6DSV32X_ADDR_B;
        ok = read_reg(dev, REG_WHO_AM_I, &id);
        if (!ok || id != WHO_AM_I_VAL) {
            return false;
        }
    }

    /*
     * Polling runs at the highest rate so that an asynchronous poll always reads the freshest
     * sample; a data-ready edge quantises to the lowest rate that reaches what was asked for, so
     * that each edge drives exactly one read - and that rate is then the real output rate.
     */
    uint8_t odr_idx = LSM6DSV32X_ODR_LADDER_N - 1u;
    if (dev->use_drdy) {
        odr_idx = 0u;
        while (odr_idx < LSM6DSV32X_ODR_LADDER_N - 1u &&
               odr_ladder[odr_idx].hz < dev->sample_rate_hz) {
            odr_idx++;
        }
        dev->sample_rate_hz = odr_ladder[odr_idx].hz;
    }
    const uint16_t odr_hz = odr_ladder[odr_idx].hz;
    const uint8_t odr = odr_ladder[odr_idx].code;

    /*
     * The accelerometer's second filter, at the widest cutoff no higher than the target: a half of
     * the rate means the filter is off, and the ratios below need it on. The target is a fraction
     * of the output rate, and the level sets which fraction.
     */
    const uint8_t divisor = (filter == IMU_FILTER_HIGH)     ? 8u
                            : (filter == IMU_FILTER_MEDIUM) ? 4u
                                                            : 2u;
    const uint16_t cutoff = dev->sample_rate_hz / divisor;

    const bool lpf2_en = (odr_hz / 2u) > cutoff;
    uint8_t lpf2_bw = 0u;
    if (lpf2_en) {
        static const uint16_t lpf2_n[8] = {4u, 10u, 20u, 45u, 100u, 200u, 400u, 800u};
        lpf2_bw = 7u; /* the narrowest, if nothing wider fits */
        for (uint8_t code = 0u; code < 8u; code++) {
            if (odr_hz / lpf2_n[code] <= cutoff) {
                lpf2_bw = code;
                break;
            }
        }
    }

    /* Block update and register auto-increment. */
    ok = ok && write_reg(dev, REG_CTRL3, CTRL3_BDU | CTRL3_IF_INC);

    /* The accelerometer: its full scale and that filter. */
    ok = ok && write_reg(dev, REG_CTRL8, (uint8_t)((lpf2_bw << 5) | CTRL8_MUST_SET | FS_XL_REG));
    ok = ok && write_reg(dev, REG_CTRL9, lpf2_en ? CTRL9_LPF2_XL_EN : 0u);

    /* The gyroscope: its full scale, with its own filter bypassed at the widest setting and enabled
     * for the two narrower ones, whose cutoffs scale down with the rate. */
    const bool lpf1_en = filter != IMU_FILTER_LOW;
    const uint8_t lpf1_bw = (filter == IMU_FILTER_HIGH) ? 0x2u : 0x0u;
    ok = ok && write_reg(dev, REG_CTRL6, (uint8_t)((lpf1_bw << 4) | FS_G_REG));
    ok = ok && write_reg(dev, REG_CTRL7, lpf1_en ? CTRL7_LPF1_G_EN : 0u);

    /* In a data-ready mode the signal is masked until the filters have settled. */
    ok = ok && write_reg(dev, REG_CTRL4, dev->use_drdy ? CTRL4_DRDY_MASK : 0u);

    /* The sensors are enabled last: writing the rate is what powers them up. */
    ok = ok && write_reg(dev, REG_CTRL1, odr);
    ok = ok && write_reg(dev, REG_CTRL2, odr);

    return ok;
}

/* applications/imu/lsm6dsv32x.c:199-215: twelve bytes, the gyroscope's three axes first again. */
static bool read_sample(imu_device_t *dev, float accel[3], float gyro[3], float mag[3]) {
    uint8_t rxb[12];

    if (!imu_transport_read_reg(dev->transport, dev->dev_addr, REG_OUTX_L_G, rxb, sizeof(rxb))) {
        return false;
    }

    for (int i = 0; i < 3; i++) {
        const size_t o = 2u * (size_t)i;
        gyro[i] = (float)((int16_t)((rxb[o + 1u] << 8) | rxb[o])) * LSM6DSV32X_GYRO_DPS_PER_LSB;
        accel[i] =
            (float)((int16_t)((rxb[o + 6u + 1u] << 8) | rxb[o + 6u])) * LSM6DSV32X_ACCEL_G_PER_LSB;
        /* A six-axis part has no magnetometer, and the attitude maths must not be told otherwise.
         */
        mag[i] = 0.0f;
    }

    return true;
}

/* applications/imu/lsm6dsv32x.c:217-220. */
static void enable_drdy_output(imu_device_t *dev, bool enable) {
    (void)write_reg(dev, REG_INT1_CTRL, enable ? INT1_DRDY_G : 0u);
}

static const imu_device_interface_t lsm6dsv32x_interface = {
    .name = "LSM6DSV32X",
    .configure = configure,
    .read_sample = read_sample,
    .on_read_fail = NULL, /* the reference has none for this one either */
    .enable_drdy_output = enable_drdy_output,
};

imu_device_t lsm6dsv32x_device(imu_transport_t *transport) {
    return (imu_device_t){.interface = &lsm6dsv32x_interface, .transport = transport};
}
