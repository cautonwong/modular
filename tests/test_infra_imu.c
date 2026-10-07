/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "imu/bmi160_wrapper.h"
#include "imu/icm20948.h"
#include "imu/lsm6ds3.h"
#include "imu/lsm6dsv32x.h"
#include "imu/mpu9150.h"

/*
 * A register file and a clock, which is all the driver asks its transport for. The magnetometer has
 * its own file because it answers at its own address over the bypass, exactly as the reference's
 * does.
 */
typedef struct mock_imu {
    uint8_t regs[128];
    uint8_t mag_regs[128];
    uint8_t who;
    uint8_t last_dev_addr;
    unsigned reads;
    unsigned writes;
    uint32_t delayed_us;
    bool fail_reads;
} mock_imu_t;

static bool mock_read_reg(imu_transport_t *t, uint8_t dev_addr, uint8_t reg, uint8_t *rx,
                          size_t len) {
    mock_imu_t *m = (mock_imu_t *)t->self;
    if (m->fail_reads) {
        return false;
    }
    const uint8_t *file = (dev_addr == 0x0Cu) ? m->mag_regs : m->regs;
    for (size_t i = 0; i < len; i++) {
        rx[i] = file[(reg + i) & 0x7Fu];
    }
    m->last_dev_addr = dev_addr;
    m->reads++;
    if (dev_addr != 0x0Cu && reg == MPU9150_WHO_AM_I) {
        rx[0] = m->who;
    }
    return true;
}

static bool mock_write_reg(imu_transport_t *t, uint8_t dev_addr, uint8_t reg, const uint8_t *tx,
                           size_t len) {
    mock_imu_t *m = (mock_imu_t *)t->self;
    uint8_t *file = (dev_addr == 0x0Cu) ? m->mag_regs : m->regs;
    for (size_t i = 0; i < len; i++) {
        file[(reg + i) & 0x7Fu] = tx[i];
    }
    m->writes++;
    return true;
}

static void mock_recover(imu_transport_t *t) {
    (void)t;
}

static void mock_delay_us(imu_transport_t *t, uint32_t us) {
    mock_imu_t *m = (mock_imu_t *)t->self;
    m->delayed_us += us;
}

static uint16_t mock_max_rate(imu_transport_t *t) {
    (void)t;
    return 1000u;
}

static const imu_transport_interface_t mock_interface = {
    .name = "mock",
    .cpu_bound = false,
    .max_sample_rate = mock_max_rate,
    .read_reg = mock_read_reg,
    .write_reg = mock_write_reg,
    .recover = mock_recover,
    .deinit = NULL,
    .delay_us = mock_delay_us,
};

/* Two little-endian halves, as the accelerometer's registers are. */
static void set_be(mock_imu_t *m, uint8_t reg, int16_t value) {
    m->regs[reg] = (uint8_t)((uint16_t)value >> 8);
    m->regs[reg + 1u] = (uint8_t)(value & 0xFFu);
}

/* And the magnetometer's are the other way round, which is the driver's own note. */
static void set_le(mock_imu_t *m, uint8_t reg, int16_t value) {
    m->mag_regs[reg] = (uint8_t)(value & 0xFFu);
    m->mag_regs[reg + 1u] = (uint8_t)((uint16_t)value >> 8);
}

static void test_mpu9150_probe_and_variants(void **state) {
    (void)state;
    mock_imu_t m;
    memset(&m, 0, sizeof(m));
    imu_transport_t transport = {.interface = &mock_interface, .self = &m};
    mpu9150_state_t st;
    imu_device_t dev = mpu9150_device(&transport, &st);

    assert_string_equal(dev.interface->name, "MPU9X50");

    m.who = 0x68u;
    assert_true(imu_device_configure(&dev, IMU_FILTER_LOW, false));
    assert_string_equal(dev.variant, "9150");

    m.who = 0x71u;
    assert_true(imu_device_configure(&dev, IMU_FILTER_LOW, false));
    assert_string_equal(dev.variant, "9250");

    /* The undocumented identity is accepted and names no variant of its own - and, as the reference
     * leaves it, one that was named before stays named. */
    m.who = 0x69u;
    assert_true(imu_device_configure(&dev, IMU_FILTER_LOW, false));
    assert_string_equal(dev.variant, "9250");

    /* And an identity that is none of them is refused. */
    m.who = 0x99u;
    assert_false(imu_device_configure(&dev, IMU_FILTER_LOW, false));
}

static void test_mpu9150_scales_and_magnetometer(void **state) {
    (void)state;
    mock_imu_t m;
    memset(&m, 0, sizeof(m));
    m.who = 0x68u;
    imu_transport_t transport = {.interface = &mock_interface, .self = &m};
    mpu9150_state_t st;
    imu_device_t dev = mpu9150_device(&transport, &st);
    assert_true(imu_device_configure(&dev, IMU_FILTER_LOW, true));

    /* Full scale on the accelerometer is half the sixteen g it is configured for. */
    set_be(&m, MPU9150_ACCEL_XOUT_H, 16384);
    set_be(&m, MPU9150_ACCEL_XOUT_H + 2u, -16384);
    set_be(&m, MPU9150_ACCEL_XOUT_H + 4u, 0);
    /* The gyroscope's half of two thousand degrees a second. */
    set_be(&m, MPU9150_ACCEL_XOUT_H + 6u, 0); /* temperature, skipped */
    set_be(&m, MPU9150_ACCEL_XOUT_H + 8u, 16384);
    set_be(&m, MPU9150_ACCEL_XOUT_H + 10u, 0);
    set_be(&m, MPU9150_ACCEL_XOUT_H + 12u, -16384);
    /* The magnetometer, in its own byte order and at its own address. */
    set_le(&m, MPU9150_HXL, 2048);

    float accel[3] = {0.0f, 0.0f, 0.0f};
    float gyro[3] = {0.0f, 0.0f, 0.0f};
    float mag[3] = {0.0f, 0.0f, 0.0f};
    assert_true(imu_device_read_sample(&dev, accel, gyro, mag));

    assert_float_equal(accel[0], 8.0f, 1e-4f);
    assert_float_equal(accel[1], -8.0f, 1e-4f);
    assert_float_equal(accel[2], 0.0f, 1e-4f);
    assert_float_equal(gyro[0], 1000.0f, 1e-4f);
    assert_float_equal(gyro[2], -1000.0f, 1e-4f);
    /* The magnetometer is read on the sample *after* the first: the driver copies the last reading
     * it has before it refreshes it, and configure leaves the counter at the decimation point so
     * that the refresh happens on the second sample. The reference reads it the same way. */
    assert_float_equal(mag[0], 0.0f, 1e-6f);
    const unsigned after_first = m.reads;
    assert_true(imu_device_read_sample(&dev, accel, gyro, mag));
    assert_float_equal(mag[0], 600.0f, 1e-3f);
    assert_int_equal(m.reads, after_first + 1u); /* only the accelerometer and gyroscope burst */

    /* And then every tenth sample: the sensor keeps moving, so that the stuck-sensor guard has
     * nothing to say about these. */
    for (int i = 0; i < 8; i++) {
        set_be(&m, MPU9150_ACCEL_XOUT_H, (int16_t)(3000 + i));
        assert_true(imu_device_read_sample(&dev, accel, gyro, mag));
    }
    assert_int_equal(m.reads, after_first + 1u + 8u);
    set_be(&m, MPU9150_ACCEL_XOUT_H, 4000);
    assert_true(imu_device_read_sample(&dev, accel, gyro, mag));
    assert_int_equal(m.reads, after_first + 1u + 8u + 2u);
}

static void test_mpu9150_stuck_sensor_and_failure(void **state) {
    (void)state;
    mock_imu_t m;
    memset(&m, 0, sizeof(m));
    m.who = 0x68u;
    imu_transport_t transport = {.interface = &mock_interface, .self = &m};
    mpu9150_state_t st;
    imu_device_t dev = mpu9150_device(&transport, &st);
    assert_true(imu_device_configure(&dev, IMU_FILTER_LOW, false));

    set_be(&m, MPU9150_ACCEL_XOUT_H, 1234);
    float accel[3];
    float gyro[3];
    float mag[3];
    /* Five identical readings are still readings: the counter has to reach the reference's own
     * threshold, and the sixth is the one that hits it. */
    for (int i = 0; i < 5; i++) {
        assert_true(imu_device_read_sample(&dev, accel, gyro, mag));
    }
    assert_false(imu_device_read_sample(&dev, accel, gyro, mag));

    /* A moved value clears the run, and the failure policy pauses and starts the device over. */
    const unsigned writes_before = m.writes;
    m.fail_reads = true;
    assert_false(imu_device_read_sample(&dev, accel, gyro, mag));
    imu_device_on_read_fail(&dev);
    assert_int_equal(m.delayed_us, 1000u);
    assert_true(m.writes > writes_before);
    m.fail_reads = false;
}

/*
 * The other driver: twelve bytes with the gyroscope first, two sensitivities, and a variant that is
 * recognised by its identity byte.
 */
static void test_lsm6ds3_probe_and_scales(void **state) {
    (void)state;
    mock_imu_t m;
    memset(&m, 0, sizeof(m));
    imu_transport_t transport = {.interface = &mock_interface, .self = &m};
    imu_device_t dev = lsm6ds3_device(&transport);

    assert_string_equal(dev.interface->name, "LSM6DS3");
    assert_null(dev.interface->on_read_fail); /* the reference has none for this one */

    /* The TR-C is the identity 0x6A; the others are accepted without a variant of their own. */
    m.regs[LSM6DS3_ACC_GYRO_WHO_AM_I_REG] = 0x6Au;
    dev.sample_rate_hz = 1000u;
    assert_true(imu_device_configure(&dev, IMU_FILTER_LOW, false));
    assert_string_equal(dev.variant, "TR-C");
    assert_int_equal(dev.dev_addr, LSM6DS3_ACC_GYRO_ADDR_A);

    m.regs[LSM6DS3_ACC_GYRO_WHO_AM_I_REG] = 0x69u;
    assert_true(imu_device_configure(&dev, IMU_FILTER_LOW, false));

    m.regs[LSM6DS3_ACC_GYRO_WHO_AM_I_REG] = 0x99u;
    assert_false(imu_device_configure(&dev, IMU_FILTER_LOW, false));

    /* Gyroscope first, at seventy millidegrees a second per bit; then the accelerometer, at
     * four hundred and eighty-eight microgee per bit. Half of each full scale is half the range. */
    m.regs[LSM6DS3_ACC_GYRO_WHO_AM_I_REG] = 0x6Au;
    dev.sample_rate_hz = 1000u;
    assert_true(imu_device_configure(&dev, IMU_FILTER_LOW, false));
    const uint8_t out = LSM6DS3_ACC_GYRO_OUTX_L_G;
    for (int axis = 0; axis < 3; axis++) {
        const int16_t v = (axis == 1) ? -16384 : 16384;
        const uint8_t o = (uint8_t)(out + 2u * (unsigned)axis);
        m.regs[o] = (uint8_t)(v & 0xFF);
        m.regs[o + 1u] = (uint8_t)((uint16_t)v >> 8);
        const uint8_t a = (uint8_t)(o + 6u);
        m.regs[a] = m.regs[o];
        m.regs[a + 1u] = m.regs[o + 1u];
    }

    float accel[3] = {0.0f, 0.0f, 0.0f};
    float gyro[3] = {0.0f, 0.0f, 0.0f};
    float mag[3] = {1.0f, 1.0f, 1.0f};
    assert_true(imu_device_read_sample(&dev, accel, gyro, mag));
    assert_float_equal(gyro[0], 16384.0f * 0.07f, 1e-2f);
    assert_float_equal(gyro[1], -16384.0f * 0.07f, 1e-2f);
    assert_float_equal(accel[0], 16384.0f * 0.000488f, 1e-3f);
    assert_float_equal(mag[0], 0.0f, 1e-9f); /* this one has no magnetometer at all */

    /* The data-ready signal is a write of its own, and disabling it writes zero. */
    const unsigned writes_before = m.writes;
    imu_device_enable_drdy_output(&dev, true);
    assert_true(m.writes > writes_before);
    imu_device_enable_drdy_output(&dev, false);
    assert_int_equal(m.regs[LSM6DS3_ACC_GYRO_INT1_CTRL], 0u);
}

/*
 * The third driver: one identity byte, a ladder both parts share, and a data-ready mode that
 * quantises the requested rate to the nearest rate the part can actually run at.
 */
static void test_lsm6dsv32x_probe_and_scales(void **state) {
    (void)state;
    mock_imu_t m;
    memset(&m, 0, sizeof(m));
    imu_transport_t transport = {.interface = &mock_interface, .self = &m};
    imu_device_t dev = lsm6dsv32x_device(&transport);

    assert_string_equal(dev.interface->name, "LSM6DSV32X");

    m.regs[0x0Fu] = 0x70u; /* its identity register */
    dev.sample_rate_hz = 1000u;
    assert_true(imu_device_configure(&dev, IMU_FILTER_LOW, false));
    assert_int_equal(dev.dev_addr, 0x6Au);
    assert_int_equal(dev.sample_rate_hz, 1000u); /* polling does not change what was asked for */

    m.regs[0x0Fu] = 0x99u;
    assert_false(imu_device_configure(&dev, IMU_FILTER_LOW, false));

    /* In a data-ready mode the rate is quantised up to one the part has, and that is reported. */
    m.regs[0x0Fu] = 0x70u;
    dev.use_drdy = true;
    dev.sample_rate_hz = 500u;
    assert_true(imu_device_configure(&dev, IMU_FILTER_LOW, false));
    assert_int_equal(dev.sample_rate_hz, 960u);
    dev.use_drdy = false;
    dev.sample_rate_hz = 1000u;
    assert_true(imu_device_configure(&dev, IMU_FILTER_LOW, false));

    /* The active pair of full scales, in the reference's own choice: four thousand degrees a second
     * and thirty-two g, so half of each range is half the sensitivity's worth. */
    const uint8_t out = 0x22u;
    for (int axis = 0; axis < 3; axis++) {
        const int16_t v = (axis == 2) ? -16384 : 16384;
        const uint8_t o = (uint8_t)(out + 2u * (unsigned)axis);
        m.regs[o] = (uint8_t)(v & 0xFF);
        m.regs[o + 1u] = (uint8_t)((uint16_t)v >> 8);
        const uint8_t a = (uint8_t)(o + 6u);
        m.regs[a] = m.regs[o];
        m.regs[a + 1u] = m.regs[o + 1u];
    }

    float accel[3] = {0.0f, 0.0f, 0.0f};
    float gyro[3] = {0.0f, 0.0f, 0.0f};
    float mag[3] = {1.0f, 1.0f, 1.0f};
    assert_true(imu_device_read_sample(&dev, accel, gyro, mag));
    assert_float_equal(gyro[0], 16384.0f * 0.14f, 1e-2f);
    assert_float_equal(gyro[2], -16384.0f * 0.14f, 1e-2f);
    assert_float_equal(accel[0], 16384.0f * 0.000976f, 1e-3f);
    assert_float_equal(mag[0], 0.0f, 1e-9f); /* a six-axis part: no magnetometer at all */

    imu_device_enable_drdy_output(&dev, true);
    assert_int_equal(m.regs[0x0Du], 0x02u); /* the gyroscope's data-ready on the first pin */
    imu_device_enable_drdy_output(&dev, false);
    assert_int_equal(m.regs[0x0Du], 0u);
}

/*
 * The fourth driver, and the one whose twelve bytes come with the accelerometer first - which is
 * the other way round from the two beside it, so the case is written to tell them apart.
 */
static void test_icm20948_order_and_failure_policy(void **state) {
    (void)state;
    mock_imu_t m;
    memset(&m, 0, sizeof(m));
    imu_transport_t transport = {.interface = &mock_interface, .self = &m};
    imu_device_t dev = icm20948_device(&transport);

    assert_string_equal(dev.interface->name, "ICM20948");
    assert_null(dev.interface->enable_drdy_output); /* the reference wires none for this one */

    /* This driver probes nothing: it takes the one address it knows and starts the part over. */
    assert_true(imu_device_configure(&dev, IMU_FILTER_LOW, false));
    assert_int_equal(dev.dev_addr, 0x68u);
    assert_int_equal(m.regs[0x7Fu], 0u); /* left on the bank the data registers live in */

    /* Six bytes of accelerometer then six of gyroscope, with the ends of both ranges to tell the
     * order apart: a swap would put eight g where a thousand degrees a second belongs. */
    const uint8_t out = ICM20948_ACCEL_XOUT_H;
    const int16_t accel_vals[3] = {16384, 0, -16384};
    const int16_t gyro_vals[3] = {-16384, 0, 16384};
    for (int axis = 0; axis < 3; axis++) {
        const uint8_t a = (uint8_t)(out + 2u * (unsigned)axis);
        m.regs[a] = (uint8_t)((uint16_t)accel_vals[axis] >> 8);
        m.regs[a + 1u] = (uint8_t)(accel_vals[axis] & 0xFF);
        const uint8_t g = (uint8_t)(a + 6u);
        m.regs[g] = (uint8_t)((uint16_t)gyro_vals[axis] >> 8);
        m.regs[g + 1u] = (uint8_t)(gyro_vals[axis] & 0xFF);
    }

    float accel[3] = {0.0f, 0.0f, 0.0f};
    float gyro[3] = {0.0f, 0.0f, 0.0f};
    float mag[3] = {1.0f, 1.0f, 1.0f};
    assert_true(imu_device_read_sample(&dev, accel, gyro, mag));
    assert_float_equal(accel[0], 8.0f, 1e-4f);
    assert_float_equal(accel[2], -8.0f, 1e-4f);
    assert_float_equal(gyro[0], -1000.0f, 1e-3f);
    assert_float_equal(gyro[2], 1000.0f, 1e-3f);
    assert_float_equal(mag[0], 0.0f, 1e-9f);

    /* Its failure policy starts the part over and then waits ten milliseconds, on top of the
     * millisecond the start-over itself takes. */
    m.delayed_us = 0u;
    imu_device_on_read_fail(&dev);
    assert_int_equal(m.delayed_us, 11000u);
}

/*
 * The fifth driver, which is Bosch's own behind a twenty-line wrapper. This case checks the
 * wrapper's share: the bus it is told to use, the address that follows from it, and the two paths
 * that do not need the chip to answer - a dead bus, and a failure worth backing off from.
 */
static void test_bmi160_bus_and_dead_bus(void **state) {
    (void)state;
    mock_imu_t m;
    memset(&m, 0, sizeof(m));
    imu_transport_t transport = {.interface = &mock_interface, .self = &m};

    struct bmi160_dev sensor;
    memset(&sensor, 0, sizeof(sensor));
    imu_device_t dev = bmi160_device(&transport, BMI160_I2C_INTF, &sensor);

    assert_string_equal(dev.interface->name, "BMI160");
    assert_null(
        dev.interface->enable_drdy_output); /* the reference wires none for this one either */
    assert_true(dev.priv == &sensor); /* the caller's structure, not one of the driver's own */
    assert_int_equal(dev.dev_addr, BMI160_I2C_ADDR); /* an I2C address... */

    /* ...and none at all for SPI, which is how the wrapper tells the two apart. */
    struct bmi160_dev spi_sensor;
    memset(&spi_sensor, 0, sizeof(spi_sensor));
    imu_device_t spi = bmi160_device(&transport, BMI160_SPI_INTF, &spi_sensor);
    assert_int_equal(spi.dev_addr, 0u);

    /* A bus that answers nothing cannot be configured, and the vendor driver is what discovers it.
     */
    m.fail_reads = true;
    assert_false(imu_device_configure(&dev, IMU_FILTER_LOW, false));
    assert_int_equal(dev.dev_addr, BMI160_I2C_ADDR);

    /* Its failure policy is a back-off, since the vendor driver recovers on its own. */
    m.delayed_us = 0u;
    imu_device_on_read_fail(&dev);
    assert_int_equal(m.delayed_us, 5000u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_mpu9150_probe_and_variants),
        cmocka_unit_test(test_mpu9150_scales_and_magnetometer),
        cmocka_unit_test(test_mpu9150_stuck_sensor_and_failure),
        cmocka_unit_test(test_lsm6ds3_probe_and_scales),
        cmocka_unit_test(test_lsm6dsv32x_probe_and_scales),
        cmocka_unit_test(test_icm20948_order_and_failure_policy),
        cmocka_unit_test(test_bmi160_bus_and_dead_bus),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
